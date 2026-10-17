#include <OpenWave/OpenWaveXml.h>
#include <OpenWave/OpenWaveIdentity.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/openat2.h>

#define BUS "org.openwave.portal"
#define PATH "/org/openwave/portal/FileChooser"
#define IFACE "org.openwave.portal.FileChooser"
#define ERROR_PREFIX "org.openwave.portal.Error."

enum { PENDING, SELECTED, CANCELLED, FAILED };
typedef struct {
    char *id, *sender, *mode, *title, *muted_title, *primary_title;
    char *root, *initial, *suggested, *parent;
    char *application, *request_identifier, *error;
    char **mime_types;
    GPtrArray *paths;
    guint uid, pid, status;
    char *permission_owner, *lease;
    gboolean authorizing;
} Request;
typedef struct {
    GDBusConnection *bus;
    GMainLoop *loop;
    GQueue *requests;
    char *backend_sender;
    char *root_prefix;
    char *backend_executable;
    gboolean test_mode;
} Service;

static void request_free(Request *request)
{
    g_free(request->permission_owner); g_free(request->lease);
    g_free(request->id); g_free(request->sender); g_free(request->mode);
    g_free(request->title); g_free(request->muted_title);
    g_free(request->primary_title); g_free(request->root); g_free(request->initial);
    g_free(request->suggested); g_free(request->parent);
    g_free(request->application); g_free(request->request_identifier);
    g_free(request->error); g_strfreev(request->mime_types);
    g_ptr_array_free(request->paths, TRUE);
    g_free(request);
}

static void fail(GDBusMethodInvocation *call, const char *kind, const char *message)
{
    char *name = g_strconcat(ERROR_PREFIX, kind, NULL);
    g_dbus_method_invocation_return_dbus_error(call, name, message);
    g_free(name);
}

static gboolean credentials(Service *service, const char *sender, guint *uid, guint *pid)
{
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_sync(service->bus, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixUser",
        g_variant_new("(s)", sender), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE,
        3000, NULL, &error);
    if (!reply) { g_clear_error(&error); return FALSE; }
    g_variant_get(reply, "(u)", uid);
    g_variant_unref(reply);
    reply = g_dbus_connection_call_sync(service->bus, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionUnixProcessID",
        g_variant_new("(s)", sender), G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE,
        3000, NULL, &error);
    if (!reply) { g_clear_error(&error); return FALSE; }
    g_variant_get(reply, "(u)", pid);
    g_variant_unref(reply);
    return TRUE;
}

static char *executable_for_pid(guint pid)
{
    char *link = g_strdup_printf("/proc/%u/exe", pid);
    char *executable = g_file_read_link(link, NULL);
    g_free(link);
    return executable;
}

static gboolean safe_component(const char *name);

static gboolean is_backend(Service *service, const char *sender)
{
    return service->backend_sender && g_str_equal(sender, service->backend_sender);
}

static gboolean safe_component(const char *name)
{
    if (!name || !*name || strlen(name) > 255 || g_str_equal(name, ".")
        || g_str_equal(name, "..")) return FALSE;
    for (const char *p = name; *p; ++p)
        if (*p == '/' || *p == '\\' || (guchar) *p < 32) return FALSE;
    return TRUE;
}

static gboolean within(const char *root, const char *path)
{
    return g_str_equal(root, path) ||
        (g_str_has_prefix(path, root) && path[strlen(root)] == '/');
}

static char *canonical_user_root(Service *service, guint uid)
{
    struct passwd *user = getpwuid(uid);
    if (!user || !safe_component(user->pw_name)) return NULL;
    char *data = g_build_filename(service->root_prefix, "Data", NULL);
    char *users = g_build_filename(data, "Users", NULL);
    char *requested = g_build_filename(users, user->pw_name, NULL);
    struct stat info;
    gboolean safe = (lstat(data, &info) != 0 || !S_ISLNK(info.st_mode))
        && (lstat(users, &info) != 0 || !S_ISLNK(info.st_mode))
        && (lstat(requested, &info) != 0 || !S_ISLNK(info.st_mode));
    if (safe && g_mkdir_with_parents(requested, 0700) != 0) safe = FALSE;
    if (safe && !service->test_mode) {
        safe = chown(data, 0, 0) == 0 && chmod(data, 0755) == 0
            && chown(users, 0, 0) == 0 && chmod(users, 0755) == 0
            && chown(requested, uid, user->pw_gid) == 0
            && chmod(requested, 0711) == 0;
    }
    char *result = safe ? realpath(requested, NULL) : NULL;
    g_free(data); g_free(users); g_free(requested);
    return result;
}

