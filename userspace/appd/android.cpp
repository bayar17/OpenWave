#include "appd.h"
#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <glib-unix.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
constexpr const char *Helper = "/usr/libexec/openwave-waydroid/android-apps";
constexpr gsize MaxOutput = 4 * 1024 * 1024;
constexpr guint HelperTimeoutMs = 30000;
constexpr guint RefreshSeconds = 10;
constexpr int MaxApps = 512;
constexpr int MaxIconBytes = 256 * 1024;
constexpr int MaxEventLine = 16384;
constexpr gint64 StateHoldUs = 6 * G_USEC_PER_SEC;

enum class Operation { List, Launch, Watch, Close };

struct Job {
    Manager *manager = nullptr;
    Operation operation = Operation::List;
    GPid pid = 0;
    int fd = -1;
    guint reader = 0;
    guint timeout = 0;
    QByteArray output;
    bool eof = false, exited = false, overflow = false;
    int status = 0;
    QString appId;
};
}

struct AndroidState {
    QString session;
    Job *list = nullptr;
    bool listAgain = false;
    Job *watch = nullptr;
    Job *close = nullptr;
    QStringList closeQueue;
    GHashTable *launches = nullptr;
};

namespace {
bool validPackage(const QString &package)
{
    static const QRegularExpression pattern(QStringLiteral(
        "\\A[A-Za-z][A-Za-z0-9_]*(?:\\.[A-Za-z][A-Za-z0-9_]*)+\\z"));
    return package.size() <= 128 - int(strlen(APPD_ANDROID_PREFIX)) && pattern.match(package).hasMatch();
}

QString cleanName(const QString &name)
{
    QString clean;
    for (const QChar character : name)
        if (character.isPrint()) clean.append(character);
    return clean.trimmed().left(64);
}

bool plausiblePng(const QByteArray &data)
{
    if (data.size() < 33 || data.size() > MaxIconBytes || !data.startsWith("\x89PNG\r\n\x1a\n")
        || data.mid(12, 4) != "IHDR") return false;
    const auto byte = [&](int i) { return quint32(quint8(data[i])); };
    const quint32 width = byte(16) << 24 | byte(17) << 16 | byte(18) << 8 | byte(19);
    const quint32 height = byte(20) << 24 | byte(21) << 16 | byte(22) << 8 | byte(23);
    return width && height && width <= 512 && height <= 512;
}

bool available(const AndroidState *state)
{
    return state->session == QLatin1String("RUNNING") || state->session == QLatin1String("FROZEN");
}

void freeApp(gpointer data)
{
    App *app = static_cast<App *>(data);
    g_free(app->id); g_free(app->name); g_free(app->icon); g_free(app->manifest);
    g_free(app->bundle); g_free(app->digest); g_strfreev(app->permissions); g_strfreev(app->opens); g_strfreev(app->mime_types); g_free(app->version); g_free(app->developer); g_strfreev(app->argv);
    g_free(app->state); g_free(app->error); g_free(app->package); g_free(app);
}

void childSetup(gpointer)
{
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
}

void finishJob(Job *job);
void startList(Manager *m);
void startClose(Manager *m, const QString &package);
void watchEvent(Job *job, const QByteArray &line);

gboolean readOutput(gint fd, GIOCondition, gpointer data)
{
    Job *job = static_cast<Job *>(data);
    char buffer[16384];
    const ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count > 0 && job->operation == Operation::Watch) {
        job->output.append(buffer, count);
        qsizetype end;
        while ((end = job->output.indexOf('\n')) >= 0) {
            const QByteArray line = job->output.left(end);
            job->output.remove(0, end + 1);
            if (line.size() <= MaxEventLine) watchEvent(job, line);
        }
        if (job->output.size() > MaxEventLine && job->pid > 0 && !job->exited) {
            g_warning("appd: Android event line too long; stopping the watcher");
            job->output.clear();
            kill(job->pid, SIGKILL);
        }
        return G_SOURCE_CONTINUE;
    }
    if (count > 0) {
        if (job->output.size() + count > qsizetype(MaxOutput)) job->overflow = true;
        else job->output.append(buffer, count);
        return G_SOURCE_CONTINUE;
    }
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) return G_SOURCE_CONTINUE;
    job->eof = true; job->reader = 0;
    close(job->fd); job->fd = -1;
    finishJob(job);
    return G_SOURCE_REMOVE;
}

