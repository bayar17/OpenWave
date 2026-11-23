#define _GNU_SOURCE
#include "desktop.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define GROUP "Desktop Entry"

static uid_t trusted_uid = 0;
static char *trust_stop = NULL;

void desktop_trust_configure(uid_t uid, const char *stop_directory)
{
    trusted_uid = uid;
    g_free(trust_stop);
    trust_stop = stop_directory ? g_strdup(stop_directory) : NULL;
}

static gboolean directory_trusted(const char *directory)
{
    struct stat st;
    return stat(directory, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == trusted_uid && !(st.st_mode & 0022);
}

gboolean desktop_file_trusted(const char *path)
{
    char *canonical = realpath(path, NULL);
    struct stat st;
    gboolean okay = canonical && stat(canonical, &st) == 0 && S_ISREG(st.st_mode)
        && st.st_uid == trusted_uid && !(st.st_mode & 0022);
    char *parent = okay ? g_path_get_dirname(canonical) : NULL;
    while (okay && parent) {
        okay = directory_trusted(parent);
        if (!okay || !strcmp(parent, "/") || (trust_stop && !strcmp(parent, trust_stop))) break;
        char *next = g_path_get_dirname(parent);
        g_free(parent);
        parent = next;
    }
    g_free(parent);
    free(canonical);
    return okay;
}

static void action_free(gpointer data)
{
    DesktopAction *action = data;
    g_free(action->id); g_free(action->name); g_free(action->exec); g_free(action);
}

void desktop_entry_free(DesktopEntry *entry)
{
    if (!entry) return;
    g_free(entry->id); g_free(entry->path); g_free(entry->source_dir); g_free(entry->name);
    g_free(entry->generic_name); g_free(entry->comment); g_free(entry->icon); g_free(entry->exec);
    g_free(entry->try_exec); g_free(entry->app_id); g_free(entry->package);
    g_strfreev(entry->categories); g_strfreev(entry->mime_types);
    g_strfreev(entry->only_show_in); g_strfreev(entry->not_show_in);
    if (entry->actions) g_ptr_array_unref(entry->actions);
    g_free(entry);
}

static char **split_dirs(const char *list, const char *fallback)
{
    char **parts = g_strsplit(list && *list ? list : fallback, ":", -1);
    GPtrArray *clean = g_ptr_array_new();
    for (char **part = parts; *part; ++part) {
        if (**part != '/') continue;
        char *normal = g_canonicalize_filename(*part, NULL);
        gboolean seen = FALSE;
        for (guint i = 0; i < clean->len; ++i) seen |= !strcmp(g_ptr_array_index(clean, i), normal);
        if (seen) g_free(normal); else g_ptr_array_add(clean, normal);
    }
    g_strfreev(parts);
    g_ptr_array_add(clean, NULL);
    return (char **)g_ptr_array_free(clean, FALSE);
}

DesktopPaths *desktop_paths_new(const char *home, uid_t uid, gid_t gid, const char *desktop,
    const char *data_dirs, const char *config_dirs, const char *language)
{
    DesktopPaths *paths = g_new0(DesktopPaths, 1);
    paths->home = g_strdup(home);
    paths->data_home = g_build_filename(home, ".local", "share", NULL);
    paths->config_home = g_build_filename(home, ".config", NULL);
    paths->data_dirs = split_dirs(data_dirs, "/usr/local/share:/usr/share");
    paths->config_dirs = split_dirs(config_dirs, "/etc/xdg");
    paths->desktop = g_strdup(desktop && *desktop ? desktop : "OpenWave");
    paths->icon_theme = g_strdup("hicolor");
    paths->language = g_strdup(language && *language ? language : "C");
    paths->uid = uid;
    paths->gid = gid;
    return paths;
}

void desktop_paths_free(DesktopPaths *paths)
{
    if (!paths) return;
    g_free(paths->home); g_free(paths->data_home); g_free(paths->config_home);
    g_strfreev(paths->data_dirs); g_strfreev(paths->config_dirs);
    g_free(paths->desktop); g_free(paths->icon_theme); g_free(paths->language); g_free(paths);
}

gboolean desktop_id_valid(const char *id)
{
    if (!id || !*id || strlen(id) > 255 || !g_str_has_suffix(id, ".desktop")) return FALSE;
    for (const char *c = id; *c; ++c)
        if (!g_ascii_isalnum(*c) && !strchr("._-+@", *c)) return FALSE;
    return id[0] != '.';
}

char *desktop_id_from_path(const char *base, const char *path)
{
    char *prefix = g_strconcat(base, "/", NULL);
    char *id = NULL;
    if (g_str_has_prefix(path, prefix)) {
        id = g_strdup(path + strlen(prefix));
        for (char *c = id; *c; ++c) if (*c == '/') *c = '-';
    }
    g_free(prefix);
    return id;
}

static gboolean valid_text(const char *text, gsize limit)
{
    return text && strlen(text) <= limit && g_utf8_validate(text, -1, NULL);
}

static char *clean_text(char *text, gsize limit)
{
    if (!text) return NULL;
    GString *clean = g_string_new(NULL);
    if (g_utf8_validate(text, -1, NULL))
        for (const char *c = text; *c && clean->len < limit; c = g_utf8_next_char(c)) {
            gunichar ch = g_utf8_get_char(c);
            if (g_unichar_isprint(ch)) g_string_append_unichar(clean, ch);
        }
    g_free(text);
    char *result = g_string_free(clean, FALSE);
    g_strstrip(result);
    return result;
}

static char **list_of(GKeyFile *file, const char *group, const char *key, guint limit, GRegex *pattern)
{
    gsize count = 0;
    char **values = g_key_file_get_string_list(file, group, key, &count, NULL);
    GPtrArray *kept = g_ptr_array_new();
    for (gsize i = 0; values && i < count && kept->len < limit; ++i) {
        if (!*values[i] || strlen(values[i]) > 255 || !g_utf8_validate(values[i], -1, NULL)) continue;
        if (pattern && !g_regex_match(pattern, values[i], 0, NULL)) continue;
        gboolean seen = FALSE;
        for (guint j = 0; j < kept->len; ++j) seen |= !strcmp(g_ptr_array_index(kept, j), values[i]);
        if (!seen) g_ptr_array_add(kept, g_strdup(values[i]));
    }
    g_strfreev(values);
    g_ptr_array_add(kept, NULL);
    return (char **)g_ptr_array_free(kept, FALSE);
}

static gboolean read_limited(const char *path, gboolean follow, char **contents, gsize *length, GError **error)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | (follow ? 0 : O_NOFOLLOW));
    struct stat st;
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size > DESKTOP_MAX_BYTES) {
        if (fd >= 0) close(fd);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Desktop entry is unreadable, not a regular file, or too large: %s", path);
        return FALSE;
    }
    char *buffer = g_malloc(st.st_size + 1);
    gsize used = 0;
    while (used < (gsize)st.st_size) {
        ssize_t count = read(fd, buffer + used, st.st_size - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        used += count;
    }
    close(fd);
    buffer[used] = 0;
    if (strlen(buffer) != used) {
        g_free(buffer);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Desktop entry contains a NUL byte: %s", path);
        return FALSE;
    }
    *contents = buffer; *length = used;
    return TRUE;
}