static Request *find_request(Service *service, const char *id)
{
    for (GList *node = service->requests->head; node; node = node->next) {
        Request *request = node->data;
        if (g_str_equal(request->id, id)) return request;
    }
    return NULL;
}

static void announce(Service *service, Request *request)
{
    g_dbus_connection_emit_signal(service->bus, request->sender, PATH, IFACE,
        "Response", g_variant_new("(ou)", request->id, request->status), NULL);
}

static void end_request(Service *service, Request *request, guint status, const char *error)
{
    if (request->status != PENDING) return;
    request->status = status;
    request->error = g_strdup(error ? error : "");
    announce(service, request);
}

static void release_lease(Service *, const char *, const char *, const char *);
static void discard_request(Service *service, Request *request)
{
    release_lease(service, request->permission_owner, request->lease, request->sender);
    g_queue_remove(service->requests, request);
    request_free(request);
}

static int open_beneath(Request *request, const char *path, int flags, mode_t mode)
{
    if (!within(request->root, path)) { errno = EACCES; return -1; }
    int root_fd = open(request->root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root_fd < 0) return -1;
    const char *relative = g_str_equal(path, request->root)
        ? "." : path + strlen(request->root) + 1;
    struct open_how how = {
        .flags = (guint64) (flags | O_CLOEXEC),
        .mode = mode,
        .resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS,
    };
    int fd = syscall(SYS_openat2, root_fd, relative, &how, sizeof(how));
    close(root_fd);
    return fd;
}

static gboolean allowed_type(Request *request, const char *path)
{
    if (!request->mime_types || !request->mime_types[0]) return TRUE;
    char header[4096];
    gsize length = 0;
    int fd = open_beneath(request, path, O_RDONLY, 0);
    if (fd < 0) return FALSE;
    ssize_t read_size = read(fd, header, sizeof(header));
    if (read_size > 0) length = (gsize) read_size;
    close(fd);
    if (!length) return FALSE;
    const char *known = NULL;
    if (length >= 8 && memcmp(header, "\x89PNG\r\n\x1a\n", 8) == 0)
        known = "image/png";
    else if (length >= 3 && (guchar) header[0] == 0xff
        && (guchar) header[1] == 0xd8 && (guchar) header[2] == 0xff)
        known = "image/jpeg";
    else if (length >= 12 && memcmp(header, "RIFF", 4) == 0
        && memcmp(header + 8, "WEBP", 4) == 0)
        known = "image/webp";
    gboolean uncertain = FALSE;
    char *content_type = known ? NULL : g_content_type_guess(path,
        (const guchar *) header, length, &uncertain);
    char *guessed = content_type ? g_content_type_get_mime_type(content_type) : NULL;
    const char *mime = known ? known : guessed;
    gboolean accepted = FALSE;
    for (char **allowed = request->mime_types; *allowed; ++allowed)
        if (g_str_equal(*allowed, mime)) { accepted = TRUE; break; }
    g_free(content_type); g_free(guessed);
    return accepted;
}

static char *validated_path(Request *request, const char *path, gboolean save)
{
    if (!path || path[0] != '/' || strlen(path) > 4096) return NULL;
    char *canonical = save ? NULL : realpath(path, NULL);
    if (save) {
        char *parent = g_path_get_dirname(path);
        char *name = g_path_get_basename(path);
        char *directory = realpath(parent, NULL);
        if (directory && safe_component(name) && within(request->root, directory))
            canonical = g_build_filename(directory, name, NULL);
        g_free(parent); g_free(name); g_free(directory);
        if (canonical && g_file_test(canonical, G_FILE_TEST_EXISTS))
            g_clear_pointer(&canonical, g_free);
    }
    if (!canonical || !within(request->root, canonical)) {
        g_free(canonical); return NULL;
    }
    if (!save) {
        struct stat info;
        if (lstat(canonical, &info) != 0
            || (g_str_equal(request->mode, "SelectDirectory")
                ? !S_ISDIR(info.st_mode) : !S_ISREG(info.st_mode))
            || (!g_str_equal(request->mode, "SelectDirectory")
                && !allowed_type(request, canonical))) {
            g_free(canonical); return NULL;
        }
    }
    return canonical;
}

static gboolean valid_mimes(char **mimes)
{
    if (!mimes) return TRUE;
    if (g_strv_length(mimes) > 32) return FALSE;
    for (char **item = mimes; *item; ++item) {
        if (strlen(*item) > 128 || !strchr(*item, '/')) return FALSE;
        for (const char *p = *item; *p; ++p)
            if (!(g_ascii_isalnum(*p) || strchr("/-.+", *p))) return FALSE;
    }
    return TRUE;
}

