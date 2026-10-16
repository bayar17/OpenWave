#include "PermissionService.h"
#include <QDateTime>
#include <QDebug>
#include <QUuid>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <OpenWave/OpenWaveIdentity.h>
#include <OpenWave/OpenWaveXml.h>

namespace {
constexpr auto busName = "org.openwave.Permissions";
constexpr auto path = "/org/openwave/Permissions";
constexpr auto interface = "org.openwave.Permissions1";
constexpr auto promptPath = "/org/openwave/Prompt";
constexpr auto promptInterface = "org.openwave.Prompt1";
void fail(GDBusMethodInvocation *call, const char *message)
{ g_dbus_method_invocation_return_dbus_error(call, "org.openwave.Permissions.Error.Denied", message); }
QString string(GVariant *fields, const char *key)
{ const char *value = ""; g_variant_lookup(fields, key, "&s", &value); return QString::fromUtf8(value); }
QByteArray utf8(const QString &value) { return value.toUtf8(); }

}
struct PermissionService::Waiter {
    QString sender, subject, permission;
    PermissionIdentity identity;
    bool brokered = false;
    GDBusMethodInvocation *call = nullptr;
    ~Waiter() { if (call) g_object_unref(call); }
};
struct PermissionService::Pending {
    PermissionService *service;
    PermissionIdentity identity;
    QString permission, owner, prompt, correlation;
    std::vector<std::shared_ptr<Waiter>> waiters;
    qint64 deadline;
    bool finished = false, polling = false;
};
struct PermissionService::Lease {
    QString broker, subject, permission, app, instance, session;
    guint user;
    guint64 start;
    qint64 expires;
};
PermissionService::PermissionService(QString directory) : m_store(directory), m_policyRoot(std::move(directory)) {}
PermissionService::~PermissionService()
{
    if (m_timer) g_source_remove(m_timer);
    if (m_bus && m_subscription) g_dbus_connection_signal_unsubscribe(m_bus, m_subscription);
    if (m_owner) g_bus_unown_name(m_owner);
    if (m_bus && m_registration) g_dbus_connection_unregister_object(m_bus, m_registration);
    for (const auto &pending : m_pending) if (!pending->finished) complete(pending, "service_lost", "");
    if (m_promptBus) g_object_unref(m_promptBus);
    m_lifecycle.reset();
    if (m_bus) g_object_unref(m_bus);
}
bool PermissionService::start(GError **error)
{
    m_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, error);
    if (!m_bus) return false;
    m_promptBus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, error);
    if (!m_promptBus) return false;
    char *appOwner = openwave_service_owner(m_bus, "org.openwave.Applications", "/run/openwave/appd.pid", error);
    if (!appOwner) return false;
    m_appOwner = QString::fromUtf8(appOwner); g_free(appOwner);
    auto *session = g_dbus_connection_call_sync(m_bus, utf8(m_appOwner).constData(), "/org/openwave/Applications", "org.openwave.Applications1",
        "GetSessionIdentity", nullptr, G_VARIANT_TYPE("(us)"), G_DBUS_CALL_FLAGS_NONE, 1500, nullptr, error);
    if (!session) return false;
    guint user; const char *sessionId; g_variant_get(session, "(u&s)", &user, &sessionId);
    m_sessionUser = user;
    m_store = PolicyStore(m_policyRoot + '/' + QString::number(user)); g_variant_unref(session);
    QString failure;
    if (!m_store.load(&failure)) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, utf8(failure).constData()); return false; }
    for (const auto &policy : m_store.list(user)) {
        if (policy.revocationPending && !finishRevocation(policy, &failure)) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, utf8(failure).constData()); return false;
        }
    }
    m_lifecycle = std::make_unique<LifecycleBridge>(m_bus, m_appOwner,
        [this](const QString &event, const QString &app, const QString &instance, bool replaced) { lifecycle(event, app, instance, replaced); });
    if (!m_lifecycle->start(user, error)) return false;
    for (auto policy : m_store.list(user)) {
        const auto installed = m_lifecycle->applications().value(policy.application);
        if (installed.id.isEmpty() || (!policy.applicationDigest.isEmpty() && policy.applicationDigest != installed.digest)) {
            if (!m_store.removeApplication(user, policy.application, &failure)) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, utf8(failure).constData()); return false; }
        } else if ((policy.duration == "application" || policy.duration == "once")
            && m_lifecycle->instances().value(policy.instance) != policy.application) {
            if (!m_store.expireInstance(user, policy.application, policy.instance, &failure)) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, utf8(failure).constData()); return false; }
        } else if (policy.applicationDigest.isEmpty()) {
            policy.applicationDigest = installed.digest;
            if (!m_store.set(policy, &failure)) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, utf8(failure).constData()); return false; }
        }
    }
    auto *node = openwave_xml_load_dbus_interface(interface, error);
    if (!node) return false;
    static const GDBusInterfaceVTable vtable = {dispatch, nullptr, nullptr, {nullptr}};
    m_registration = g_dbus_connection_register_object(m_bus, path, node->interfaces[0], &vtable, this, nullptr, error);
    g_dbus_node_info_unref(node);
    if (!m_registration) return false;
    auto *reply = g_dbus_connection_call_sync(m_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "RequestName", g_variant_new("(su)", busName, 4u), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, error);
    if (!reply) return false;
    guint result; g_variant_get(reply, "(u)", &result); g_variant_unref(reply);
    if (result != 1) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS, "Another permissiond owns the service"); return false; }
    m_subscription = g_dbus_connection_signal_subscribe(m_bus, "org.freedesktop.DBus", "org.freedesktop.DBus",
        "NameOwnerChanged", "/org/freedesktop/DBus", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, nameChanged, this, nullptr);
    m_timer = g_timeout_add(200, tick, this);
    return true;
}
bool PermissionService::identity(const char *sender, PermissionIdentity *id)
{
    GError *error = nullptr;
    auto *fields = openwave_resolve_caller(m_bus, sender, &error);
    if (!fields) { g_clear_error(&error); return false; }
    id->app = string(fields, "appId"); id->name = string(fields, "name"); id->session = string(fields, "session");
    id->instance = string(fields, "instance"); id->userRoot = string(fields, "userRoot");
    id->digest = string(fields, "manifestDigest");
    gboolean trusted = false, admin = false;
    bool okay = g_variant_lookup(fields, "userId", "u", &id->user)
        && g_variant_lookup(fields, "processUserId", "u", &id->uid)
        && g_variant_lookup(fields, "pid", "u", &id->pid)
        && g_variant_lookup(fields, "leaderPid", "u", &id->leader)
        && g_variant_lookup(fields, "startTime", "t", &id->start)
        && g_variant_lookup(fields, "trusted", "b", &trusted)
        && g_variant_lookup(fields, "administrator", "b", &admin)
        && !id->app.isEmpty() && !id->session.isEmpty() && id->start
        && !id->digest.isEmpty() && m_lifecycle
        && m_lifecycle->applications().value(id->app).digest == id->digest;
    id->trusted = trusted; id->administrator = admin;
    g_variant_unref(fields); return okay;
}
bool PermissionService::broker(const char *sender)
{
    GError *error = nullptr;
    char *owner = openwave_service_owner(m_bus, "org.openwave.portal", "/run/openwave/portal.pid", &error);
    const bool okay = owner && !strcmp(owner, sender);
    g_free(owner); g_clear_error(&error); return okay;
}
void PermissionService::dispatch(GDBusConnection *, const char *sender, const char *, const char *, const char *method,
    GVariant *arguments, GDBusMethodInvocation *call, void *data)
{ static_cast<PermissionService *>(data)->method(sender, method, arguments, call); }