static GRegex *mime_pattern(void)
{
    static GRegex *pattern;
    if (!pattern) pattern = g_regex_new("^[a-z0-9][a-z0-9!#$&^_.+-]{0,126}/[a-z0-9*][a-z0-9!#$&^_.+*-]{0,126}$", G_REGEX_OPTIMIZE, 0, NULL);
    return pattern;
}

static GRegex *identifier_pattern(void)
{
    static GRegex *pattern;
    if (!pattern) pattern = g_regex_new("^[a-zA-Z][a-zA-Z0-9_-]*(?:\\.[a-zA-Z0-9][a-zA-Z0-9_-]*){2,}$", G_REGEX_OPTIMIZE, 0, NULL);
    return pattern;
}

static GRegex *package_pattern(void)
{
    static GRegex *pattern;
    if (!pattern) pattern = g_regex_new("^[A-Za-z][A-Za-z0-9_]*(?:\\.[A-Za-z][A-Za-z0-9_]*)+$", G_REGEX_OPTIMIZE, 0, NULL);
    return pattern;
}

static gboolean action_id_valid(const char *id)
{
    if (!id || !*id || strlen(id) > 64) return FALSE;
    for (const char *c = id; *c; ++c) if (!g_ascii_isalnum(*c) && *c != '-') return FALSE;
    return TRUE;
}