static gboolean valid_options(GVariant *options)
{
    GVariantIter iterator;
    const char *key;
    GVariant *value;
    g_variant_iter_init(&iterator, options);
    while (g_variant_iter_next(&iterator, "{&sv}", &key, &value)) {
        gboolean valid = TRUE;
        if (g_str_equal(key, "title") || g_str_equal(key, "muted_title")
            || g_str_equal(key, "primary_title") || g_str_equal(key, "initial_directory")
            || g_str_equal(key, "suggested_filename") || g_str_equal(key, "parent_window")
            || g_str_equal(key, "request_identifier") || g_str_equal(key, "directory_token"))
            valid = g_variant_is_of_type(value, G_VARIANT_TYPE_STRING);
        else if (g_str_equal(key, "mime_types"))
            valid = g_variant_is_of_type(value, G_VARIANT_TYPE("as"));
        else if (g_str_equal(key, "multiple"))
            valid = g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN);
        g_variant_unref(value);
        if (!valid) return FALSE;
    }
    return TRUE;
}

static GKeyFile *directory_store(void)
{
    const char *path = "/var/lib/openwave/portal-directories.ini";
    struct stat st; GKeyFile *file = g_key_file_new();
    if (!lstat(path, &st)) {
        if (!S_ISREG(st.st_mode) || st.st_uid || (st.st_mode & 0077) || st.st_nlink != 1 || st.st_size > 1048576
            || !g_key_file_load_from_file(file, path, 0, NULL)) { g_key_file_unref(file); return NULL; }
    } else if (errno != ENOENT) { g_key_file_unref(file); return NULL; }
    gsize count; char **groups = g_key_file_get_groups(file, &count);
    gboolean changed = FALSE;
    for (gsize i = 0; i < count; ++i) {
        char *app = g_key_file_get_string(file, groups[i], "Application", NULL);
        const char *id = !g_strcmp0(app, "browser") ? "com.openwave.ocean"
            : !g_strcmp0(app, "settings") ? "com.openwave.settings"
            : !g_strcmp0(app, "files") ? "com.openwave.files" : NULL;
        if (id) { g_key_file_set_string(file, groups[i], "Application", id); changed = TRUE; }
        g_free(app);
    }
    g_strfreev(groups);
    if (changed) {
        gsize length; char *contents = g_key_file_to_data(file, &length, NULL);
        gboolean okay = g_file_set_contents_full(path, contents, length,
            G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, NULL);
        g_free(contents);
        if (!okay) { g_key_file_unref(file); return NULL; }
    }
    return file;
}
static char *remember_directory(Service *service, Request *request)
{
    GKeyFile *file = directory_store();
    if (!file) return NULL;
    gsize count = 0; char **groups = g_key_file_get_groups(file, &count);
    if (request->paths->len != 1) { g_strfreev(groups); g_key_file_unref(file); return NULL; }
    const char *path = g_ptr_array_index(request->paths, 0); struct stat st;
    int fd = open_beneath(request, path, O_RDONLY | O_DIRECTORY, 0);
    gboolean okay = fd >= 0 && !fstat(fd, &st); if (fd >= 0) close(fd);
    if (!okay) { g_strfreev(groups); g_key_file_unref(file); return NULL; }
    guint owned = 0;
    for (gsize i = 0; i < count; ++i) {
        char *app = g_key_file_get_string(file, groups[i], "Application", NULL);
        char *root = g_key_file_get_string(file, groups[i], "Root", NULL);
        char *saved = g_key_file_get_string(file, groups[i], "Path", NULL);
        gboolean mine = !g_strcmp0(app, request->application) && !g_strcmp0(root, request->root);
        owned += mine;
        gboolean same = mine && !g_strcmp0(saved, path)
            && st.st_dev == g_key_file_get_uint64(file, groups[i], "Device", NULL)
            && st.st_ino == g_key_file_get_uint64(file, groups[i], "Inode", NULL);
        g_free(app); g_free(root); g_free(saved);
        if (same) {
            char *token = g_strdup(groups[i]); g_strfreev(groups); g_key_file_unref(file); return token;
        }
    }
    g_strfreev(groups);
    if (count >= 1024 || owned >= 64) { g_key_file_unref(file); return NULL; }
    char *token = g_uuid_string_random();
    g_key_file_set_string(file, token, "Application", request->application);
    g_key_file_set_string(file, token, "Root", request->root);
    g_key_file_set_string(file, token, "Path", path);
    g_key_file_set_uint64(file, token, "Device", st.st_dev); g_key_file_set_uint64(file, token, "Inode", st.st_ino);
    gsize serialized = 0; char *contents = g_key_file_to_data(file, &serialized, NULL); g_free(contents);
    okay = serialized <= 1048576 && g_key_file_save_to_file(file, "/var/lib/openwave/portal-directories.ini", NULL)
        && !chmod("/var/lib/openwave/portal-directories.ini", 0600);
    g_key_file_unref(file); (void)service;
    if (!okay) g_clear_pointer(&token, g_free);
    return token;
}
static char *download_directory(Service *service, const char *token, const char *app, const char *root)
{
    if (!token || !*token) {
        char *path = g_build_filename(root, "Downloads", NULL);
        if (g_mkdir_with_parents(path, 0700)) { g_free(path); return NULL; }
        Request request = {.root = (char *)root};
        int fd = open_beneath(&request, path, O_RDONLY | O_DIRECTORY, 0);
        if (fd < 0) g_clear_pointer(&path, g_free); else close(fd);
        return path;
    }
    GKeyFile *file = directory_store(); if (!file) return NULL;
    char *owner = g_key_file_get_string(file, token, "Application", NULL);
    char *user = g_key_file_get_string(file, token, "Root", NULL);
    char *path = g_key_file_get_string(file, token, "Path", NULL);
    Request request = {.root = (char *)root}; struct stat st;
    int fd = path && !g_strcmp0(app, owner) && !g_strcmp0(root, user) ? open_beneath(&request, path, O_RDONLY | O_DIRECTORY, 0) : -1;
    gboolean okay = fd >= 0 && !fstat(fd, &st) && st.st_dev == g_key_file_get_uint64(file, token, "Device", NULL)
        && st.st_ino == g_key_file_get_uint64(file, token, "Inode", NULL);
    if (fd >= 0) close(fd);
    g_key_file_unref(file); g_free(owner); g_free(user); (void)service;
    if (!okay) g_clear_pointer(&path, g_free);
    return path;
}
typedef struct { Service *service; char *id; char *owner; char *sender; } Authorization;
static void release_lease(Service *service, const char *owner, const char *lease, const char *sender)
{
    if (!owner || !lease || !*lease) return;
    g_dbus_connection_call(service->bus, owner, "/org/openwave/Permissions", "org.openwave.Permissions1", "ReleaseLease",
        g_variant_new("(ss)", lease, sender), NULL, 0, 1000, NULL, NULL, NULL);
}
static void authorized(GObject *source, GAsyncResult *result, gpointer data)
{
    Authorization *auth = data; Service *service = auth->service;
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    Request *request = find_request(service, auth->id);
    if (request) request->authorizing = FALSE;
    if (reply) {
        const char *lease; g_variant_get(reply, "(&s)", &lease);
        if (request && request->status == PENDING) {
            request->lease = g_strdup(lease); request->permission_owner = g_strdup(auth->owner);
            if (!strcmp(request->mode, "CreateFile")) {
                char *path = g_build_filename(request->initial, request->suggested, NULL);
                g_ptr_array_add(request->paths, path); end_request(service, request, SELECTED, NULL);
            }
        } else release_lease(service, auth->owner, lease, auth->sender);
        g_variant_unref(reply);
    } else if (request && request->status == PENDING) end_request(service, request, FAILED, "File access was not authorized.");
    g_clear_error(&error); g_free(auth->id); g_free(auth->owner); g_free(auth->sender); g_free(auth);
}
static void authorize(Service *service, Request *request)
{
    if (service->test_mode) return;
    char *owner = openwave_service_owner(service->bus, "org.openwave.Permissions", "/run/openwave/permissiond.pid", NULL);
    if (!owner) { end_request(service, request, FAILED, "The permission service is unavailable."); return; }
    const char *permission = (!strcmp(request->mode, "SaveFile") || !strcmp(request->mode, "CreateFile")) ? "files.write"
        : !strcmp(request->mode, "SelectDirectory") ? "files.directory" : "files.read";
    Authorization *auth = g_new0(Authorization, 1);
    auth->service = service; auth->id = g_strdup(request->id); auth->owner = owner; auth->sender = g_strdup(request->sender);
    request->authorizing = TRUE;
    g_dbus_connection_call(service->bus, owner, "/org/openwave/Permissions", "org.openwave.Permissions1", "AuthorizeFileChooser",
        g_variant_new("(ss)", request->sender, permission), G_VARIANT_TYPE("(s)"), 0, 130000, NULL, authorized, auth);
}
static void open_request(Service *service, const char *sender, const char *mode,
                         GVariant *parameters, GDBusMethodInvocation *call)
{
    if (!service->backend_sender && strcmp(mode, "CreateFile")) {
        fail(call, "BackendUnavailable", "The file chooser is unavailable."); return;
    }
    if (g_queue_get_length(service->requests) >= 64) {
        fail(call, "Busy", "Too many file chooser requests."); return;
    }
    guint uid = 0, pid = 0;
    if (!credentials(service, sender, &uid, &pid)) {
        fail(call, "Unauthorized", "Could not verify the requesting process."); return;
    }
    GVariant *identity = service->test_mode ? NULL : openwave_resolve_caller(service->bus, sender, NULL);
    guint user_uid = uid;
    if (!service->test_mode && (!identity || !g_variant_lookup(identity, "userId", "u", &user_uid))) {
        if (identity) g_variant_unref(identity);
        fail(call, "Unauthorized", "The requester is not a live managed application."); return;
    }
    const char *appid = NULL;
    if (identity) g_variant_lookup(identity, "appId", "&s", &appid);
    g_autofree char *verified_app = g_strdup(appid);
    if (identity) g_variant_unref(identity);
    char *root = canonical_user_root(service, user_uid);
    char *executable = executable_for_pid(pid);
    if (!root || !executable) {
        fail(call, "Unauthorized", "The requester has no verified user storage or executable.");
        g_free(root); g_free(executable); return;
    }
    GVariant *options = g_variant_get_child_value(parameters, 0);
    if (!valid_options(options)) {
        fail(call, "InvalidRequest", "File chooser option has an invalid type.");
        g_variant_unref(options); g_free(root); g_free(executable); return;
    }
    const char *title = NULL, *muted_title = NULL, *primary_title = NULL;
    const char *initial = NULL, *suggested = NULL, *parent = NULL;
    const char *identifier = NULL;
    g_variant_lookup(options, "title", "&s", &title);
    g_variant_lookup(options, "muted_title", "&s", &muted_title);
    g_variant_lookup(options, "primary_title", "&s", &primary_title);
    g_variant_lookup(options, "initial_directory", "&s", &initial);
    g_variant_lookup(options, "suggested_filename", "&s", &suggested);
    g_variant_lookup(options, "parent_window", "&s", &parent);
    g_variant_lookup(options, "request_identifier", "&s", &identifier);
    char **mimes = NULL;
    g_variant_lookup(options, "mime_types", "^as", &mimes);
    gboolean multiple = FALSE;
    g_variant_lookup(options, "multiple", "b", &multiple);
    gboolean valid = (!title || strlen(title) <= 128)
        && (!muted_title || strlen(muted_title) <= 128)
        && (!primary_title || strlen(primary_title) <= 128)
        && (!parent || strlen(parent) <= 128)
        && (!identifier || strlen(identifier) <= 128)
        && (!suggested || safe_component(suggested)) && valid_mimes(mimes)
        && (!multiple || g_str_equal(mode, "OpenFile") || g_str_equal(mode, "OpenFiles"));
    const char *directory_token = NULL;
    g_variant_lookup(options, "directory_token", "&s", &directory_token);
    char *initial_path = !strcmp(mode, "CreateFile")
        ? download_directory(service, directory_token, verified_app, root)
        : initial && *initial ? realpath(initial, NULL) : g_strdup(root);
    if (!strcmp(mode, "CreateFile") && (!suggested || !*suggested || (directory_token && strlen(directory_token) > 128))) valid = FALSE;
    if (!initial_path || !within(root, initial_path)
        || !g_file_test(initial_path, G_FILE_TEST_IS_DIR)) valid = FALSE;
    if (!valid) {
        fail(call, "InvalidRequest", "Invalid file chooser options or initial directory.");
        g_free(initial_path); g_strfreev(mimes); g_variant_unref(options);
        g_free(root); g_free(executable); return;
    }
    Request *request = g_new0(Request, 1);
    request->sender = g_strdup(sender);
    request->mode = g_strdup(multiple ? "OpenFiles" : mode);
    request->title = g_strdup(title && *title ? title : mode);
    request->muted_title = g_strdup(muted_title ? muted_title : "");
    request->primary_title = g_strdup(primary_title ? primary_title : "");
    request->root = root;
    request->initial = initial_path;
    request->suggested = g_strdup(suggested ? suggested : "");
    if (!strcmp(mode, "CreateFile")) {
        for (guint suffix = 1; suffix < 10000; ++suffix) {
            char *candidate = g_build_filename(initial_path, request->suggested, NULL);
            struct stat st; gboolean exists = !lstat(candidate, &st); g_free(candidate);
            if (!exists) break;
            g_free(request->suggested);
            const char *dot = strrchr(suggested, '.');
            if (dot == suggested) dot = NULL;
            char *stem = dot ? g_strndup(suggested, dot - suggested) : g_strdup(suggested);
            request->suggested = g_strdup_printf("%s (%u)%s", stem, suffix, dot ? dot : ""); g_free(stem);
        }
    }
    request->parent = g_strdup(parent ? parent : "");
    request->request_identifier = g_strdup(identifier ? identifier : "");
    request->application = service->test_mode ? g_path_get_basename(executable)
                                              : g_strdup(verified_app);
    request->mime_types = mimes;
    request->paths = g_ptr_array_new_with_free_func(g_free);
    request->uid = uid; request->pid = pid;
    char *uuid = g_uuid_string_random();
    for (char *p = uuid; *p; ++p) if (*p == '-') *p = '_';
    request->id = g_strdup_printf("/org/openwave/portal/request/r%s", uuid);
    g_free(uuid); g_free(executable); g_variant_unref(options);
    g_queue_push_tail(service->requests, request);
    g_dbus_method_invocation_return_value(call, g_variant_new("(o)", request->id));
    authorize(service, request);
}