void PermissionService::method(const char *sender, const char *method, GVariant *args, GDBusMethodInvocation *call)
{
    if (!strcmp(method, "QueryFor")) {
        const char *subject, *permission, *correlation; g_variant_get(args, "(&s&s&s)", &subject, &permission, &correlation);
        PermissionIdentity id;
        if (!broker(sender) || !identity(subject, &id)) { fail(call, "A supervised broker and live subject are required"); return; }
        if (!g_uuid_string_is_valid(correlation) || !PolicyStore::validPermission(QString::fromUtf8(permission))) { fail(call, "Invalid broker query"); return; }
        const auto state = id.trusted && PolicyStore::permissions().contains(QString::fromUtf8(permission)) ? QStringLiteral("granted")
            : m_store.get(id.user, id.app, QString::fromUtf8(permission), id.session, id.instance).state;
        g_dbus_method_invocation_return_value(call, g_variant_new("(ss)", correlation, utf8(state).constData())); return;
    }
    if (!strcmp(method, "AuthorizeFileChooser")) {
        if (!broker(sender)) { fail(call, "Only the supervised file broker may authorize file access"); return; }
        const char *subject, *permission; g_variant_get(args, "(&s&s)", &subject, &permission);
        PermissionIdentity id;
        if (!identity(subject, &id)) { fail(call, "Application identity is unavailable"); return; }
        request(QString::fromUtf8(subject), id, QString::fromUtf8(permission), true, call); return;
    }
    if (!strcmp(method, "ValidateLease") || !strcmp(method, "ReleaseLease")) {
        if (!broker(sender)) { fail(call, "Only the file broker may use leases"); return; }
        const char *token, *subject; g_variant_get(args, "(&s&s)", &token, &subject);
        auto lease = m_leases.value(QString::fromUtf8(token));
        PermissionIdentity current;
        bool okay = lease && lease->broker == QString::fromUtf8(sender) && lease->subject == QString::fromUtf8(subject)
            && lease->expires > QDateTime::currentMSecsSinceEpoch() && identity(subject, &current)
            && current.app == lease->app && current.user == lease->user && current.start == lease->start
            && current.instance == lease->instance && current.session == lease->session;
        if (okay && !current.trusted) {
            const auto policy = m_store.get(current.user, current.app, lease->permission, current.session, current.instance);
            okay = policy.state == "granted" || (policy.duration == "once" && policy.state == "not-determined"
                && policy.expires > QDateTime::currentMSecsSinceEpoch());
        }
        if (okay || !strcmp(method, "ReleaseLease")) m_leases.remove(QString::fromUtf8(token));
        if (!strcmp(method, "ValidateLease")) g_dbus_method_invocation_return_value(call, g_variant_new("(b)", okay));
        else g_dbus_method_invocation_return_value(call, nullptr);
        return;
    }
    PermissionIdentity id;
    if (!identity(sender, &id)) { fail(call, "A live application identity from appd is required"); return; }
    if (!strcmp(method, "SubscribePermissionChanges")) {
        if (m_watchers.size() >= 256 && !m_watchers.contains(QString::fromUtf8(sender))) { fail(call, "Too many subscribers"); return; }
        m_watchers.insert(QString::fromUtf8(sender), id);
        g_dbus_method_invocation_return_value(call, nullptr); return;
    }
    if (!strcmp(method, "ListPermissions") || !strcmp(method, "ListApplicationsPermissions")) {
        bool admin = !strcmp(method, "ListApplicationsPermissions");
        if (admin && !id.administrator) { fail(call, "Settings authorization is required"); return; }
        g_dbus_method_invocation_return_value(call, g_variant_new("(@aa{sv})", records(id, admin))); return;
    }
    if (!strcmp(method, "RevokePermission") || !strcmp(method, "SetPermission")) {
        if (!id.administrator) { fail(call, "Only authenticated Settings may change application policies"); return; }
        const char *app, *permission, *state = "not-determined", *duration = "permanent";
        if (!strcmp(method, "SetPermission")) g_variant_get(args, "(&s&s&s&s)", &app, &permission, &state, &duration);
        else g_variant_get(args, "(&s&s)", &app, &permission);
        if (!PolicyStore::permissions().contains(QString::fromUtf8(permission)) || !PolicyStore::validState(QString::fromUtf8(state))
            || !PolicyStore::validDuration(QString::fromUtf8(duration)) || !strcmp(duration, "once") || !strcmp(duration, "application")
            || strlen(app) > 128) { fail(call, "Invalid administrative policy"); return; }
        const auto installed = m_lifecycle->applications().value(QString::fromUtf8(app));
        if (installed.id.isEmpty()) { fail(call, "Application is no longer installed"); return; }
        if (installed.trusted) { fail(call, "Trusted system components do not have revocable application policies"); return; }
        PermissionPolicy policy; policy.application = QString::fromUtf8(app); policy.user = id.user;
        policy.applicationDigest = installed.digest;
        policy.permission = QString::fromUtf8(permission); policy.state = QString::fromUtf8(state);
        policy.duration = QString::fromUtf8(duration); policy.session = id.session;
        policy.revocationPending = policy.state != "granted";
        QString failure;
        if (!m_store.set(policy, &failure)) { fail(call, utf8(failure).constData()); return; }
        invalidate(policy.application, policy.user);
        PermissionIdentity target = id; target.app = policy.application;
        changed(target, policy.permission, policy.state);
        if (policy.revocationPending && !finishRevocation(policy, &failure)) { fail(call, utf8(failure).constData()); return; }
        g_dbus_method_invocation_return_value(call, nullptr); return;
    }
    const char *permission = nullptr; g_variant_get(args, "(&s)", &permission);
    const QString name = QString::fromUtf8(permission);
    if (!strcmp(method, "RequestPermission")) { request(QString::fromUtf8(sender), id, name, false, call); return; }
    if (!strcmp(method, "CheckPermission") || !strcmp(method, "GetPermissionStatus")) {
        const QString state = id.trusted && PolicyStore::permissions().contains(name) ? QStringLiteral("granted")
            : m_store.get(id.user, id.app, name, id.session, id.instance).state;
        g_dbus_method_invocation_return_value(call, g_variant_new("(s)", utf8(state).constData())); return;
    }
    fail(call, "Unsupported permission operation");
}

