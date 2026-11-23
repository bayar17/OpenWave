#define _GNU_SOURCE
#include "appd.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define LAUNCHER "/System/Services/openwave-launch"
#define LINUX_PREFIX "org.openwave.linux."

#ifndef APPD_SYSTEM_APPLICATIONS
#define APPD_SYSTEM_APPLICATIONS "/System/Applications"
#endif
#ifndef APPD_USER_APPLICATIONS
#define APPD_USER_APPLICATIONS "/Applications"
#endif

#ifndef APPD_ANDROID_ROOT
#define APPD_ANDROID_ROOT "/var/lib/openwave/app-identities/android"
#endif

#ifndef APPD_DATA_DIRS
#define APPD_DATA_DIRS "/usr/local/share:/usr/share"
#endif

static const char *const roots[] = {APPD_SYSTEM_APPLICATIONS, APPD_USER_APPLICATIONS, NULL};

static char *language_of(Manager *m)
{
    return g_strdup(m->desktop && m->desktop->language ? m->desktop->language : "C");
}

static gint compare_names(gconstpointer a, gconstpointer b)
{
    return g_strcmp0(*(char *const *)a, *(char *const *)b);
}

void appd_desktop_prepare(Manager *m)
{
    GString *dirs = g_string_new(APPD_DATA_DIRS);
    for (guint i = 0; roots[i]; ++i) {
        GDir *directory = g_dir_open(roots[i], 0, NULL);
        GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
        const char *name;
        while (directory && (name = g_dir_read_name(directory)))
            if (name[0] != '.') g_ptr_array_add(names, g_strdup(name));
        if (directory) g_dir_close(directory);
        g_ptr_array_sort(names, compare_names);
        for (guint n = 0; n < names->len; ++n) {
            char *share = g_build_filename(roots[i], g_ptr_array_index(names, n), "share", NULL);
            if (g_file_test(share, G_FILE_TEST_IS_DIR)) g_string_append_printf(dirs, ":%s", share);
            g_free(share);
        }
        g_ptr_array_unref(names);
    }
    char *android_share = g_build_filename(APPD_ANDROID_ROOT, "share", NULL);
    if (g_file_test(android_share, G_FILE_TEST_IS_DIR)) g_string_append_printf(dirs, ":%s", android_share);
    g_free(android_share);
    char *language = language_of(m);
    DesktopPaths *fresh = desktop_paths_new(m->home, m->uid, m->gid, "OpenWave", dirs->str, NULL, language);
    g_free(language);
    g_string_free(dirs, TRUE);
    desktop_paths_free(m->desktop);
    m->desktop = fresh;
}

static gboolean bundle_local(const DesktopEntry *entry, char **name)
{
    char *canonical = entry->path ? realpath(entry->path, NULL) : NULL;
    gboolean local = FALSE;
    for (guint i = 0; canonical && !local && roots[i]; ++i) {
        char *root = realpath(roots[i], NULL);
        char *prefix = root ? g_strconcat(root, "/", NULL) : NULL;
        if (prefix && g_str_has_prefix(canonical, prefix)) {
            const char *rest = canonical + strlen(prefix);
            const char *slash = strchr(rest, '/');
            if (slash && slash != rest) {
                *name = g_strndup(rest, slash - rest);
                local = TRUE;
            }
        }
        g_free(prefix);
        free(root);
    }
    free(canonical);
    return local;
}

static gboolean launcher_exec(const DesktopEntry *entry)
{
    char **words = entry->exec ? desktop_exec_tokens(entry->exec, NULL) : NULL;
    gboolean okay = words && g_strv_length(words) >= 2 && !strcmp(words[0], LAUNCHER) && !strcmp(words[1], entry->app_id);
    g_strfreev(words);
    return okay;
}

