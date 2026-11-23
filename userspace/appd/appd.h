#ifndef APPD_H
#define APPD_H

#include "preferences.h"
#include <gio/gio.h>
#include <sys/types.h>
#include "desktop.h"
#include "mimeapps.h"

#define APPD_NAME "org.openwave.Applications"
#define APPD_PATH "/org/openwave/Applications"
#define APPD_INTERFACE "org.openwave.Applications1"
#ifndef APPD_ANDROID_ROOT
#define APPD_ANDROID_ROOT "/var/lib/openwave/app-identities/android"
#endif
#define APPD_ANDROID_INSTALLER "system.android-installer"
#define APPD_PACKAGE_INSTALLER "system.package-installer"

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
    char **opens;
    char **mime_types;
    char *version;
    char *developer;
    char **argv;
    gboolean paths;
    gboolean single;
    gboolean removing;
    char *state;
    char *error;
    gboolean android;
    char *package;
    gint64 android_hold;
    char *desktop_id;
    char *comment;
    char *generic_name;
    char **categories;
    char **schemes;
    char *presentation;
    gboolean linux_app;
    gboolean visible;
    gboolean removable;
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
    gid_t storage_gid;
    char *runtime;
    char *data;
    gboolean storage_changed;
    gboolean namespace_sandboxed;
    int namespace_fds[4];
    int output_fds[2];
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
    char *details_request;
    gint64 details_time;
    char *public_preferences;
    GHashTable *android_apps;
    struct AndroidState *android;
    DesktopPaths *desktop;
    GPtrArray *desktop_entries;
    GHashTable *desktop_ids;
    char *mimeapps_digest;
    gboolean defaults_ready;
};

G_BEGIN_DECLS

gboolean appd_configuration(Manager *manager, const char *path, GError **error);
gboolean appd_verified_file(const char *path, gboolean executable, GError **error);
void appd_registry_scan(Manager *manager);
void appd_registry_watch(Manager *manager);
GVariant *appd_info(Manager *manager, App *app, Instance *instance);
void appd_info_desktop(GVariantBuilder *info, App *app);
Instance *appd_instance(Manager *manager, App *app);
void appd_emit(Manager *manager, const char *signal, GVariant *parameters);
void appd_state(Manager *manager, App *app, const char *state, const char *error);
gboolean appd_arguments(App *app, char **arguments, GError **error);
gboolean appd_launch(Manager *manager, App *app, char **arguments, GError **error);
char *appd_deliver_file(Instance *instance, const char *path, GError **error);
gboolean appd_launch_file(Manager *manager, App *app, const char *path, GError **error);
void appd_activate(Manager *manager, Instance *instance, char **arguments);
void appd_terminate(Manager *manager, App *app);
void appd_renderer_lost(Manager *manager);
void appd_stop_all(Manager *manager);
void appd_recover(Manager *manager);
gboolean appd_watch_children(Manager *manager, GError **error);
gboolean appd_deploy_child_exited(Manager *manager, GPid pid, int status);
void appd_deploy_peer_gone(Manager *manager, const char *name);
gboolean appd_register_bus(Manager *manager, GError **error);
gboolean appd_credentials(Manager *manager, const char *sender, guint *uid, guint *pid);
Instance *appd_caller(Manager *manager, guint uid, guint pid);
GVariant *appd_identity(Manager *manager, Instance *instance, guint pid);
gboolean appd_trusted_app(App *app);
GVariant *appd_file_associations(Manager *manager);
gboolean appd_set_file_default(Manager *manager, const char *extension, const char *id, GError **error);
char *appd_type_key(const char *type);
char *appd_detect_type(const char *path, GError **error);
gboolean appd_handler_valid(Manager *m, const char *type, const char *id);
GVariant *appd_resolve_file(Manager *m, const char *path, GError **error);
gboolean appd_clear_file_defaults(Manager *m, const char *id, GError **error);
void appd_app_free(gpointer app);
void appd_desktop_scan(Manager *m, GHashTable *candidates);
void appd_desktop_attach_android(Manager *m);
void appd_desktop_sync_android(Manager *m);
App *appd_app_for_desktop_id(Manager *m, const char *desktop_id);
char **appd_mime_parents(const char *type);
GVariant *appd_default_categories(Manager *m);
GVariant *appd_get_default_category(Manager *m, const char *category, GError **error);
gboolean appd_set_default_category(Manager *m, const char *category, const char *id, GError **error);
GVariant *appd_default_file_handler(Manager *m, const char *type, GError **error);
gboolean appd_clear_file_default(Manager *m, const char *type, GError **error);
GVariant *appd_supported_types(Manager *m, const char *id, GError **error);
GVariant *appd_resolve_uri(Manager *m, const char *uri, GError **error);
void appd_defaults_refresh(Manager *m);
void appd_desktop_prepare(Manager *m);
void appd_desktop_migrate(Manager *m);
const char *appd_app_kind(App *app);
void appd_prune_file_defaults(Manager *m);
GVariant *appd_storage_info(Manager *m, App *app);
void appd_revoke(Manager *manager, App *app);

gboolean appd_prepare_isolation(Manager *manager, Instance *instance, GError **error);
int appd_enforce_isolation(Instance *instance);
void appd_release_isolation(Instance *instance);
gboolean appd_publish_preferences(gpointer manager);
gboolean appd_preferences_store(Manager *manager, const AppdPreferenceChange *change, GError **error);
gboolean appd_publish_sockets(Manager *manager);

App *appd_read_bundle(const char *directory);
char *appd_create_webapp(Manager *manager, Instance *caller, guint pid,
    const char *name, const char *url, const guint8 *icon, gsize icon_size, GError **error);
gboolean appd_update_webapp_icon(Manager *manager, Instance *caller, guint pid, const guint8 *icon, gsize icon_size, GError **error);
gboolean appd_validate_bundle(App *app, GError **error);
gboolean appd_is_webapp(App *app);
const char *appd_engine_bundle(App *app);
gboolean appd_webapp_pending(App *app);
gboolean appd_is_managed(App *app);
void appd_uninstall_application(Manager *manager, Instance *caller, guint pid, gboolean from_shell, const char *id, GDBusMethodInvocation *call);
void appd_deploy_remove(Manager *manager, const char *id, GDBusMethodInvocation *call);
void appd_recover_uninstalls(Manager *manager);

const char *appd_legacy_id(App *app);
gboolean appd_prepare_storage(Manager *manager, Instance *instance, GError **error);
gboolean appd_prepare_namespace(Instance *instance, GError **error);
int appd_enter_namespace(Instance *instance);
void appd_close_namespace(Instance *instance);
gboolean appd_register_services(Manager *manager, GError **error);
gboolean appd_register_deploy(Manager *manager, GError **error);
void appd_services_event(Manager *manager, const char *event, App *app, Instance *instance);
int appd_install_seccomp(void);

#define APPD_ANDROID_PREFIX "org.openwave.android."
void appd_android_start(Manager *manager);
App *appd_lookup(Manager *manager, const char *id);
GVariant *appd_android_info(Manager *manager, App *app);
gboolean appd_android_launch(Manager *manager, App *app, GError **error);
gboolean appd_android_launch_uri(Manager *manager, App *app, const char *uri, GError **error);
gboolean appd_android_uninstall(Manager *manager, App *app, GDBusMethodInvocation *call, GError **error);
void appd_android_terminate(Manager *manager, App *app);
gboolean appd_android_child_exited(Manager *manager, GPid pid, int status);

G_END_DECLS
#endif