void PermissionService::respond(Waiter &waiter, const QString &state)
{
    if (!waiter.brokered) {
        g_dbus_method_invocation_return_value(waiter.call, g_variant_new("(s)", utf8(state).constData())); return;
    }
    if (state != "granted") { fail(waiter.call, "File access was not authorized"); return; }
    if (m_leases.size() >= 256) { fail(waiter.call, "Too many outstanding file grants"); return; }
    const auto policy = m_store.get(waiter.identity.user, waiter.identity.app, waiter.permission, waiter.identity.session, waiter.identity.instance);
    if (!waiter.identity.trusted && policy.state != "granted") { fail(waiter.call, "The permission grant was already consumed"); return; }
    auto lease = std::make_shared<Lease>();
    lease->broker = waiter.sender; lease->subject = waiter.subject; lease->permission = waiter.permission;
    lease->app = waiter.identity.app; lease->user = waiter.identity.user; lease->instance = waiter.identity.instance;
    lease->session = waiter.identity.session; lease->start = waiter.identity.start;
    lease->expires = QDateTime::currentMSecsSinceEpoch() + 120000;
    if (policy.duration == "once") {
        auto consumed = policy; consumed.state = "not-determined"; QString error;
        if (!m_store.set(consumed, &error)) { fail(waiter.call, utf8(error).constData()); return; }
        changed(waiter.identity, waiter.permission, consumed.state);
    }
    const auto token = QUuid::createUuid().toString(QUuid::Id128);
    m_leases.insert(token, lease);
    g_dbus_method_invocation_return_value(waiter.call, g_variant_new("(s)", utf8(token).constData()));
}