typedef struct { GString *text; gboolean quoted; } Token;

static void tokens_free(GPtrArray *tokens)
{
    for (guint i = 0; i < tokens->len; ++i) {
        Token *token = g_ptr_array_index(tokens, i);
        g_string_free(token->text, TRUE); g_free(token);
    }
    g_ptr_array_free(tokens, TRUE);
}

static GPtrArray *tokenize(const char *exec, GError **error)
{
    GPtrArray *tokens = g_ptr_array_new();
    Token *current = NULL;
    gboolean quote = FALSE;
    if (!exec || !*exec || strlen(exec) > 4096) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Exec is empty or too long");
        g_ptr_array_free(tokens, TRUE); return NULL;
    }
    for (const char *c = exec; *c; ++c) {
        if (!quote && *c == ' ') {
            if (current) { g_ptr_array_add(tokens, current); current = NULL; }
            continue;
        }
        if (!current) { current = g_new0(Token, 1); current->text = g_string_new(NULL); }
        if (*c == '"') { quote = !quote; current->quoted = TRUE; continue; }
        if (quote && *c == '\\') {
            if (!c[1]) { quote = TRUE; break; }
            g_string_append_c(current->text, *++c);
            continue;
        }
        g_string_append_c(current->text, *c);
    }
    if (current) g_ptr_array_add(tokens, current);
    if (quote || !tokens->len || tokens->len > DESKTOP_MAX_ARGV) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Exec has an unterminated quote or an unsupported argument count");
        tokens_free(tokens); return NULL;
    }
    return tokens;
}

char **desktop_exec_tokens(const char *exec, GError **error)
{
    GPtrArray *tokens = tokenize(exec, error);
    if (!tokens) return NULL;
    char **argv = g_new0(char *, tokens->len + 1);
    for (guint i = 0; i < tokens->len; ++i) argv[i] = g_strdup(((Token *)g_ptr_array_index(tokens, i))->text->str);
    tokens_free(tokens);
    return argv;
}

static gboolean code_allowed(char code, gboolean whole, gboolean quoted)
{
    if (code == '%') return TRUE;
    if (strchr("fFuUicks", code) == NULL) return strchr("dDnNvm", code) != NULL && !quoted;
    if (quoted && code != 'c' && code != 'k') return FALSE;
    if ((code == 'F' || code == 'U' || code == 'i') && !whole) return FALSE;
    return code != 's';
}

static gboolean validate_tokens(GPtrArray *tokens, GError **error)
{
    guint lists = 0;
    for (guint i = 0; i < tokens->len; ++i) {
        Token *token = g_ptr_array_index(tokens, i);
        const char *text = token->text->str;
        gboolean whole = strlen(text) == 2 && text[0] == '%';
        for (const char *c = text; *c; ++c) {
            if (*c != '%') continue;
            if (!c[1] || !code_allowed(c[1], whole, token->quoted)) {
                g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Exec contains an invalid field code in argument %u", i);
                return FALSE;
            }
            if (c[1] == 'F' || c[1] == 'U') ++lists;
            ++c;
        }
    }
    if (lists > 1) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Exec may contain at most one file list field code");
        return FALSE;
    }
    Token *program = g_ptr_array_index(tokens, 0);
    if (!program->text->len || strchr(program->text->str, '%')) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Exec program is empty or uses a field code");
        return FALSE;
    }
    return TRUE;
}