gboolean jobTimeout(gpointer data)
{
    Job *job = static_cast<Job *>(data);
    job->timeout = 0;
    g_warning("appd: Android helper timed out operation=%d pid=%d", int(job->operation), job->pid);
    if (job->pid > 0 && !job->exited) kill(job->pid, SIGKILL);
    return G_SOURCE_REMOVE;
}

bool spawn(Manager *m, Job *job, const QStringList &arguments)
{
    QList<QByteArray> storage{QByteArray(Helper)};
    for (const auto &argument : arguments) storage.append(argument.toUtf8());
    QVector<char *> argv;
    for (auto &value : storage) argv.append(value.data());
    argv.append(nullptr);
    char *envp[] = {const_cast<char *>("PATH=/usr/bin:/bin"), const_cast<char *>("LANG=C.UTF-8"), nullptr};
    GError *error = nullptr;
    int out = -1;
    if (!g_spawn_async_with_pipes("/", argv.data(), envp, GSpawnFlags(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL),
            childSetup, nullptr, &job->pid, nullptr, &out, nullptr, &error)) {
        g_warning("appd: cannot run Android helper: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        return false;
    }
    job->manager = m; job->fd = out;
    g_unix_set_fd_nonblocking(out, TRUE, nullptr);
    job->reader = g_unix_fd_add(out, GIOCondition(G_IO_IN | G_IO_HUP | G_IO_ERR), readOutput, job);
    if (job->operation != Operation::Watch) job->timeout = g_timeout_add(HelperTimeoutMs, jobTimeout, job);
    return true;
}

GVariant *infoFor(Manager *m, App *app)
{
    GVariantBuilder info;
    g_variant_builder_init(&info, G_VARIANT_TYPE("a{sv}"));
    const bool launchable = !app->removing && available(m->android);
    g_variant_builder_add(&info, "{sv}", "launchable", g_variant_new_boolean(launchable));
    g_variant_builder_add(&info, "{sv}", "appId", g_variant_new_string(app->id));
    g_variant_builder_add(&info, "{sv}", "manifestDigest", g_variant_new_string(app->digest));
    g_variant_builder_add(&info, "{sv}", "declaredPermissions", g_variant_new_strv(nullptr, 0));
    g_variant_builder_add(&info, "{sv}", "name", g_variant_new_string(app->name));
    g_variant_builder_add(&info, "{sv}", "icon", g_variant_new_string(app->icon));
    g_variant_builder_add(&info, "{sv}", "bundlePath", g_variant_new_string(""));
    g_variant_builder_add(&info, "{sv}", "isolationProfile", g_variant_new_string("android-container"));
    g_variant_builder_add(&info, "{sv}", "namespaceSandboxed", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&info, "{sv}", "seccompFiltered", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&info, "{sv}", "executable", g_variant_new_string(""));
    g_variant_builder_add(&info, "{sv}", "state", g_variant_new_string(app->state));
    g_variant_builder_add(&info, "{sv}", "error", g_variant_new_string(app->error));
    g_variant_builder_add(&info, "{sv}", "pid", g_variant_new_uint32(0));
    g_variant_builder_add(&info, "{sv}", "trusted", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&info, "{sv}", "canUninstall", g_variant_new_boolean(FALSE));
    g_variant_builder_add(&info, "{sv}", "singleInstance", g_variant_new_boolean(TRUE));
    g_variant_builder_add(&info, "{sv}", "kind", g_variant_new_string("android"));
    g_variant_builder_add(&info, "{sv}", "androidPackage", g_variant_new_string(app->package));
    return g_variant_builder_end(&info);
}

void applyCatalogue(Manager *m, const QByteArray &output)
{
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        g_warning("appd: rejected Android catalogue: not a JSON object");
        return;
    }
    const auto entries = document.object().value(QStringLiteral("apps")).toArray();
    GHashTable *fresh = g_hash_table_new_full(g_str_hash, g_str_equal, nullptr, freeApp);
    for (const auto &value : entries) {
        if (g_hash_table_size(fresh) >= guint(MaxApps)) break;
        const auto entry = value.toObject();
        const QString package = entry.value(QStringLiteral("package")).toString();
        if (!validPackage(package)) continue;
        const QString id = QStringLiteral(APPD_ANDROID_PREFIX) + package;
        const QByteArray idBytes = id.toUtf8();
        if (g_hash_table_contains(fresh, idBytes.constData())) continue;
        QString name = cleanName(entry.value(QStringLiteral("name")).toString());
        if (name.isEmpty()) name = package;
        const QByteArray icon = QByteArray::fromBase64(entry.value(QStringLiteral("icon")).toString().toLatin1(),
                                                        QByteArray::AbortOnBase64DecodingErrors);
        const QByteArray iconUrl = plausiblePng(icon) ? "data:image/png;base64," + icon.toBase64() : QByteArray();
        App *app = g_new0(App, 1);
        app->android = TRUE;
        app->id = g_strdup(idBytes.constData());
        app->package = g_strdup(package.toUtf8().constData());
        app->name = g_strdup(name.toUtf8().constData());
        app->icon = g_strdup(iconUrl.constData());
        app->manifest = g_strdup("");
        app->bundle = g_strdup("");
        app->digest = g_strdup(QCryptographicHash::hash(idBytes + '\0' + name.toUtf8() + '\0' + iconUrl,
                                                       QCryptographicHash::Sha256).toHex().constData());
        app->permissions = g_new0(char *, 1);
        app->argv = g_new0(char *, 2);
        app->argv[0] = g_strdup("");
        app->single = TRUE;
        App *old = static_cast<App *>(g_hash_table_lookup(m->android_apps, app->id));
        app->android_hold = old ? old->android_hold : 0;
        app->state = g_strdup(old ? old->state : "Stopped");
        app->error = g_strdup(old ? old->error : "");
        g_hash_table_insert(fresh, app->id, app);
    }
    bool changed = g_hash_table_size(fresh) != g_hash_table_size(m->android_apps);
    GHashTableIter iterator; gpointer key, value;
    g_hash_table_iter_init(&iterator, fresh);
    while (!changed && g_hash_table_iter_next(&iterator, &key, &value)) {
        App *old = static_cast<App *>(g_hash_table_lookup(m->android_apps, key));
        changed = !old || g_strcmp0(old->digest, static_cast<App *>(value)->digest);
    }
    if (!changed) { g_hash_table_unref(fresh); return; }
    g_hash_table_unref(m->android_apps);
    m->android_apps = fresh;
    g_message("appd: Android catalogue updated, %u applications", g_hash_table_size(fresh));
    appd_emit(m, "RegistryChanged", nullptr);
}

