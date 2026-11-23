#ifndef APPD_DESKTOP_H
#define APPD_DESKTOP_H

#include <gio/gio.h>
#include <sys/types.h>

G_BEGIN_DECLS

#define DESKTOP_MAX_BYTES 65536
#define DESKTOP_MAX_ENTRIES 4096
#define DESKTOP_MAX_DEPTH 6
#define DESKTOP_MAX_ACTIONS 16
#define DESKTOP_MAX_ARGV 256

typedef struct {
    char *id;
    char *name;
    char *exec;
} DesktopAction;

typedef struct {
    char *id;
    char *path;
    char *source_dir;
    char *name;
    char *generic_name;
    char *comment;
    char *icon;
    char *exec;
    char *try_exec;
    char *app_id;
    char *package;
    char **categories;
    char **mime_types;
    char **only_show_in;
    char **not_show_in;
    GPtrArray *actions;
    gboolean hidden;
    gboolean no_display;
    gboolean terminal;
    gboolean dbus_activatable;
    gboolean accepts_paths;
    gboolean trusted_source;
    gboolean user_source;
} DesktopEntry;

typedef struct {
    char *home;
    char *data_home;
    char *config_home;
    char **data_dirs;
    char **config_dirs;
    char *desktop;
    char *icon_theme;
    char *language;
    uid_t uid;
    gid_t gid;
} DesktopPaths;

typedef struct {
    const char *const *files;
    const char *const *uris;
    const char *icon;
    const char *name;
    const char *desktop_path;
} DesktopExecContext;

DesktopPaths *desktop_paths_new(const char *home, uid_t uid, gid_t gid, const char *desktop,
    const char *data_dirs, const char *config_dirs, const char *language);
void desktop_paths_free(DesktopPaths *paths);

DesktopEntry *desktop_entry_parse(const char *path, const char *id, const char *source_dir,
    const DesktopPaths *paths, GError **error);
void desktop_entry_free(DesktopEntry *entry);
char *desktop_id_from_path(const char *base, const char *path);
gboolean desktop_id_valid(const char *id);
GPtrArray *desktop_scan(const DesktopPaths *paths);
gboolean desktop_entry_shown(const DesktopEntry *entry, const char *desktop);
gboolean desktop_entry_try_exec_ok(const DesktopEntry *entry);
gboolean desktop_entry_has_mime(const DesktopEntry *entry, const char *mime);
char **desktop_entry_schemes(const DesktopEntry *entry);

char **desktop_exec_tokens(const char *exec, GError **error);
char **desktop_exec_expand(const DesktopEntry *entry, const char *exec, const DesktopExecContext *context, GError **error);
gboolean desktop_exec_valid(const char *exec, GError **error);

char *desktop_resolve_icon(const DesktopPaths *paths, const char *icon, int size);

void desktop_trust_configure(uid_t uid, const char *stop_directory);
gboolean desktop_file_trusted(const char *path);

G_END_DECLS
#endif