gboolean desktop_exec_valid(const char *exec, GError **error)
{
    GPtrArray *tokens = tokenize(exec, error);
    if (!tokens) return FALSE;
    gboolean valid = validate_tokens(tokens, error);
    tokens_free(tokens);
    return valid;
}

static gboolean argument_safe(const char *value)
{
    return value && *value && strlen(value) <= 4096 && g_utf8_validate(value, -1, NULL);
}

static gboolean value_safe(const char *value)
{
    return argument_safe(value) && value[0] != '-';
}

static char *substitute(const char *text, const DesktopExecContext *context, const DesktopEntry *entry, gboolean *drop)
{
    GString *out = g_string_new(NULL);
    *drop = FALSE;
    for (const char *c = text; *c; ++c) {
        if (*c != '%') { g_string_append_c(out, *c); continue; }
        ++c;
        switch (*c) {
        case '%': g_string_append_c(out, '%'); break;
        case 'f': if (context->files && context->files[0]) g_string_append(out, context->files[0]); else *drop = TRUE; break;
        case 'u': if (context->uris && context->uris[0]) g_string_append(out, context->uris[0]); else *drop = TRUE; break;
        case 'c': g_string_append(out, context->name ? context->name : entry->name ? entry->name : ""); break;
        case 'k': g_string_append(out, context->desktop_path ? context->desktop_path : entry->path ? entry->path : ""); break;
        default: break;
        }
    }
    return g_string_free(out, FALSE);
}

static void append_list(GPtrArray *argv, const char *const *values)
{
    for (; values && *values && argv->len < DESKTOP_MAX_ARGV; ++values)
        if (value_safe(*values)) g_ptr_array_add(argv, g_strdup(*values));
}

char **desktop_exec_expand(const DesktopEntry *entry, const char *exec, const DesktopExecContext *context, GError **error)
{
    GPtrArray *tokens = tokenize(exec ? exec : entry->exec, error);
    if (!tokens) return NULL;
    if (!validate_tokens(tokens, error)) { tokens_free(tokens); return NULL; }
    GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < tokens->len && argv->len < DESKTOP_MAX_ARGV; ++i) {
        Token *token = g_ptr_array_index(tokens, i);
        const char *text = token->text->str;
        if (!strcmp(text, "%F")) { append_list(argv, context->files); continue; }
        if (!strcmp(text, "%U")) { append_list(argv, context->uris); continue; }
        if (!strcmp(text, "%i")) {
            const char *icon = context->icon ? context->icon : entry->icon;
            if (icon && *icon) { g_ptr_array_add(argv, g_strdup("--icon")); g_ptr_array_add(argv, g_strdup(icon)); }
            continue;
        }
        gboolean drop = FALSE;
        char *value = substitute(text, context, entry, &drop);
        gboolean literal = !strchr(text, '%');
        if (drop || !*value || !argument_safe(value) || (!literal && text[0] == '%' && !value_safe(value))) g_free(value);
        else g_ptr_array_add(argv, value);
    }
    tokens_free(tokens);
    if (!argv->len) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Exec expanded to no command");
        g_ptr_array_free(argv, TRUE); return NULL;
    }
    g_ptr_array_add(argv, NULL);
    return (char **)g_ptr_array_free(argv, FALSE);
}

static char *package_from_exec(const char *exec)
{
    char **words = desktop_exec_tokens(exec, NULL);
    char *package = NULL;
    if (words && g_strv_length(words) == 4 && !strcmp(words[0], "waydroid") && !strcmp(words[1], "app")
        && !strcmp(words[2], "launch") && g_regex_match(package_pattern(), words[3], 0, NULL) && strlen(words[3]) <= 128)
        package = g_strdup(words[3]);
    g_strfreev(words);
    return package;
}