void setSession(Manager *m, const QString &session)
{
    if (m->android->session == session) return;
    const bool before = available(m->android);
    m->android->session = session;
    g_message("appd: Android session %s", session.toUtf8().constData());
    if (before != available(m->android) && g_hash_table_size(m->android_apps))
        appd_emit(m, "RegistryChanged", nullptr);
}

void finishJob(Job *job)
{
    if (!job->eof || !job->exited) return;
    if (job->reader) g_source_remove(job->reader);
    if (job->timeout) g_source_remove(job->timeout);
    Manager *m = job->manager;
    const bool success = !job->overflow && WIFEXITED(job->status) && WEXITSTATUS(job->status) == 0;
    if (job->operation == Operation::Watch) {
        m->android->watch = nullptr;
        g_message("appd: Android event watcher stopped (status %d)", job->status);
    } else if (job->operation == Operation::Close) {
        m->android->close = nullptr;
        g_message("appd: Android close %s result=%s", job->appId.isEmpty() ? "all" : job->appId.toUtf8().constData(),
                  success ? "requested" : "failed");
        if (!m->android->closeQueue.isEmpty()) startClose(m, m->android->closeQueue.takeFirst());
    } else if (job->operation == Operation::List) {
        m->android->list = nullptr;
        if (success) applyCatalogue(m, job->output);
        else g_warning("appd: Android catalogue query failed (status %d)", job->status);
        if (m->android->listAgain) {
            m->android->listAgain = false;
            startList(m);
        }
    } else {
        const QByteArray id = job->appId.toUtf8();
        g_hash_table_remove(m->android->launches, id.constData());
        App *app = static_cast<App *>(g_hash_table_lookup(m->android_apps, id.constData()));
        if (app) {
            if (success) {
                app->android_hold = g_get_monotonic_time() + StateHoldUs;
                appd_state(m, app, "Running", nullptr);
                appd_emit(m, "ActivationRequested", g_variant_new("(su)", app->id, 0u));
            } else appd_state(m, app, "Stopped", "Android could not start this application.");
        }
        g_message("appd: Android launch appId=%s result=%s", id.constData(), success ? "started" : "failed");
    }
    delete job;
}

