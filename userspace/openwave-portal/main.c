#include <OpenWave/OpenWaveXml.h>
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

static char *application_for_executable(const char *executable)
{
    GDir *directory = g_dir_open("/usr/share/applications", 0, NULL);
    if (directory) {
        const char *name;
        while ((name = g_dir_read_name(directory))) {
            if (!g_str_has_suffix(name, ".desktop")) continue;
            char *path = g_build_filename("/usr/share/applications", name, NULL);
            GKeyFile *desktop = g_key_file_new();
            char *result = NULL;
            if (g_key_file_load_from_file(desktop, path, G_KEY_FILE_NONE, NULL)) {
                char *exec = g_key_file_get_string(desktop, "Desktop Entry", "Exec", NULL);
                char *id = g_key_file_get_string(desktop, "Desktop Entry", "X-OpenWave-AppId", NULL);
                if (exec && id && g_str_equal(exec, executable) && safe_component(id))
                    result = g_strdup(id);
                g_free(exec); g_free(id);
            }
            g_key_file_free(desktop); g_free(path);
            if (result) { g_dir_close(directory); return result; }
        }
        g_dir_close(directory);
    }
    return g_path_get_basename(executable);
}

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
            && chmod(requested, 0700) == 0;
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

static void discard_request(Service *service, Request *request)
{
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
            || g_str_equal(key, "request_identifier"))
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

static void open_request(Service *service, const char *sender, const char *mode,
                         GVariant *parameters, GDBusMethodInvocation *call)
{
    if (!service->backend_sender) {
        fail(call, "BackendUnavailable", "The file chooser is unavailable."); return;
    }
    if (g_queue_get_length(service->requests) >= 64) {
        fail(call, "Busy", "Too many file chooser requests."); return;
    }
    guint uid = 0, pid = 0;
    if (!credentials(service, sender, &uid, &pid)) {
        fail(call, "Unauthorized", "Could not verify the requesting process."); return;
    }
    char *root = canonical_user_root(service, uid);
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
    char *initial_path = initial && *initial ? realpath(initial, NULL) : g_strdup(root);
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
    request->parent = g_strdup(parent ? parent : "");
    request->request_identifier = g_strdup(identifier ? identifier : "");
    request->application = service->test_mode ? g_path_get_basename(executable)
                                              : application_for_executable(executable);
    request->mime_types = mimes;
    request->paths = g_ptr_array_new_with_free_func(g_free);
    request->uid = uid; request->pid = pid;
    char *uuid = g_uuid_string_random();
    for (char *p = uuid; *p; ++p) if (*p == '-') *p = '_';
    request->id = g_strdup_printf("/org/openwave/portal/request/r%s", uuid);
    g_free(uuid); g_free(executable); g_variant_unref(options);
    g_queue_push_tail(service->requests, request);
    g_dbus_method_invocation_return_value(call, g_variant_new("(o)", request->id));
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
        for (guint i = 0; i < request->paths->len; ++i) {
            const char *saved_path = g_ptr_array_index(request->paths, i);
            gboolean save = g_str_equal(request->mode, "SaveFile");
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
        || g_str_equal(method, "SaveFile") || g_str_equal(method, "SelectDirectory")) {
        open_request(service, sender, method, parameters, call); return;
    }
    if (g_str_equal(method, "RegisterBackend")) {
        guint uid = 0, pid = 0;
        char *executable = NULL;
        gboolean okay = credentials(service, sender, &uid, &pid)
            && uid == (service->test_mode ? getuid() : 0)
            && (executable = executable_for_pid(pid))
            && g_str_equal(executable, service->backend_executable);
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
            if (candidate->status == PENDING) { next = candidate; break; }
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
    if (argc != 1 && (argc != 4 || !g_str_equal(argv[1], "--test-root"))) {
        g_printerr("usage: openwave-portal [--test-root ROOT BACKEND_EXECUTABLE]\n");
        return 2;
    }
    Service service = { .requests = g_queue_new(),
        .root_prefix = argc == 4 ? argv[2] : "/",
        .backend_executable = argc == 4 ? argv[3] : "/usr/bin/ocean-launcher",
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
