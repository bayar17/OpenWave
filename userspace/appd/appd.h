#ifndef APPD_H
#define APPD_H

#include <gio/gio.h>
#include <sys/types.h>

#define APPD_NAME "org.openwave.Applications"
#define APPD_PATH "/org/openwave/Applications"
#define APPD_INTERFACE "org.openwave.Applications1"

typedef struct App App;
typedef struct Instance Instance;
typedef struct Manager Manager;

struct App {
    char *id;
    char *name;
    char *icon;
    char *desktop;
    char **argv;
    gboolean paths;
    gboolean single;
    char *state;
    char *error;
};

struct Instance {
    Manager *manager;
    App *app;
    GPid pid;
    guint deadline;
    gboolean stopping;
    guint64 start_time;
    char *cgroup;
};

struct Manager {
    GDBusConnection *bus;
    GMainLoop *loop;
    GHashTable *apps;
    GPtrArray *instances;
    char *renderer;
    guint renderer_pid;
    uid_t uid;
    gid_t gid;
    char *user;
    char *home;
    char *runtime;
    char *desktop_dir;
    char *state_dir;
    char *launcher_exe;
    char *cgroup_root;
    guint shutdown_ms;
    gboolean ready;
    gboolean quitting;
    GFileMonitor *monitor;
    guint scan_timer;
    guint registration;
    guint owner;
    guint subscription;
    GHashTable *pending;
};

gboolean appd_configuration(Manager *manager, const char *path, GError **error);
gboolean appd_verified_file(const char *path, gboolean executable, GError **error);
void appd_registry_scan(Manager *manager);
void appd_registry_watch(Manager *manager);
GVariant *appd_info(Manager *manager, App *app, Instance *instance);
Instance *appd_instance(Manager *manager, App *app);
void appd_emit(Manager *manager, const char *signal, GVariant *parameters);
void appd_state(Manager *manager, App *app, const char *state, const char *error);
gboolean appd_arguments(App *app, char **arguments, GError **error);
gboolean appd_launch(Manager *manager, App *app, char **arguments, GError **error);
void appd_activate(Manager *manager, Instance *instance, char **arguments);
void appd_terminate(Manager *manager, App *app);
void appd_renderer_lost(Manager *manager);
void appd_stop_all(Manager *manager);
void appd_recover(Manager *manager);
gboolean appd_watch_children(Manager *manager, GError **error);
gboolean appd_register_bus(Manager *manager, GError **error);
gboolean appd_credentials(Manager *manager, const char *sender, guint *uid, guint *pid);

#endif
