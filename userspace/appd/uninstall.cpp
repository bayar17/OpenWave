#include "appd.h"
#include "uninstall-storage.h"
#include "../webapp/WebAppDefinition.h"
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>
#include <cstring>

namespace {
struct Removal {
    Manager *manager;
    App *app;
    GDBusMethodInvocation *call;
    QString id;
    QString data;
    QStringList cgroups;
    gint64 deadline;
};
bool emptyCgroup(const QString &path)
{
    QFile events(path + "/cgroup.events");
    if (!events.open(QIODevice::ReadOnly)) return !QFile::exists(path);
    return events.readAll().split('\n').contains("populated 0");
}
void finished(GObject *, GAsyncResult *result, gpointer data)
{
    auto *removal = static_cast<Removal *>(data);
    GError *error = nullptr;
    const bool okay = g_task_propagate_boolean(G_TASK(result), &error);
    if (removal->app) removal->app->removing = FALSE;
    appd_registry_scan(removal->manager);
    if (removal->call) {
        if (okay) g_dbus_method_invocation_return_value(removal->call, nullptr);
        else g_dbus_method_invocation_return_dbus_error(removal->call, "org.openwave.Applications.Error", error->message);
        g_object_unref(removal->call);
    } else if (!okay) g_warning("appd: %s", error->message);
    g_clear_error(&error);
    delete removal;
}
gboolean waitStopped(gpointer data)
{
    auto *removal = static_cast<Removal *>(data);
    bool stopped = !removal->app || !appd_instance(removal->manager, removal->app);
    for (const auto &group : removal->cgroups) stopped = stopped && emptyCgroup(group);
    if (!stopped && g_get_monotonic_time() < removal->deadline) return G_SOURCE_CONTINUE;
    GTask *task = g_task_new(nullptr, nullptr, finished, removal);
    g_task_set_task_data(task, removal, nullptr);
    if (!stopped) g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_BUSY, "The web app could not be stopped. No data was removed.");
    else g_task_run_in_thread(task, [](GTask *task, gpointer, gpointer data, GCancellable *) {
        auto *removal = static_cast<Removal *>(data);
        GError *error = nullptr;
        if (appd_uninstall_storage("/Applications", removal->data.toUtf8().constData(), removal->id.toUtf8().constData(), &error))
            g_task_return_boolean(task, TRUE);
        else g_task_return_error(task, error);
    });
    g_object_unref(task);
    return G_SOURCE_REMOVE;
}
}
extern "C" gboolean appd_webapp_pending(App *app)
{
    return app && g_str_has_prefix(app->id, "org.openwave.webapp.h")
        && QFile::exists("/Applications/.uninstall-" + QString::fromUtf8(app->id));
}
extern "C" gboolean appd_is_webapp(App *app)
{
    if (!app || strcmp(app->bundle, ("/Applications/" + QString::fromUtf8(app->id)).toUtf8().constData())
        || !appd_validate_bundle(app, nullptr)) return FALSE;
    QFile file(QString::fromUtf8(app->manifest));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return FALSE;
    const auto definition = QJsonDocument::fromJson(file.readAll()).object();
    const auto url = OpenWaveWebApp::address(definition.value("webApp").toObject().value("url").toString());
    return !url.isEmpty() && OpenWaveWebApp::identifier(url) == QString::fromUtf8(app->id)
        && definition.value("executable").toString() == "bin/openwave-webapp";
}
extern "C" void appd_uninstall_webapp(Manager *manager, Instance *caller, guint pid,
    const char *id, GDBusMethodInvocation *call)
{
    const char *failure = nullptr;
    App *app = static_cast<App *>(g_hash_table_lookup(manager->apps, id));
    if (!caller || pid != static_cast<guint>(caller->pid)
        || strcmp(caller->app->id, "com.openwave.settings")
        || strcmp(caller->app->argv[0], "/System/Applications/com.openwave.settings/bin/ocean-settings")
        || !appd_validate_bundle(caller->app, nullptr)) failure = "Only the installed Settings app may uninstall website apps.";
    else if (!app || !appd_is_webapp(app)) failure = "Only an installed website app can be uninstalled.";
    else if (app->removing || manager->quitting) failure = "An uninstall is already in progress or the system is shutting down.";
    if (failure) { g_dbus_method_invocation_return_dbus_error(call, "org.openwave.Applications.Error", failure); return; }
    auto *removal = new Removal{manager, app, static_cast<GDBusMethodInvocation *>(g_object_ref(call)),
        QString::fromUtf8(id), QString::fromUtf8(manager->home) + "/.DataLocation/Applications", {},
        g_get_monotonic_time() + (manager->shutdown_ms + 10000) * 1000LL};
    for (guint i = 0; i < manager->instances->len; ++i) {
        const auto *instance = static_cast<Instance *>(g_ptr_array_index(manager->instances, i));
        if (instance->app == app && instance->cgroup) removal->cgroups.append(QString::fromUtf8(instance->cgroup));
    }
    app->removing = TRUE;
    appd_terminate(manager, app);
    appd_state(manager, app, "Uninstalling", "");
    g_timeout_add(50, waitStopped, removal);
}
extern "C" void appd_recover_uninstalls(Manager *manager)
{
    GDir *directory = g_dir_open("/Applications", 0, nullptr);
    const char *name;
    const QRegularExpression pattern("^\\.uninstall-(org\\.openwave\\.webapp\\.h[0-9a-f]{64})$");
    while (directory && (name = g_dir_read_name(directory))) {
        const auto match = pattern.match(QString::fromUtf8(name));
        if (!match.hasMatch()) continue;
        auto *removal = new Removal{manager, nullptr, nullptr, match.captured(1),
            QString::fromUtf8(manager->home) + "/.DataLocation/Applications", {}, g_get_monotonic_time() + 10000000};
        GDir *groups = manager->cgroup_root ? g_dir_open(manager->cgroup_root, 0, nullptr) : nullptr;
        const char *group;
        while (groups && (group = g_dir_read_name(groups))) {
            if (g_str_has_prefix(group, "app-")) removal->cgroups.append(QString::fromUtf8(manager->cgroup_root) + '/' + group);
        }
        if (groups) g_dir_close(groups);
        g_timeout_add(50, waitStopped, removal);
    }
    if (directory) g_dir_close(directory);
}