void PermissionService::request(const QString &sender, const PermissionIdentity &id, const QString &permission, bool brokered, GDBusMethodInvocation *call)
{
    auto waiter = std::make_shared<Waiter>(); waiter->subject = sender; waiter->permission = permission;
    waiter->identity = id; waiter->brokered = brokered; waiter->call = G_DBUS_METHOD_INVOCATION(g_object_ref(call));
    waiter->sender = QString::fromUtf8(g_dbus_method_invocation_get_sender(call));
    auto policy = m_store.get(id.user, id.app, permission, id.session, id.instance);
    if (!PolicyStore::validPermission(permission)) { fail(call, "Invalid permission identifier"); return; }
    policy.applicationDigest = m_lifecycle->applications().value(id.app).digest;
    if (!policy.requestedAt) {
        if (!PolicyStore::permissions().contains(permission) && m_store.list(id.user, id.app).size() >= 32) {
            fail(call, "Too many requested permission identifiers"); return;
        }
        policy.requestedAt = QDateTime::currentMSecsSinceEpoch(); QString error;
        if (!m_store.set(policy, &error)) { fail(call, utf8(error).constData()); return; }
        changed(id, permission, policy.state);
    }
    if (id.trusted && PolicyStore::permissions().contains(permission)) { respond(*waiter, "granted"); return; }
    if (policy.state != "not-determined") { respond(*waiter, policy.state); return; }
    for (const auto &pending : m_pending) {
        if (!pending->finished && pending->identity.app == id.app && pending->identity.user == id.user && pending->identity.instance == id.instance && pending->permission == permission) {
            if (pending->waiters.size() >= 16) { fail(call, "Too many concurrent permission requests"); return; }
            pending->waiters.push_back(waiter); return;
        }
    }
    if (m_pending.size() >= 32) { fail(call, "Too many pending permission requests"); return; }
    GError *error = nullptr;
    char *owner = openwave_service_owner(m_promptBus, "org.openwave.Prompt", "/run/openwave/promptd.pid", &error);
    if (!owner) { g_clear_error(&error); respond(*waiter, "denied"); return; }
    auto pending = std::make_shared<Pending>(); pending->service = this; pending->identity = id;
    pending->owner = QString::fromUtf8(owner); g_free(owner); pending->permission = permission;
    pending->correlation = QUuid::createUuid().toString(QUuid::WithoutBraces);
    qInfo().noquote() << "permissiond: request" << pending->correlation << id.app << permission;
    pending->deadline = QDateTime::currentMSecsSinceEpoch() + 125000;
    pending->waiters.push_back(waiter); m_pending.push_back(pending);
    GVariantBuilder options, choices; g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_init(&choices, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&choices, "{sv}", "allow_once", g_variant_new_string("Allow once"));
    g_variant_builder_add(&choices, "{sv}", "allow_application", g_variant_new_string("Allow while using the application"));
    if (m_registry.definition(permission).permanent)
        g_variant_builder_add(&choices, "{sv}", "allow_permanent", g_variant_new_string("Always allow"));
    g_variant_builder_add(&choices, "{sv}", "deny", g_variant_new_string("Don't allow"));
    g_variant_builder_add(&options, "{sv}", "choices", g_variant_builder_end(&choices));
    const auto title = utf8(id.name.left(80) + ": " + m_registry.definition(permission).label);
    g_variant_builder_add(&options, "{sv}", "title", g_variant_new_string(title.constData()));
    const auto message = utf8(m_registry.definition(permission).description);
    g_variant_builder_add(&options, "{sv}", "message", g_variant_new_string(message.constData()));
    g_variant_builder_add(&options, "{sv}", "acceptLabel", g_variant_new_string("Continue"));
    g_variant_builder_add(&options, "{sv}", "cancelLabel", g_variant_new_string("Cancel"));
    g_variant_builder_add(&options, "{sv}", "initialText", g_variant_new_string("deny"));
    auto *holder = new std::shared_ptr<Pending>(pending);
    g_dbus_connection_call(m_promptBus, utf8(pending->owner).constData(), promptPath, promptInterface, "RequestFor",
        g_variant_new("(ss@a{sv})", utf8(sender).constData(), "selection", g_variant_builder_end(&options)), G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, [](GObject *source, GAsyncResult *result, gpointer data) {
            std::unique_ptr<std::shared_ptr<Pending>> holder(static_cast<std::shared_ptr<Pending> *>(data)); auto p = *holder;
            GError *error = nullptr; auto *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
            if (!reply) { qWarning() << "permissiond: prompt IPC failed:" << (error ? error->message : "unknown error"); g_clear_error(&error); p->service->complete(p, "service_lost", ""); return; }
            const char *id; g_variant_get(reply, "(&s)", &id); p->prompt = QString::fromUtf8(id); g_variant_unref(reply);
            if (p->finished) {
                g_dbus_connection_call(p->service->m_promptBus, utf8(p->owner).constData(), promptPath, promptInterface,
                    "Cancel", g_variant_new("(s)", utf8(p->prompt).constData()), nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr, nullptr);
            }
        }, holder);
}