void startList(Manager *m)
{
    if (m->android->list) { m->android->listAgain = true; return; }
    if (m->quitting) return;
    Job *job = new Job;
    job->operation = Operation::List;
    if (!spawn(m, job, {QStringLiteral("list")})) { delete job; return; }
    m->android->list = job;
}

void startClose(Manager *m, const QString &package)
{
    if (m->quitting) return;
    if (m->android->close) {
        if (!m->android->closeQueue.contains(package)) m->android->closeQueue.append(package);
        return;
    }
    Job *job = new Job;
    job->operation = Operation::Close;
    job->appId = package;
    const QStringList arguments = package.isEmpty() ? QStringList{QStringLiteral("close-all")}
                                                    : QStringList{QStringLiteral("close"), package};
    if (!spawn(m, job, arguments)) { delete job; return; }
    m->android->close = job;
}

struct CloseRequest { QString package; };

void closeAfterSession(Manager *m, const QString &state, gpointer data)
{
    auto *request = static_cast<CloseRequest *>(data);
    if (state == QLatin1String("RUNNING")) startClose(m, request->package);
    delete request;
}

void applyTasks(Manager *m, const QJsonArray &packages)
{
    QSet<QString> running;
    for (const auto &value : packages) {
        const QString package = value.toString();
        if (validPackage(package)) running.insert(package);
    }
    const gint64 now = g_get_monotonic_time();
    guint started = 0, stopped = 0;
    GHashTableIter iterator; gpointer value;
    g_hash_table_iter_init(&iterator, m->android_apps);
    while (g_hash_table_iter_next(&iterator, nullptr, &value)) {
        App *app = static_cast<App *>(value);
        if (app->android_hold > now || !g_strcmp0(app->state, "Starting")) continue;
        const bool listed = running.contains(QString::fromUtf8(app->package));
        if (listed && g_strcmp0(app->state, "Running")) { appd_state(m, app, "Running", nullptr); ++started; }
        else if (!listed && !g_strcmp0(app->state, "Running")) { appd_state(m, app, "Stopped", nullptr); ++stopped; }
    }
    if (started || stopped)
        g_message("appd: Android task list: %d running, %u newly running, %u stopped", int(running.size()), started, stopped);
}

void startWatch(Manager *m)
{
    if (m->android->watch || m->quitting) return;
    Job *job = new Job;
    job->operation = Operation::Watch;
    if (!spawn(m, job, {QStringLiteral("watch")})) { delete job; return; }
    m->android->watch = job;
}