static void return_result(Service *service, Request *request,
                          GDBusMethodInvocation *call)
{
    if (request->status == PENDING) {
        fail(call, "Pending", "The file chooser has not finished."); return;
    }
    GUnixFDList *fds = g_unix_fd_list_new();
    GVariantBuilder names, handles;
    g_variant_builder_init(&names, G_VARIANT_TYPE("as"));
    g_variant_builder_init(&handles, G_VARIANT_TYPE("ah"));
    if (request->status == SELECTED) {
        if (!service->test_mode) {
            char *owner = openwave_service_owner(service->bus, "org.openwave.Permissions", "/run/openwave/permissiond.pid", NULL);
            GVariant *reply = owner && request->lease && !g_strcmp0(owner, request->permission_owner)
                ? g_dbus_connection_call_sync(service->bus, owner, "/org/openwave/Permissions", "org.openwave.Permissions1", "ValidateLease",
                    g_variant_new("(ss)", request->lease, request->sender), G_VARIANT_TYPE("(b)"), 0, 1500, NULL, NULL) : NULL;
            gboolean valid = FALSE;
            if (reply) { g_variant_get(reply, "(b)", &valid); g_variant_unref(reply); }
            g_free(owner);
            if (!valid) {
                g_object_unref(fds); fail(call, "PermissionDenied", "The file grant expired or was revoked.");
                discard_request(service, request); return;
            }
        }
        for (guint i = 0; i < request->paths->len; ++i) {
            const char *saved_path = g_ptr_array_index(request->paths, i);
            gboolean save = g_str_equal(request->mode, "SaveFile") || g_str_equal(request->mode, "CreateFile");
            char *path = validated_path(request, saved_path, save);
            if (!path) {
                g_object_unref(fds);
                fail(call, "InvalidSelection", "The selected resource is no longer available.");
                discard_request(service, request); return;
            }
            int flags = save ? O_WRONLY | O_CREAT | O_EXCL
                : g_str_equal(request->mode, "SelectDirectory") ? O_RDONLY | O_DIRECTORY
                : O_RDONLY;
            int fd = open_beneath(request, path, flags, save ? 0600 : 0);
            int handle = fd < 0 ? -1 : g_unix_fd_list_append(fds, fd, NULL);
            if (fd >= 0) close(fd);
            if (handle < 0) {
                g_free(path); g_object_unref(fds);
                fail(call, "InvalidSelection", "Could not open the selected resource.");
                discard_request(service, request); return;
            }
            char *name = g_path_get_basename(path);
            g_variant_builder_add(&names, "s", name);
            g_variant_builder_add(&handles, "h", handle);
            g_free(name); g_free(path);
        }
    }
    g_dbus_method_invocation_return_value_with_unix_fd_list(call,
        g_variant_new("(u@as@ah)", request->status,
            g_variant_builder_end(&names), g_variant_builder_end(&handles)), fds);
    g_object_unref(fds);
    discard_request(service, request);
}