void PermissionService::poll(const std::shared_ptr<Pending> &pending)
{
    if (pending->polling || pending->prompt.isEmpty() || pending->finished) return;
    pending->polling = true;
    auto *holder = new std::shared_ptr<Pending>(pending);
    g_dbus_connection_call(m_promptBus, utf8(pending->owner).constData(), promptPath, promptInterface, "GetResult",
        g_variant_new("(s)", utf8(pending->prompt).constData()), G_VARIANT_TYPE("(bss)"), G_DBUS_CALL_FLAGS_NONE, 1000, nullptr,
        [](GObject *source, GAsyncResult *result, gpointer data) {
            std::unique_ptr<std::shared_ptr<Pending>> holder(static_cast<std::shared_ptr<Pending> *>(data)); auto p = *holder;
            GError *error = nullptr; auto *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
            p->polling = false;
            if (!reply) { qWarning() << "permissiond: prompt IPC failed:" << (error ? error->message : "unknown error"); g_clear_error(&error); p->service->complete(p, "service_lost", ""); return; }
            gboolean finished; const char *state, *choice; g_variant_get(reply, "(b&s&s)", &finished, &state, &choice);
            if (finished) p->service->complete(p, QString::fromUtf8(state), QString::fromUtf8(choice));
            g_variant_unref(reply);
        }, holder);
}