void watchEvent(Job *job, const QByteArray &line)
{
    const auto document = QJsonDocument::fromJson(line);
    if (!document.isObject()) return;
    const QString event = document.object().value(QStringLiteral("event")).toString();
    if (event == QLatin1String("registered")) {
        g_message("appd: Android user monitor registered");
    } else if (event == QLatin1String("error")) {
        const QString message = document.object().value(QStringLiteral("message")).toString().left(300);
        QString printable; for (const QChar ch : message) if (ch.isPrint()) printable.append(ch);
        g_warning("appd: Android event watcher failed: %s", printable.toUtf8().constData());
    } else if (event == QLatin1String("note")) {
        QString printable;
        for (const QChar ch : document.object().value(QStringLiteral("message")).toString().left(200))
            if (ch.isPrint()) printable.append(ch);
        g_message("appd: Android event watcher: %s", printable.toUtf8().constData());
    } else if (event == QLatin1String("tasks")) {
        applyTasks(job->manager, document.object().value(QStringLiteral("packages")).toArray());
    } else if (event == QLatin1String("idle")) {
        Manager *m = job->manager;
        guint stopped = 0;
        GHashTableIter iterator; gpointer value;
        g_hash_table_iter_init(&iterator, m->android_apps);
        while (g_hash_table_iter_next(&iterator, nullptr, &value)) {
            App *app = static_cast<App *>(value);
            if (!g_strcmp0(app->state, "Running") && app->android_hold <= g_get_monotonic_time()) {
                appd_state(m, app, "Stopped", nullptr); ++stopped;
            }
        }
        g_message("appd: Android reported idle; %u application(s) marked stopped", stopped);
    } else if (event == QLatin1String("unlocked") || event == QLatin1String("package")) {
        g_message("appd: Android reported %s; refreshing the catalogue", event.toUtf8().constData());
        startList(job->manager);
    }
}

void startLaunch(Manager *m, App *app)
{
    Job *job = new Job;
    job->operation = Operation::Launch;
    job->appId = QString::fromUtf8(app->id);
    if (!spawn(m, job, {QStringLiteral("launch"), QString::fromUtf8(app->package)})) {
        delete job;
        appd_state(m, app, "Stopped", "The Android application service could not be started.");
        return;
    }
    g_hash_table_insert(m->android->launches, g_strdup(app->id), job);
}

using SessionCallback = void (*)(Manager *, const QString &, gpointer);
struct SessionQuery { Manager *manager; SessionCallback callback; gpointer data; };

void sessionReply(GObject *source, GAsyncResult *result, gpointer data)
{
    auto *query = static_cast<SessionQuery *>(data);
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    QString state = QStringLiteral("UNAVAILABLE");
    if (reply) {
        GVariant *session = nullptr;
        g_variant_get(reply, "(@a{ss})", &session);
        const char *value = nullptr;
        if (g_variant_lookup(session, "state", "&s", &value) && value) state = QString::fromUtf8(value);
        g_variant_unref(session); g_variant_unref(reply);
    }
    g_clear_error(&error);
    if (state != QLatin1String("RUNNING") && state != QLatin1String("FROZEN") && state != QLatin1String("STOPPED"))
        state = QStringLiteral("UNAVAILABLE");
    setSession(query->manager, state);
    query->callback(query->manager, state, query->data);
    delete query;
}

void querySession(Manager *m, SessionCallback callback, gpointer data)
{
    auto *query = new SessionQuery{m, callback, data};
    g_dbus_connection_call(m->bus, "id.waydro.Container", "/ContainerManager", "id.waydro.ContainerManager",
        "GetSession", nullptr, G_VARIANT_TYPE("(a{ss})"), G_DBUS_CALL_FLAGS_NO_AUTO_START, 3000, nullptr,
        sessionReply, query);
}

void refreshAfterSession(Manager *m, const QString &state, gpointer)
{
    if (state == QLatin1String("RUNNING")) {
        startWatch(m);
        if (!m->android->list) startList(m);
    }
}