static char **merge_lists(char **first, char **second, gboolean schemes)
{
    GPtrArray *out = g_ptr_array_new();
    for (int pass = 0; pass < 2; ++pass)
        for (char **item = pass ? second : first; item && *item; ++item) {
            gboolean scheme = g_str_has_prefix(*item, "x-scheme-handler/");
            if (scheme != schemes) continue;
            const char *value = schemes ? *item + 17 : *item;
            if (!*value) continue;
            gboolean seen = FALSE;
            for (guint i = 0; i < out->len; ++i) seen |= !strcmp(g_ptr_array_index(out, i), value);
            if (!seen) g_ptr_array_add(out, g_strdup(value));
        }
    g_ptr_array_add(out, NULL);
    return (char **)g_ptr_array_free(out, FALSE);
}

static void seal(App *app)
{
    GChecksum *sum = g_checksum_new(G_CHECKSUM_SHA256);
    const char *parts[] = {app->name, app->comment, app->generic_name, app->icon, app->desktop_id,
        app->visible ? "1" : "0", app->linux_app ? "L" : "N"};
    for (guint i = 0; i < G_N_ELEMENTS(parts); ++i) {
        if (parts[i]) g_checksum_update(sum, (const guchar *)parts[i], -1);
        g_checksum_update(sum, (const guchar *)"\n", 1);
    }
    char **lists[] = {app->categories, app->mime_types, app->schemes, app->opens};
    for (guint l = 0; l < G_N_ELEMENTS(lists); ++l) {
        for (char **item = lists[l]; item && *item; ++item) {
            g_checksum_update(sum, (const guchar *)*item, -1);
            g_checksum_update(sum, (const guchar *)";", 1);
        }
        g_checksum_update(sum, (const guchar *)"|", 1);
    }
    g_free(app->presentation);
    app->presentation = g_strdup(g_checksum_get_string(sum));
    g_checksum_free(sum);
}

static void apply_entry(Manager *m, App *app, DesktopEntry *entry)
{
    g_free(app->desktop_id); app->desktop_id = g_strdup(entry->id);
    if (entry->name && *entry->name) { g_free(app->name); app->name = g_strdup(entry->name); }
    g_free(app->comment); app->comment = g_strdup(entry->comment ? entry->comment : "");
    g_free(app->generic_name); app->generic_name = g_strdup(entry->generic_name ? entry->generic_name : "");
    g_strfreev(app->categories); app->categories = g_strdupv(entry->categories);
    char *icon = desktop_resolve_icon(m->desktop, entry->icon, 64);
    if (icon) {
        char *uri = g_filename_to_uri(icon, NULL, NULL);
        if (uri) { g_free(app->icon); app->icon = uri; }
        g_free(icon);
    }
    char **types = merge_lists(app->mime_types, entry->mime_types, FALSE);
    g_strfreev(app->mime_types); app->mime_types = types;
    char **declared = merge_lists(NULL, entry->mime_types, TRUE);
    GPtrArray *schemes = g_ptr_array_new();
    for (int pass = 0; pass < 2; ++pass)
        for (char **item = pass ? declared : app->schemes; item && *item; ++item) {
            gboolean seen = FALSE;
            for (guint i = 0; i < schemes->len; ++i) seen |= !strcmp(g_ptr_array_index(schemes, i), *item);
            if (!seen) g_ptr_array_add(schemes, g_strdup(*item));
        }
    g_ptr_array_add(schemes, NULL);
    g_strfreev(declared);
    g_strfreev(app->schemes);
    app->schemes = (char **)g_ptr_array_free(schemes, FALSE);
    app->visible = desktop_entry_shown(entry, m->desktop->desktop) && desktop_entry_try_exec_ok(entry);
    seal(app);
}