static void method_call(GDBusConnection *bus, const char *sender, const char *object,
                        const char *interface, const char *method, GVariant *parameters,
                        GDBusMethodInvocation *call, gpointer data)
{
    (void) bus; (void) object; (void) interface;
    Service *service = data;
    if (g_str_equal(method, "OpenFile") || g_str_equal(method, "OpenFiles")
        || g_str_equal(method, "SaveFile") || g_str_equal(method, "SelectDirectory") || g_str_equal(method, "CreateFile")) {
        open_request(service, sender, method, parameters, call); return;
    }
    if (g_str_equal(method, "RegisterBackend")) {
        guint uid = 0, pid = 0;
        char *executable = NULL;
        gboolean okay = credentials(service, sender, &uid, &pid)
            && uid == (service->test_mode ? getuid() : 0)
            && (executable = executable_for_pid(pid))
            && g_str_equal(executable, service->backend_executable)
            && (service->test_mode || openwave_supervised_process(uid, pid, "/run/openwave/ocean.pid"));
        g_free(executable);
        if (!okay) { fail(call, "Unauthorized", "Only Ocean may provide the file chooser."); return; }
        if (service->backend_sender && !g_str_equal(sender, service->backend_sender)) {
            fail(call, "Busy", "A file chooser backend is already registered."); return;
        }
        g_free(service->backend_sender);
        service->backend_sender = g_strdup(sender);
        g_dbus_method_invocation_return_value(call, NULL); return;
    }
    if (g_str_equal(method, "NextRequest")) {
        if (!is_backend(service, sender)) {
            fail(call, "Unauthorized", "Only the registered backend can read requests."); return;
        }
        Request *next = NULL;
        for (GList *node = service->requests->head; node; node = node->next) {
            Request *candidate = node->data;
            if (candidate->status == PENDING && strcmp(candidate->mode, "CreateFile") && (service->test_mode || candidate->lease)) { next = candidate; break; }
        }
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
        if (next) {
            g_variant_builder_add(&options, "{sv}", "title", g_variant_new_string(next->title));
            g_variant_builder_add(&options, "{sv}", "muted_title", g_variant_new_string(next->muted_title));
            g_variant_builder_add(&options, "{sv}", "primary_title", g_variant_new_string(next->primary_title));
            g_variant_builder_add(&options, "{sv}", "initial_directory", g_variant_new_string(next->initial));
            g_variant_builder_add(&options, "{sv}", "user_root", g_variant_new_string(next->root));
            g_variant_builder_add(&options, "{sv}", "suggested_filename", g_variant_new_string(next->suggested));
            g_variant_builder_add(&options, "{sv}", "parent_window", g_variant_new_string(next->parent));
            g_variant_builder_add(&options, "{sv}", "request_identifier", g_variant_new_string(next->request_identifier));
            g_variant_builder_add(&options, "{sv}", "application_id", g_variant_new_string(next->application));
            const char *empty_mimes[] = { NULL };
            g_variant_builder_add(&options, "{sv}", "mime_types",
                g_variant_new_strv(next->mime_types
                    ? (const char * const *) next->mime_types : empty_mimes, -1));
        }
        g_dbus_method_invocation_return_value(call,
            g_variant_new("(os@a{sv})", next ? next->id : "/",
                next ? next->mode : "", g_variant_builder_end(&options)));
        return;
    }
    const char *id = NULL;
    g_variant_get_child(parameters, 0, "&o", &id);
    Request *request = find_request(service, id);
    if (!request) { fail(call, "UnknownRequest", "No such file chooser request."); return; }
    if (g_str_equal(method, "CompleteRequest")) {
        if (!service->test_mode && !request->lease) { fail(call, "PermissionDenied", "This request has no permission grant."); return; }
        if (!is_backend(service, sender)) {
            fail(call, "Unauthorized", "Only the registered backend can complete requests."); return;
        }
        guint status = 0;
        char **paths = NULL;
        g_variant_get(parameters, "(&ou^as)", &id, &status, &paths);
        if (request->status != PENDING || (status != SELECTED && status != CANCELLED)
            || (status == SELECTED && (!paths || !paths[0]))
            || (paths && g_strv_length(paths) > (g_str_equal(request->mode, "OpenFiles") ? 32u : 1u))) {
            fail(call, "InvalidRequest", "Invalid completion state or selection count.");
            g_strfreev(paths); return;
        }
        if (status == SELECTED) {
            for (char **entry = paths; *entry; ++entry) {
                char *canonical = validated_path(request, *entry,
                    g_str_equal(request->mode, "SaveFile"));
                if (!canonical) {
                    fail(call, "InvalidSelection", "Selection is outside authorized storage or has the wrong type.");
                    g_ptr_array_set_size(request->paths, 0);
                    g_strfreev(paths); return;
                }
                g_ptr_array_add(request->paths, canonical);
            }
        }
        g_strfreev(paths);
        end_request(service, request, status, NULL);
        g_dbus_method_invocation_return_value(call, NULL); return;
    }
    if (!g_str_equal(sender, request->sender)) {
        fail(call, "Unauthorized", "Only the requesting application can use this request."); return;
    }
    if (g_str_equal(method, "DirectoryToken")) {
        if (request->status != SELECTED || strcmp(request->mode, "SelectDirectory") || !request->lease) {
            fail(call, "InvalidRequest", "No selected directory grant is available."); return;
        }
        char *token = remember_directory(service, request);
        if (!token) { fail(call, "Unavailable", "Could not retain the selected directory."); return; }
        g_dbus_method_invocation_return_value(call, g_variant_new("(s)", token)); g_free(token); return;
    }
    if (g_str_equal(method, "CancelRequest")) {
        end_request(service, request, CANCELLED, NULL);
        g_dbus_method_invocation_return_value(call, NULL); return;
    }
    if (g_str_equal(method, "GetStatus")) {
        g_dbus_method_invocation_return_value(call,
            g_variant_new("(us)", request->status, request->error ? request->error : ""));
        return;
    }
    if (g_str_equal(method, "TakeResult")) {
        return_result(service, request, call); return;
    }
    fail(call, "InvalidRequest", "Unknown file chooser method.");
}