gboolean refreshTimer(gpointer data)
{
    Manager *m = static_cast<Manager *>(data);
    if (m->bus && !m->quitting) querySession(m, refreshAfterSession, nullptr);
    return G_SOURCE_CONTINUE;
}

struct LaunchRequest { Manager *manager; char *id; };

void unfrozen(GObject *source, GAsyncResult *result, gpointer data)
{
    auto *request = static_cast<LaunchRequest *>(data);
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (reply) g_variant_unref(reply);
    g_clear_error(&error);
    Manager *m = request->manager;
    App *app = static_cast<App *>(g_hash_table_lookup(m->android_apps, request->id));
    if (app) startLaunch(m, app);
    g_free(request->id); delete request;
}

void launchAfterSession(Manager *m, const QString &state, gpointer data)
{
    auto *request = static_cast<LaunchRequest *>(data);
    App *app = static_cast<App *>(g_hash_table_lookup(m->android_apps, request->id));
    if (!app) { g_free(request->id); delete request; return; }
    if (state == QLatin1String("RUNNING")) {
        startLaunch(m, app);
        g_free(request->id); delete request;
    } else if (state == QLatin1String("FROZEN")) {
        g_dbus_connection_call(m->bus, "id.waydro.Container", "/ContainerManager", "id.waydro.ContainerManager",
            "Unfreeze", nullptr, nullptr, G_DBUS_CALL_FLAGS_NO_AUTO_START, 5000, nullptr, unfrozen, request);
    } else {
        appd_state(m, app, "Stopped", "Android is not running.");
        g_free(request->id); delete request;
    }
}
}

extern "C" void appd_android_start(Manager *m)
{
    m->android_apps = g_hash_table_new_full(g_str_hash, g_str_equal, nullptr, freeApp);
    m->android = new AndroidState;
    m->android->session = QStringLiteral("UNAVAILABLE");
    m->android->launches = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, nullptr);
    refreshTimer(m);
    g_timeout_add_seconds(RefreshSeconds, refreshTimer, m);
}

extern "C" App *appd_lookup(Manager *m, const char *id)
{
    if (!id) return nullptr;
    if (g_str_has_prefix(id, APPD_ANDROID_PREFIX))
        return m->android_apps ? static_cast<App *>(g_hash_table_lookup(m->android_apps, id)) : nullptr;
    return static_cast<App *>(g_hash_table_lookup(m->apps, id));
}

extern "C" GVariant *appd_android_info(Manager *m, App *app) { return infoFor(m, app); }

extern "C" gboolean appd_android_launch(Manager *m, App *app, GError **error)
{
    if (!available(m->android)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_CONNECTED, "Android is not running.");
        return FALSE;
    }
    if (g_hash_table_contains(m->android->launches, app->id)) return TRUE;
    appd_state(m, app, "Starting", nullptr);
    querySession(m, launchAfterSession, new LaunchRequest{m, g_strdup(app->id)});
    return TRUE;
}

extern "C" void appd_android_terminate(Manager *m, App *app)
{
    app->android_hold = g_get_monotonic_time() + StateHoldUs;
    appd_state(m, app, "Stopped", nullptr);
    querySession(m, closeAfterSession, new CloseRequest{QString::fromUtf8(app->package)});
}

extern "C" gboolean appd_android_child_exited(Manager *m, GPid pid, int status)
{
    if (!m->android) return FALSE;
    Job *job = nullptr;
    if (m->android->list && m->android->list->pid == pid) job = m->android->list;
    if (!job && m->android->watch && m->android->watch->pid == pid) job = m->android->watch;
    if (!job && m->android->close && m->android->close->pid == pid) job = m->android->close;
    GHashTableIter iterator; gpointer value;
    g_hash_table_iter_init(&iterator, m->android->launches);
    while (!job && g_hash_table_iter_next(&iterator, nullptr, &value))
        if (static_cast<Job *>(value)->pid == pid) job = static_cast<Job *>(value);
    if (!job) return FALSE;
    job->exited = true; job->status = status;
    finishJob(job);
    return TRUE;
}