static App *linux_app(Manager *m, DesktopEntry *entry)
{
    char *base = g_strndup(entry->id, strlen(entry->id) - strlen(".desktop"));
    for (char *c = base; *c; ++c) if (!g_ascii_isalnum(*c) && !strchr("_.-", *c)) *c = '-';
    char *id = g_strconcat(LINUX_PREFIX, base, NULL);
    g_free(base);
    if (strlen(id) > 128) id[128] = 0;
    App *app = g_new0(App, 1);
    app->id = id;
    app->name = g_strdup(entry->name ? entry->name : id);
    app->icon = g_strdup("");
    app->manifest = g_strdup("");
    app->bundle = g_strdup("");
    app->permissions = g_new0(char *, 1);
    app->opens = g_new0(char *, 1);
    app->mime_types = g_new0(char *, 1);
    app->argv = g_new0(char *, 2);
    app->argv[0] = g_strdup("");
    app->state = g_strdup("Stopped");
    app->error = g_strdup("");
    app->single = TRUE;
    app->linux_app = TRUE;
    app->digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, entry->path, -1);
    apply_entry(m, app, entry);
    app->visible = FALSE;
    seal(app);
    return app;
}

static char *bundle_directory(const char *id)
{
    for (guint i = 0; roots[i]; ++i) {
        char *path = g_build_filename(roots[i], id, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_DIR)) return path;
        g_free(path);
    }
    return NULL;
}

void appd_desktop_scan(Manager *m, GHashTable *candidates)
{
    appd_desktop_prepare(m);
    GPtrArray *entries = desktop_scan(m->desktop);
    if (m->desktop_entries) g_ptr_array_unref(m->desktop_entries);
    m->desktop_entries = entries;
    if (m->desktop_ids) g_hash_table_unref(m->desktop_ids);
    m->desktop_ids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    GHashTable *bound = g_hash_table_new(g_str_hash, g_str_equal);
    for (int pass = 0; pass < 2; ++pass)
        for (guint i = 0; i < entries->len; ++i) {
            DesktopEntry *entry = g_ptr_array_index(entries, i);
            char *owner = NULL;
            gboolean local = bundle_local(entry, &owner);
            if (local && entry->app_id && strcmp(owner, entry->app_id)) {
                g_warning("appd: desktop entry %s inside bundle %s claims another application", entry->path, owner);
                g_free(owner); continue;
            }
            g_free(owner);
            if (entry->package) continue;
            if (entry->app_id && !launcher_exec(entry)) {
                g_warning("appd: desktop entry %s does not use the trusted launcher; ignoring its identity", entry->path);
                g_clear_pointer(&entry->app_id, g_free);
            }
            if (!entry->app_id) {
                if (pass == 1 && !g_str_has_prefix(entry->id, "waydroid.") && !entry->hidden) {
                    App *app = linux_app(m, entry);
                    if (g_hash_table_contains(candidates, app->id)) appd_app_free(app);
                    else {
                        g_hash_table_insert(candidates, g_strdup(app->id), app);
                        g_hash_table_insert(m->desktop_ids, g_strdup(entry->id), g_strdup(app->id));
                    }
                }
                continue;
            }
            gboolean prefer = local;
            if (pass != (prefer ? 0 : 1)) continue;
            if (g_hash_table_contains(bound, entry->app_id)) continue;
            char *directory = bundle_directory(entry->app_id);
            App *app = directory ? appd_read_bundle(directory) : NULL;
            g_free(directory);
            if (!app || strcmp(app->id, entry->app_id)) {
                if (app) appd_app_free(app);
                continue;
            }
            g_hash_table_add(bound, entry->app_id);
            apply_entry(m, app, entry);
            g_hash_table_insert(candidates, g_strdup(app->id), app);
            g_hash_table_insert(m->desktop_ids, g_strdup(entry->id), g_strdup(app->id));
        }
    g_hash_table_unref(bound);
}

static char **android_categories(App *app);

void appd_desktop_attach_android(Manager *m)
{
    if (!m->android_apps || !m->desktop_ids) return;
    GHashTableIter iter; gpointer value;
    g_hash_table_iter_init(&iter, m->android_apps);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        App *app = value;
        g_free(app->desktop_id);
        app->desktop_id = g_strdup_printf("waydroid.%s.desktop", app->package);
        g_hash_table_replace(m->desktop_ids, g_strdup(app->desktop_id), g_strdup(app->id));
        g_strfreev(app->categories);
        app->categories = android_categories(app);
        app->visible = TRUE;
    }
}