static void name_owner_changed(GDBusConnection *bus, const char *sender,
                               const char *object, const char *interface,
                               const char *signal, GVariant *parameters,
                               gpointer data)
{
    (void) bus; (void) sender; (void) object; (void) interface; (void) signal;
    Service *service = data;
    const char *name, *old_owner, *new_owner;
    g_variant_get(parameters, "(&s&s&s)", &name, &old_owner, &new_owner);
    if (!strcmp(name, "org.openwave.Permissions") && *old_owner) {
        for (GList *node = service->requests->head; node; node = node->next) {
            Request *request = node->data;
            if (request->status == PENDING || request->status == SELECTED) {
                request->status = PENDING; end_request(service, request, FAILED, "The permission service restarted.");
            }
        }
    }
    if (*new_owner || !*old_owner) return;
    if (service->backend_sender && g_str_equal(name, service->backend_sender)) {
        g_clear_pointer(&service->backend_sender, g_free);
        for (GList *node = service->requests->head; node; node = node->next)
            end_request(service, node->data, FAILED, "The file chooser backend closed.");
    }
    for (GList *node = service->requests->head; node;) {
        GList *next = node->next;
        Request *request = node->data;
        if (g_str_equal(name, request->sender)) discard_request(service, request);
        node = next;
    }
}