DesktopEntry *desktop_entry_parse(const char *path, const char *id, const char *source_dir,
    const DesktopPaths *paths, GError **error)
{
    char *contents = NULL; gsize length = 0;
    gboolean user = paths && paths->home && g_str_has_prefix(path, paths->home) && path[strlen(paths->home)] == '/';
    if (!read_limited(path, !user, &contents, &length, error)) return NULL;
    GKeyFile *file = g_key_file_new();
    if (!g_key_file_load_from_data(file, contents, length, G_KEY_FILE_KEEP_TRANSLATIONS, error)) {
        g_free(contents); g_key_file_unref(file); return NULL;
    }
    g_free(contents);
    const char *language = paths ? paths->language : "C";
    char *type = g_key_file_get_string(file, GROUP, "Type", NULL);
    gboolean application = !g_strcmp0(type, "Application");
    g_free(type);
    if (!g_key_file_has_group(file, GROUP) || !application) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Not an Application desktop entry");
        g_key_file_unref(file); return NULL;
    }
    DesktopEntry *entry = g_new0(DesktopEntry, 1);
    entry->id = g_strdup(id);
    entry->path = g_strdup(path);
    entry->source_dir = g_strdup(source_dir);
    entry->hidden = g_key_file_get_boolean(file, GROUP, "Hidden", NULL);
    entry->no_display = g_key_file_get_boolean(file, GROUP, "NoDisplay", NULL);
    entry->terminal = g_key_file_get_boolean(file, GROUP, "Terminal", NULL);
    entry->dbus_activatable = g_key_file_get_boolean(file, GROUP, "DBusActivatable", NULL);
    entry->name = clean_text(g_key_file_get_locale_string(file, GROUP, "Name", language, NULL), 256);
    entry->generic_name = clean_text(g_key_file_get_locale_string(file, GROUP, "GenericName", language, NULL), 256);
    entry->comment = clean_text(g_key_file_get_locale_string(file, GROUP, "Comment", language, NULL), 512);
    entry->icon = g_key_file_get_string(file, GROUP, "Icon", NULL);
    if (entry->icon && (!valid_text(entry->icon, 1024) || !*entry->icon)) g_clear_pointer(&entry->icon, g_free);
    entry->exec = g_key_file_get_string(file, GROUP, "Exec", NULL);
    if (entry->exec && !desktop_exec_valid(entry->exec, NULL)) g_clear_pointer(&entry->exec, g_free);
    entry->try_exec = g_key_file_get_string(file, GROUP, "TryExec", NULL);
    if (entry->try_exec && (!valid_text(entry->try_exec, 1024) || !*entry->try_exec)) g_clear_pointer(&entry->try_exec, g_free);
    entry->categories = list_of(file, GROUP, "Categories", 32, NULL);
    entry->mime_types = list_of(file, GROUP, "MimeType", 128, mime_pattern());
    entry->only_show_in = list_of(file, GROUP, "OnlyShowIn", 16, NULL);
    entry->not_show_in = list_of(file, GROUP, "NotShowIn", 16, NULL);
    entry->accepts_paths = g_key_file_get_boolean(file, GROUP, "X-OpenWave-AcceptsPaths", NULL);
    entry->trusted_source = !user && desktop_file_trusted(path);
    entry->user_source = user;
    char *app_id = g_key_file_get_string(file, GROUP, "X-OpenWave-AppId", NULL);
    if (app_id && strlen(app_id) <= 128 && g_regex_match(identifier_pattern(), app_id, 0, NULL)) entry->app_id = app_id;
    else g_free(app_id);
    entry->package = entry->exec ? package_from_exec(entry->exec) : NULL;
    entry->actions = g_ptr_array_new_with_free_func(action_free);
    char **action_ids = list_of(file, GROUP, "Actions", DESKTOP_MAX_ACTIONS, NULL);
    for (char **action = action_ids; *action; ++action) {
        if (!action_id_valid(*action)) continue;
        char *group = g_strdup_printf("Desktop Action %s", *action);
        char *exec = g_key_file_get_string(file, group, "Exec", NULL);
        char *name = clean_text(g_key_file_get_locale_string(file, group, "Name", language, NULL), 256);
        if (exec && name && *name && desktop_exec_valid(exec, NULL)) {
            DesktopAction *item = g_new0(DesktopAction, 1);
            item->id = g_strdup(*action); item->name = name; item->exec = exec;
            g_ptr_array_add(entry->actions, item);
        } else { g_free(exec); g_free(name); }
        g_free(group);
    }
    g_strfreev(action_ids);
    g_key_file_unref(file);
    if (!entry->hidden && (!entry->name || !*entry->name)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Desktop entry has no Name");
        desktop_entry_free(entry); return NULL;
    }
    if (!entry->hidden && !entry->exec && !entry->dbus_activatable) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Desktop entry has no valid Exec");
        desktop_entry_free(entry); return NULL;
    }
    if (!entry->trusted_source) g_clear_pointer(&entry->app_id, g_free);
    return entry;
}