void PermissionService::complete(const std::shared_ptr<Pending> &pending, const QString &result, const QString &choice)
{
    if (pending->finished) return;
    pending->finished = true;
    qInfo().noquote() << "permissiond: completed" << pending->correlation << result;
    QString state = "denied";
    bool live = false;
    for (const auto &waiter : pending->waiters) {
        PermissionIdentity current;
        live |= identity(utf8(waiter->subject).constData(), &current) && current.app == waiter->identity.app
            && current.start == waiter->identity.start && current.session == waiter->identity.session;
    }
    if (live && (choice != "allow_permanent" || m_registry.definition(pending->permission).permanent) && result == "accepted"
        && QStringList{"deny", "allow_once", "allow_application", "allow_permanent"}.contains(choice)) {
        PermissionPolicy policy;
        policy.application = pending->identity.app; policy.user = pending->identity.user; policy.permission = pending->permission;
        policy.state = choice == "deny" ? "denied" : "granted";
        policy.duration = choice == "allow_once" ? "once" : choice == "allow_application" ? "application" : "permanent";
        policy.session = pending->identity.session; policy.instance = pending->identity.instance;
        policy.applicationDigest = m_lifecycle->applications().value(policy.application).digest;
        if (policy.duration == "once") policy.expires = QDateTime::currentMSecsSinceEpoch() + 120000;
        QString error;
        if (m_store.set(policy, &error)) { state = policy.state; changed(pending->identity, pending->permission, state); }
        else qWarning().noquote() << "permissiond:" << error;
    }
    for (const auto &waiter : pending->waiters) {
        PermissionIdentity current;
        if (!identity(utf8(waiter->subject).constData(), &current) || current.start != waiter->identity.start
            || current.app != waiter->identity.app || current.session != waiter->identity.session) respond(*waiter, "denied");
        else respond(*waiter, state);
    }
    pending->waiters.clear();
    if (!pending->prompt.isEmpty()) {
        const char *operation = result == "accepted" || result == "cancelled" ? "Acknowledge" : "Cancel";
        g_dbus_connection_call(m_promptBus, utf8(pending->owner).constData(), promptPath, promptInterface,
            operation, g_variant_new("(s)", utf8(pending->prompt).constData()), nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr, nullptr);
    }
}
bool PermissionService::finishRevocation(PermissionPolicy policy, QString *failure)
{
    GError *error = nullptr;
    auto *reply = g_dbus_connection_call_sync(m_bus, utf8(m_appOwner).constData(), "/org/openwave/Applications", "org.openwave.Applications1",
        "RevokeAccess", g_variant_new("(s)", utf8(policy.application).constData()), G_VARIANT_TYPE("()"), G_DBUS_CALL_FLAGS_NONE, 2000, nullptr, &error);
    if (!reply) { *failure = error ? QString::fromUtf8(error->message) : QStringLiteral("Could not revoke live application access"); g_clear_error(&error); return false; }
    g_variant_unref(reply); policy.revocationPending = false;
    return m_store.set(policy, failure);
}
void PermissionService::invalidate(const QString &app, guint user, const QString &instance)
{
    for (auto it = m_leases.begin(); it != m_leases.end(); ) {
        if (it.value()->app == app && it.value()->user == user && (instance.isEmpty() || it.value()->instance == instance)) it = m_leases.erase(it); else ++it;
    }
    const auto pending = m_pending;
    for (const auto &p : pending) if (!p->finished && p->identity.app == app && p->identity.user == user && (instance.isEmpty() || p->identity.instance == instance)) complete(p, "revoked", "");
}
void PermissionService::changed(const PermissionIdentity &id, const QString &permission, const QString &state)
{
    qInfo().noquote() << "permissiond: decision" << id.app << permission << state;
    for (auto it = m_watchers.cbegin(); it != m_watchers.cend(); ++it) {
        if (it->user != id.user || (!it->administrator && it->app != id.app)) continue;
        g_dbus_connection_emit_signal(m_bus, utf8(it.key()).constData(), path, interface, "PermissionChanged",
            g_variant_new("(sss)", utf8(id.app).constData(), utf8(permission).constData(), utf8(state).constData()), nullptr);
    }
}
GVariant *PermissionService::records(const PermissionIdentity &id, bool administrative)
{
    GVariantBuilder result; g_variant_builder_init(&result, G_VARIANT_TYPE("aa{sv}"));
    auto append = [&](const QString &app, const QString &name, bool trusted) {
        const auto declared = m_lifecycle->applications().value(app).permissions;
        auto identifiers = PolicyStore::permissions();
        for (const auto &permission : declared) if (!identifiers.contains(permission)) identifiers.append(permission);
        for (const auto &stored : m_store.list(id.user, app)) if (!identifiers.contains(stored.permission)) identifiers.append(stored.permission);
        for (const auto &permission : identifiers) {
            QString instance = id.instance;
            if (administrative) for (const auto &stored : m_store.list(id.user, app))
                if (stored.permission == permission && m_lifecycle->instances().value(stored.instance) == app) instance = stored.instance;
            const auto policy = m_store.get(id.user, app, permission, id.session, instance);
            const bool supported = PolicyStore::permissions().contains(permission);
            GVariantBuilder row; g_variant_builder_init(&row, G_VARIANT_TYPE("a{sv}"));
            const QMap<QString, QString> fields{{"application", app}, {"name", name}, {"permission", permission},
                {"label", m_registry.definition(permission).label}, {"state", trusted && supported ? QStringLiteral("granted") : policy.state}, {"duration", policy.duration}};
            for (auto it = fields.cbegin(); it != fields.cend(); ++it)
                g_variant_builder_add(&row, "{sv}", utf8(it.key()).constData(), g_variant_new_string(utf8(it.value()).constData()));
            g_variant_builder_add(&row, "{sv}", "trusted", g_variant_new_boolean(trusted));
            g_variant_builder_add(&row, "{sv}", "createdAt", g_variant_new_int64(policy.created));
            g_variant_builder_add(&row, "{sv}", "modifiedAt", g_variant_new_int64(policy.modified));
            g_variant_builder_add(&row, "{sv}", "requestedAt", g_variant_new_int64(policy.requestedAt));
            g_variant_builder_add(&row, "{sv}", "declared", g_variant_new_boolean(declared.contains(permission)));
            g_variant_builder_add(&row, "{sv}", "supported", g_variant_new_boolean(supported));
            g_variant_builder_add_value(&result, g_variant_builder_end(&row));
        }
    };
    if (!administrative) append(id.app, id.name, id.trusted);
    else {
        for (const auto &app : m_lifecycle->applications()) append(app.id, app.name, app.trusted);
    }
    return g_variant_builder_end(&result);
}
void PermissionService::nameChanged(GDBusConnection *, const char *, const char *, const char *, const char *, GVariant *args, void *data)
{
    auto *service = static_cast<PermissionService *>(data);
    const char *name, *old, *next; g_variant_get(args, "(&s&s&s)", &name, &old, &next);
    if (!strcmp(name, "org.openwave.Applications") && *old) {
        service->m_leases.clear();
        auto pending = service->m_pending;
        for (const auto &p : pending) service->complete(p, "service_lost", "");
        // PID 1 restarts us against the new identity registry; never reuse its old session.
        _exit(1);
    }
    if (*old && !*next && name[0] == ':') {
        service->m_watchers.remove(QString::fromUtf8(name));
        const auto pending = service->m_pending;
        for (const auto &p : pending) {
            if (p->finished) continue;
            auto &waiters = p->waiters;
            waiters.erase(std::remove_if(waiters.begin(), waiters.end(), [name](const auto &w) {
                return w->subject == QString::fromUtf8(name);
            }), waiters.end());
            if (waiters.empty()) service->complete(p, "caller_lost", "");
        }
        for (auto it = service->m_leases.begin(); it != service->m_leases.end(); ) {
            if (it.value()->subject == QString::fromUtf8(name) || it.value()->broker == QString::fromUtf8(name)) it = service->m_leases.erase(it); else ++it;
        }
    }
}
gboolean PermissionService::tick(void *data)
{
    auto *service = static_cast<PermissionService *>(data); const qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto pending = service->m_pending;
    for (const auto &p : pending) {
        if (!p->finished && p->deadline <= now) service->complete(p, "timeout", "");
        else service->poll(p);
    }
    auto &queue = service->m_pending;
    queue.erase(std::remove_if(queue.begin(), queue.end(), [](const auto &p) { return p->finished && !p->polling; }), queue.end());
    for (auto it = service->m_leases.begin(); it != service->m_leases.end(); ) {
        if (it.value()->expires <= now) it = service->m_leases.erase(it); else ++it;
    }
    for (auto policy : service->m_store.list(service->m_sessionUser)) {
        if (policy.state != "granted" || !policy.expires || policy.expires > now) continue;
        policy.state = "not-determined"; policy.expires = 0;
        QString error;
        if (service->m_store.set(policy, &error)) {
            PermissionIdentity id; id.app = policy.application; id.user = policy.user;
            service->changed(id, policy.permission, policy.state);
        } else qWarning().noquote() << "permissiond:" << error;
    }
    return G_SOURCE_CONTINUE;
}

void PermissionService::lifecycle(const QString &event, const QString &app, const QString &instance, bool replaced)
{
    QString error;
    if (event == "ApplicationTerminated") {
        invalidate(app, m_sessionUser, instance);
        if (!m_store.expireInstance(m_sessionUser, app, instance, &error)) { qCritical().noquote() << error; _exit(1); }
    } else if (event == "ApplicationRemoved" || replaced) {
        invalidate(app, m_sessionUser);
        if (!m_store.removeApplication(m_sessionUser, app, &error)) { qCritical().noquote() << error; _exit(1); }
    }
    PermissionIdentity id; id.app = app; id.user = m_sessionUser;
    if (event != "ApplicationLaunched") changed(id, "", "not-determined");
    qInfo().noquote() << "permissiond: lifecycle" << event << app << instance;
}