static const GDBusInterfaceVTable vtable = { .method_call = method_call };

int main(int argc, char **argv)
{
#ifndef PORTAL_TESTING
    if (argc != 1) return 2;
#endif
    if (argc != 1 && (argc != 4 || !g_str_equal(argv[1], "--test-root"))) {
        g_printerr("usage: openwave-portal [--test-root ROOT BACKEND_EXECUTABLE]\n");
        return 2;
    }
    Service service = { .requests = g_queue_new(),
        .root_prefix = argc == 4 ? argv[2] : "/",
        .backend_executable = argc == 4 ? argv[3] : "/System/Applications/com.openwave.shell/bin/ocean-launcher",
        .test_mode = argc == 4 };
    GError *error = NULL;
    service.bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!service.bus) {
        g_printerr("openwave-portal: system bus unavailable: %s\n", error->message);
        return 1;
    }
    GDBusNodeInfo *node = openwave_xml_load_dbus_interface(IFACE, &error);
    if (!node || !g_dbus_connection_register_object(service.bus, PATH,
            node->interfaces[0], &vtable,
            &service, NULL, &error)) {
        g_printerr("openwave-portal: registration failed: %s\n", error->message);
        return 1;
    }
    service.loop = g_main_loop_new(NULL, FALSE);
    g_dbus_connection_signal_subscribe(service.bus, "org.freedesktop.DBus",
        "org.freedesktop.DBus", "NameOwnerChanged", "/org/freedesktop/DBus",
        NULL, G_DBUS_SIGNAL_FLAGS_NONE, name_owner_changed, &service, NULL);
    g_bus_own_name_on_connection(service.bus, BUS, G_BUS_NAME_OWNER_FLAGS_NONE,
                                 NULL, NULL, NULL, NULL);
    g_main_loop_run(service.loop);
    return 0;
}