App *appd_app_for_desktop_id(Manager *m, const char *desktop_id)
{
    if (!m->desktop_ids) return NULL;
    const char *id = g_hash_table_lookup(m->desktop_ids, desktop_id);
    return id ? appd_lookup(m, id) : NULL;
}

const char *appd_app_kind(App *app)
{
    if (app->android) return "android";
    if (app->linux_app) return "linux";
    return "native";
}

static int open_checked(int parent, const char *name, gboolean create)
{
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0 && errno == ENOENT && create && !mkdirat(parent, name, 0755))
        fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || st.st_uid != geteuid() || (st.st_mode & 0022)) {
        if (fd >= 0) close(fd);
        return -1;
    }
    return fd;
}

static char *entry_text(App *app, const char *bundle)
{
    GString *text = g_string_new("[Desktop Entry]\nType=Application\nVersion=1.0\nName=");
    for (const char *c = app->name; *c; ++c) {
        if ((unsigned char)*c < 32 || *c == 127) g_string_append_c(text, ' ');
        else if (*c == '\\') g_string_append(text, "\\\\");
        else g_string_append_c(text, *c);
    }
    gboolean files = app->paths && ((app->mime_types && *app->mime_types) || (app->opens && *app->opens));
    const char *suffix = files ? " %F" : (app->schemes && *app->schemes) ? " %U" : app->paths ? " %F" : "";
    g_string_append_printf(text, "\nExec=%s %s%s\n", LAUNCHER, app->id, suffix);
    char *icon = app->icon && g_str_has_prefix(app->icon, "file://") ? g_filename_from_uri(app->icon, NULL, NULL) : NULL;
    if (icon && g_str_has_prefix(icon, bundle)) g_string_append_printf(text, "Icon=%s\n", icon);
    g_free(icon);
    g_string_append(text, "Terminal=false\n");
    if ((app->mime_types && *app->mime_types) || (app->schemes && *app->schemes)) {
        g_string_append(text, "MimeType=");
        for (char **type = app->mime_types; type && *type; ++type) g_string_append_printf(text, "%s;", *type);
        for (char **scheme = app->schemes; scheme && *scheme; ++scheme) g_string_append_printf(text, "x-scheme-handler/%s;", *scheme);
        g_string_append_c(text, '\n');
    }
    g_string_append_printf(text, "X-OpenWave-AppId=%s\n", app->id);
    if (app->paths) g_string_append(text, "X-OpenWave-AcceptsPaths=true\n");
    return g_string_free(text, FALSE);
}