static gboolean contains(char **list, const char *value)
{
    for (; list && *list; ++list) if (!g_strcmp0(*list, value)) return TRUE;
    return FALSE;
}

gboolean desktop_entry_shown(const DesktopEntry *entry, const char *desktop)
{
    if (entry->hidden || entry->no_display) return FALSE;
    char **current = g_strsplit(desktop && *desktop ? desktop : "", ":", -1);
    gboolean only = !entry->only_show_in || !*entry->only_show_in, never = FALSE;
    for (char **name = current; *name; ++name) {
        only |= contains(entry->only_show_in, *name);
        never |= contains(entry->not_show_in, *name);
    }
    g_strfreev(current);
    return only && !never;
}

gboolean desktop_entry_try_exec_ok(const DesktopEntry *entry)
{
    if (!entry->try_exec) return TRUE;
    if (g_path_is_absolute(entry->try_exec)) return g_file_test(entry->try_exec, G_FILE_TEST_IS_EXECUTABLE) && g_file_test(entry->try_exec, G_FILE_TEST_IS_REGULAR);
    if (strchr(entry->try_exec, '/')) return FALSE;
    const char *directories[] = {"/System/Services", "/usr/local/bin", "/usr/bin", "/bin", NULL};
    for (guint i = 0; directories[i]; ++i) {
        char *candidate = g_build_filename(directories[i], entry->try_exec, NULL);
        gboolean found = g_file_test(candidate, G_FILE_TEST_IS_REGULAR) && g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE);
        g_free(candidate);
        if (found) return TRUE;
    }
    return FALSE;
}

gboolean desktop_entry_has_mime(const DesktopEntry *entry, const char *mime)
{
    return contains(entry->mime_types, mime);
}

char **desktop_entry_schemes(const DesktopEntry *entry)
{
    GPtrArray *schemes = g_ptr_array_new();
    for (char **mime = entry->mime_types; mime && *mime; ++mime)
        if (g_str_has_prefix(*mime, "x-scheme-handler/") && (*mime)[17]) g_ptr_array_add(schemes, g_strdup(*mime + 17));
    g_ptr_array_add(schemes, NULL);
    return (char **)g_ptr_array_free(schemes, FALSE);
}

