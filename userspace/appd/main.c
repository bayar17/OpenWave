#define _GNU_SOURCE
#include "appd.h"
#include <glib-unix.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

static gboolean stop(gpointer data)
{
    Manager *m = data;
    m->quitting = TRUE; m->ready = FALSE; appd_stop_all(m);
    if (!m->instances->len) g_main_loop_quit(m->loop);
    return G_SOURCE_REMOVE;
}
static void closed(GDBusConnection *bus, gboolean vanished, GError *error, gpointer data)
{
    (void)bus; (void)vanished; (void)error; stop(data);
}
static gboolean clean_empty_cgroups(gpointer data)
{
    Manager *m = data;
    if (m->cgroup_root) {
        GDir *directory = g_dir_open(m->cgroup_root, 0, NULL);
        const char *name;
        while (directory && (name = g_dir_read_name(directory))) {
            if (!g_str_has_prefix(name, "app-")) continue;
            char *path = g_build_filename(m->cgroup_root, name, NULL);
            rmdir(path); g_free(path);
        }
        if (directory) g_dir_close(directory);
    }
    return G_SOURCE_CONTINUE;
}
int main(void)
{
    Manager m = {0}; GError *error = NULL;
    if (!appd_configuration(&m, "/etc/openwave/appd.conf", &error)) goto failure;
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0) { g_printerr("appd: cannot become a child subreaper\n"); return 1; }
    m.loop = g_main_loop_new(NULL, FALSE); m.instances = g_ptr_array_new();
    if (!appd_watch_children(&m, &error)) goto failure;
    m.bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!m.bus) goto failure;
    g_dbus_connection_set_exit_on_close(m.bus, FALSE);
    g_signal_connect(m.bus, "closed", G_CALLBACK(closed), &m);
    appd_registry_scan(&m);
    if (!appd_register_bus(&m, &error)) goto failure;
    appd_recover(&m); appd_registry_watch(&m);
    char *ready = g_build_filename(m.state_dir, "ready", NULL);
    char *pid = g_strdup_printf("%d\n", getpid());
    if (!g_file_set_contents(ready, pid, -1, &error)) goto failure;
    g_free(pid);
    g_unix_signal_add(SIGTERM, stop, &m); g_unix_signal_add(SIGINT, stop, &m);
    g_timeout_add(250, clean_empty_cgroups, &m);
    g_print("appd: ready, %u applications, execution user %s (uid %u)\n", g_hash_table_size(m.apps), m.user, m.uid);
    if (m.uid == 0) g_warning("appd: explicit legacy root session; non-root migration is not complete");
    g_main_loop_run(m.loop);
    unlink(ready); g_free(ready);
    return 0;
failure:
    g_printerr("appd: %s\n", error ? error->message : "initialization failed");
    g_clear_error(&error); return 1;
}
