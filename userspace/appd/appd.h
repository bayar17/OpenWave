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
    char *manifest;
    char *bundle;
    char *digest;
    char **permissions;
    char **argv;
    gboolean paths;
    gboolean single;
    gboolean removing;
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
    uid_t uid;
    gid_t render_gid;
    char *runtime;
    char *data;
    gboolean storage_changed;
    gboolean namespace_sandboxed;
    int namespace_fds[4];
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
    char *state_dir;
    char *launcher_exe;
    char *cgroup_root;
    guint shutdown_ms;
    guint pids_max;
    gboolean ready;
    gboolean quitting;
    GPtrArray *monitors;
    guint scan_timer;
    guint registration;
    guint owner;
    guint subscription;
    guint services_registration;
    char *permission_peer;
    char *permission_correlation;
    guint64 services_sequence;
    GHashTable *pending;
    char *session;
    char *public_preferences;
};

G_BEGIN_DECLS

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
Instance *appd_caller(Manager *manager, guint uid, guint pid);
GVariant *appd_identity(Manager *manager, Instance *instance, guint pid);
gboolean appd_trusted_app(App *app);
void appd_revoke(Manager *manager, App *app);

gboolean appd_prepare_isolation(Manager *manager, Instance *instance, GError **error);
int appd_enforce_isolation(Instance *instance);
void appd_release_isolation(Instance *instance);
gboolean appd_publish_preferences(gpointer manager);
gboolean appd_publish_sockets(Manager *manager);

App *appd_read_bundle(const char *directory);
char *appd_create_webapp(Manager *manager, Instance *caller, guint pid,
    const char *name, const char *url, const guint8 *icon, gsize icon_size, GError **error);
gboolean appd_update_webapp_icon(Manager *manager, Instance *caller, guint pid, const guint8 *icon, gsize icon_size, GError **error);
gboolean appd_validate_bundle(App *app, GError **error);
gboolean appd_is_webapp(App *app);
gboolean appd_webapp_pending(App *app);
void appd_uninstall_webapp(Manager *manager, Instance *caller, guint pid, const char *id, GDBusMethodInvocation *call);
void appd_recover_uninstalls(Manager *manager);

const char *appd_legacy_id(App *app);
gboolean appd_prepare_storage(Manager *manager, Instance *instance, GError **error);
gboolean appd_prepare_namespace(Instance *instance, GError **error);
int appd_enter_namespace(Instance *instance);
void appd_close_namespace(Instance *instance);
gboolean appd_register_services(Manager *manager, GError **error);
void appd_services_event(Manager *manager, const char *event, App *app, Instance *instance);
int appd_install_seccomp(void);

G_END_DECLS
#endif