static int compare_names(gconstpointer a, gconstpointer b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void walk(const char *base, const char *directory, guint depth, const DesktopPaths *paths, GPtrArray *found)
{
    GDir *dir = g_dir_open(directory, 0, NULL);
    if (!dir) return;
    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    const char *name;
    while ((name = g_dir_read_name(dir)) && names->len < 8192) g_ptr_array_add(names, g_strdup(name));
    g_dir_close(dir);
    g_ptr_array_sort(names, compare_names);
    for (guint i = 0; i < names->len && found->len < DESKTOP_MAX_ENTRIES; ++i) {
        char *path = g_build_filename(directory, g_ptr_array_index(names, i), NULL);
        struct stat st;
        if (lstat(path, &st) == 0) {
            gboolean link = S_ISLNK(st.st_mode);
            if (link && g_str_has_prefix(path, paths->home) && path[strlen(paths->home)] == '/') { g_free(path); continue; }
            if (link && stat(path, &st)) { g_free(path); continue; }
            if (S_ISDIR(st.st_mode) && !link && depth < DESKTOP_MAX_DEPTH) walk(base, path, depth + 1, paths, found);
            else if (S_ISREG(st.st_mode) && g_str_has_suffix(path, ".desktop")) {
                char *id = desktop_id_from_path(base, path);
                GError *error = NULL;
                DesktopEntry *entry = id && desktop_id_valid(id) ? desktop_entry_parse(path, id, base, paths, &error) : NULL;
                if (entry) g_ptr_array_add(found, entry);
                else if (error) g_debug("appd: skipping desktop entry %s: %s", path, error->message);
                g_clear_error(&error); g_free(id);
            }
        }
        g_free(path);
    }
    g_ptr_array_unref(names);
}

GPtrArray *desktop_scan(const DesktopPaths *paths)
{
    GPtrArray *all = g_ptr_array_new();
    GPtrArray *roots = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(roots, g_build_filename(paths->data_home, "applications", NULL));
    for (char **dir = paths->data_dirs; *dir; ++dir) g_ptr_array_add(roots, g_build_filename(*dir, "applications", NULL));
    for (guint i = 0; i < roots->len; ++i) walk(g_ptr_array_index(roots, i), g_ptr_array_index(roots, i), 0, paths, all);
    g_ptr_array_unref(roots);
    GPtrArray *result = g_ptr_array_new_with_free_func((GDestroyNotify)desktop_entry_free);
    GHashTable *picked = g_hash_table_new(g_direct_hash, g_direct_equal);
    GHashTable *done = g_hash_table_new(g_str_hash, g_str_equal);
    for (guint i = 0; i < all->len; ++i) {
        DesktopEntry *entry = g_ptr_array_index(all, i);
        if (g_hash_table_contains(done, entry->id)) continue;
        g_hash_table_add(done, entry->id);
        DesktopEntry *pick = entry;
        for (guint j = i + 1; !entry->trusted_source && j < all->len; ++j) {
            DesktopEntry *other = g_ptr_array_index(all, j);
            if (!strcmp(other->id, entry->id) && other->trusted_source) { pick = other; break; }
        }
        if (pick != entry && entry->hidden) pick->hidden = TRUE;
        g_hash_table_add(picked, pick);
        g_ptr_array_add(result, pick);
    }
    for (guint i = 0; i < all->len; ++i)
        if (!g_hash_table_contains(picked, g_ptr_array_index(all, i))) desktop_entry_free(g_ptr_array_index(all, i));
    g_ptr_array_free(all, TRUE);
    g_hash_table_unref(picked);
    g_hash_table_unref(done);
    return result;
}

typedef struct { char *dir; int size; int min; int max; int threshold; char type; } IconDirectory;

static gint icon_distance(const IconDirectory *dir, int size)
{
    if (dir->type == 'F') return abs(dir->size - size);
    if (dir->type == 'S') {
        if (size < dir->min) return dir->min - size;
        if (size > dir->max) return size - dir->max;
        return 0;
    }
    if (size < dir->size - dir->threshold) return dir->min - size;
    if (size > dir->size + dir->threshold) return size - dir->max;
    return 0;
}

static GKeyFile *theme_index(const DesktopPaths *paths, const char *theme, char **base_out)
{
    GPtrArray *bases = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(bases, g_build_filename(paths->data_home, "icons", NULL));
    for (char **dir = paths->data_dirs; *dir; ++dir) g_ptr_array_add(bases, g_build_filename(*dir, "icons", NULL));
    GKeyFile *index = NULL;
    for (guint i = 0; i < bases->len && !index; ++i) {
        char *file = g_build_filename(g_ptr_array_index(bases, i), theme, "index.theme", NULL);
        GKeyFile *candidate = g_key_file_new();
        g_key_file_set_list_separator(candidate, ',');
        if (g_file_test(file, G_FILE_TEST_IS_REGULAR) && g_key_file_load_from_file(candidate, file, G_KEY_FILE_NONE, NULL)) {
            index = candidate;
            if (base_out) *base_out = g_strdup(g_ptr_array_index(bases, i));
        } else g_key_file_unref(candidate);
        g_free(file);
    }
    g_ptr_array_unref(bases);
    return index;
}

static const char *const icon_extensions[] = {"png", "svg", "xpm", NULL};

static char *find_in_theme(const DesktopPaths *paths, const char *theme, const char *name, int size, GHashTable *visited, guint depth)
{
    if (depth > 6 || g_hash_table_contains(visited, theme)) return NULL;
    g_hash_table_add(visited, g_strdup(theme));
    char *base = NULL;
    GKeyFile *index = theme_index(paths, theme, &base);
    char *best = NULL;
    if (index) {
        char **directories = g_key_file_get_string_list(index, "Icon Theme", "Directories", NULL, NULL);
        int best_distance = G_MAXINT;
        for (char **dir = directories; dir && *dir; ++dir) {
            if (strstr(*dir, "..") || **dir == '/') continue;
            IconDirectory item = {*dir, g_key_file_get_integer(index, *dir, "Size", NULL), 0, 0,
                g_key_file_has_key(index, *dir, "Threshold", NULL) ? g_key_file_get_integer(index, *dir, "Threshold", NULL) : 2, 'T'};
            char *type = g_key_file_get_string(index, *dir, "Type", NULL);
            if (type && !strcmp(type, "Fixed")) item.type = 'F';
            else if (type && !strcmp(type, "Scalable")) item.type = 'S';
            g_free(type);
            item.min = g_key_file_has_key(index, *dir, "MinSize", NULL) ? g_key_file_get_integer(index, *dir, "MinSize", NULL) : item.size;
            item.max = g_key_file_has_key(index, *dir, "MaxSize", NULL) ? g_key_file_get_integer(index, *dir, "MaxSize", NULL) : item.size;
            int distance = icon_distance(&item, size);
            if (distance >= best_distance) continue;
            for (guint e = 0; icon_extensions[e]; ++e) {
                char *file = g_strdup_printf("%s.%s", name, icon_extensions[e]);
                char *candidate = g_build_filename(base, theme, *dir, file, NULL);
                g_free(file);
                if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR)) {
                    g_free(best); best = candidate; best_distance = distance; break;
                }
                g_free(candidate);
            }
        }
        g_strfreev(directories);
        if (!best) {
            char **parents = g_key_file_get_string_list(index, "Icon Theme", "Inherits", NULL, NULL);
            for (char **parent = parents; parents && *parent && !best; ++parent)
                best = find_in_theme(paths, *parent, name, size, visited, depth + 1);
            g_strfreev(parents);
        }
        g_key_file_unref(index);
    }
    g_free(base);
    return best;
}