static gboolean write_entry(App *app, const char *bundle)
{
    int root = open(bundle, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    struct stat st;
    if (root < 0 || fstat(root, &st) || st.st_uid != geteuid() || (st.st_mode & 0022)) { if (root >= 0) close(root); return FALSE; }
    int shared = open_checked(root, "share", TRUE);
    close(root);
    if (shared < 0) return FALSE;
    int applications = open_checked(shared, "applications", TRUE);
    close(shared);
    if (applications < 0) return FALSE;
    char *name = g_strconcat(app->id, ".desktop", NULL), *temporary = g_strconcat(".", app->id, ".desktop.tmp", NULL);
    char *text = entry_text(app, bundle);
    int fd = openat(applications, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    gsize length = strlen(text), written = 0;
    gboolean okay = fd >= 0;
    while (okay && written < length) {
        ssize_t count = write(fd, text + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) okay = FALSE; else written += count;
    }
    okay = okay && fsync(fd) == 0 && renameat(applications, temporary, applications, name) == 0;
    if (fd >= 0) close(fd);
    if (!okay) unlinkat(applications, temporary, 0);
    else fsync(applications);
    close(applications);
    g_free(name); g_free(temporary); g_free(text);
    return okay;
}

void appd_desktop_migrate(Manager *m)
{
    (void)m;
    if (geteuid()) return;
    GDir *directory = g_dir_open(APPD_USER_APPLICATIONS, 0, NULL);
    const char *name;
    while (directory && (name = g_dir_read_name(directory))) {
        if (name[0] == '.') continue;
        char *bundle = g_build_filename(APPD_USER_APPLICATIONS, name, NULL);
        char *file = g_strconcat(name, ".desktop", NULL);
        char *entry = g_build_filename(bundle, "share", "applications", file, NULL);
        if (!g_file_test(entry, G_FILE_TEST_EXISTS)) {
            App *app = appd_read_bundle(bundle);
            if (app && !strcmp(app->id, name)) {
                if (write_entry(app, bundle)) g_message("appd: generated the desktop entry of %s", name);
                else g_warning("appd: could not generate the desktop entry of %s", name);
            }
            if (app) appd_app_free(app);
        }
        g_free(bundle); g_free(file); g_free(entry);
    }
    if (directory) g_dir_close(directory);
}

static gboolean directory_sound(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0022);
}

static gboolean android_directories(char **applications, char **icons)
{
    char *share = g_build_filename(APPD_ANDROID_ROOT, "share", NULL);
    *applications = g_build_filename(share, "applications", NULL);
    *icons = g_build_filename(share, "icons", NULL);
    gboolean okay = g_mkdir_with_parents(*applications, 0755) == 0 && g_mkdir_with_parents(*icons, 0755) == 0
        && directory_sound(APPD_ANDROID_ROOT) && directory_sound(share) && directory_sound(*applications) && directory_sound(*icons);
    g_free(share);
    if (!okay) { g_clear_pointer(applications, g_free); g_clear_pointer(icons, g_free); }
    return okay;
}

static gboolean write_atomic(const char *directory, const char *name, const guchar *data, gsize length)
{
    char *path = g_build_filename(directory, name, NULL), *temporary = g_strconcat(directory, "/.", name, ".tmp", NULL);
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    gsize written = 0;
    gboolean okay = fd >= 0;
    while (okay && written < length) {
        ssize_t count = write(fd, data + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) okay = FALSE; else written += count;
    }
    okay = okay && fsync(fd) == 0 && rename(temporary, path) == 0;
    if (fd >= 0) close(fd);
    if (!okay) unlink(temporary);
    g_free(path); g_free(temporary);
    return okay;
}

static gboolean file_equals(const char *path, const guchar *data, gsize length)
{
    gchar *contents = NULL; gsize size = 0;
    gboolean same = g_file_get_contents(path, &contents, &size, NULL) && size == length && !memcmp(contents, data, length);
    g_free(contents);
    return same;
}

static gboolean has_scheme(App *app, const char *scheme)
{
    for (char **item = app->schemes; item && *item; ++item)
        if (!strcmp(*item, scheme)) return TRUE;
    return FALSE;
}

static char **android_categories(App *app)
{
    GPtrArray *list = g_ptr_array_new();
    g_ptr_array_add(list, g_strdup("X-WayDroid-App"));
    if (has_scheme(app, "http") || has_scheme(app, "https")) {
        g_ptr_array_add(list, g_strdup("Network"));
        g_ptr_array_add(list, g_strdup("WebBrowser"));
    }
    if (has_scheme(app, "mailto")) {
        g_ptr_array_add(list, g_strdup("Network"));
        g_ptr_array_add(list, g_strdup("Email"));
    }
    if (has_scheme(app, "tel") || has_scheme(app, "sms")) g_ptr_array_add(list, g_strdup("Telephony"));
    if (has_scheme(app, "sms")) g_ptr_array_add(list, g_strdup("InstantMessaging"));
    GPtrArray *unique = g_ptr_array_new();
    for (guint i = 0; i < list->len; ++i) {
        gboolean seen = FALSE;
        for (guint j = 0; j < unique->len; ++j) seen |= !strcmp(g_ptr_array_index(unique, j), g_ptr_array_index(list, i));
        if (seen) g_free(g_ptr_array_index(list, i)); else g_ptr_array_add(unique, g_ptr_array_index(list, i));
    }
    g_ptr_array_add(unique, NULL);
    g_ptr_array_free(list, TRUE);
    return (char **)g_ptr_array_free(unique, FALSE);
}

static char *android_entry_text(App *app, const char *icon_path)
{
    GString *text = g_string_new("[Desktop Entry]\nType=Application\nVersion=1.0\nName=");
    for (const char *c = app->name; *c; ++c) {
        if ((unsigned char)*c < 32 || *c == 127) g_string_append_c(text, ' ');
        else if (*c == '\\') g_string_append(text, "\\\\");
        else g_string_append_c(text, *c);
    }
    g_string_append_printf(text, "\nExec=waydroid app launch %s\nTerminal=false\nCategories=", app->package);
    char **categories = android_categories(app);
    for (char **category = categories; *category; ++category) g_string_append_printf(text, "%s;", *category);
    g_strfreev(categories);
    g_string_append_c(text, '\n');
    if (icon_path) g_string_append_printf(text, "Icon=%s\n", icon_path);
    if ((app->mime_types && *app->mime_types) || (app->schemes && *app->schemes)) {
        g_string_append(text, "MimeType=");
        for (char **type = app->mime_types; type && *type; ++type) g_string_append_printf(text, "%s;", *type);
        for (char **scheme = app->schemes; scheme && *scheme; ++scheme) g_string_append_printf(text, "x-scheme-handler/%s;", *scheme);
        g_string_append_c(text, '\n');
    }
    g_string_append_printf(text, "X-OpenWave-AndroidPackage=%s\n", app->package);
    return g_string_free(text, FALSE);
}

void appd_desktop_sync_android(Manager *m)
{
    if (!m->android_apps || !g_hash_table_size(m->android_apps)) return;
    char *applications = NULL, *icons = NULL;
    if (!android_directories(&applications, &icons)) { g_warning("appd: the Android desktop entry directory is unavailable"); return; }
    GHashTable *keep = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GHashTableIter iter; gpointer value;
    g_hash_table_iter_init(&iter, m->android_apps);
    while (g_hash_table_iter_next(&iter, NULL, &value)) {
        App *app = value;
        if (!app->package || !app->name) continue;
        char *entry_name = g_strdup_printf("waydroid.%s.desktop", app->package);
        char *icon_name = g_strdup_printf("waydroid.%s.png", app->package);
        char *icon_path = NULL;
        const char *prefix = "data:image/png;base64,";
        if (app->icon && g_str_has_prefix(app->icon, prefix)) {
            gsize length = 0;
            guchar *png = g_base64_decode(app->icon + strlen(prefix), &length);
            char *target = g_build_filename(icons, icon_name, NULL);
            if (png && length && (file_equals(target, png, length) || write_atomic(icons, icon_name, png, length))) icon_path = g_strdup(target);
            g_free(png); g_free(target);
        }
        char *text = android_entry_text(app, icon_path);
        char *target = g_build_filename(applications, entry_name, NULL);
        if (!file_equals(target, (const guchar *)text, strlen(text)) && !write_atomic(applications, entry_name, (const guchar *)text, strlen(text)))
            g_warning("appd: could not write the desktop entry of %s", app->package);
        g_hash_table_add(keep, g_strdup(entry_name));
        if (icon_path) g_hash_table_add(keep, g_strdup(icon_name));
        g_free(target); g_free(text); g_free(icon_path); g_free(entry_name); g_free(icon_name);
    }
    for (int pass = 0; pass < 2; ++pass) {
        const char *directory = pass ? icons : applications;
        GDir *dir = g_dir_open(directory, 0, NULL);
        const char *name;
        while (dir && (name = g_dir_read_name(dir))) {
            if (g_str_has_prefix(name, "waydroid.") && !g_hash_table_contains(keep, name)) {
                char *stale = g_build_filename(directory, name, NULL);
                unlink(stale);
                g_free(stale);
            }
        }
        if (dir) g_dir_close(dir);
    }
    g_hash_table_unref(keep);
    g_free(applications); g_free(icons);
}