char *desktop_resolve_icon(const DesktopPaths *paths, const char *icon, int size)
{
    if (!icon || !*icon || strlen(icon) > 1024) return NULL;
    if (g_path_is_absolute(icon)) {
        struct stat st;
        gboolean image = g_str_has_suffix(icon, ".png") || g_str_has_suffix(icon, ".svg") || g_str_has_suffix(icon, ".xpm") || g_str_has_suffix(icon, ".jpg");
        return image && stat(icon, &st) == 0 && S_ISREG(st.st_mode) && st.st_size <= 8 * 1024 * 1024 ? g_strdup(icon) : NULL;
    }
    if (strchr(icon, '/') || strstr(icon, "..")) return NULL;
    GHashTable *visited = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    char *found = find_in_theme(paths, paths->icon_theme, icon, size, visited, 0);
    if (!found) found = find_in_theme(paths, "hicolor", icon, size, visited, 0);
    g_hash_table_unref(visited);
    for (char **dir = paths->data_dirs; !found && *dir; ++dir)
        for (guint e = 0; !found && icon_extensions[e]; ++e) {
            char *file = g_strdup_printf("%s.%s", icon, icon_extensions[e]);
            char *candidate = g_build_filename(*dir, "pixmaps", file, NULL);
            g_free(file);
            if (g_file_test(candidate, G_FILE_TEST_IS_REGULAR)) found = candidate; else g_free(candidate);
        }
    return found;
}
