#include <OpenWave/OpenWaveXml.h>
#include <OpenWave/OpenWaveIdentity.h>
#include <gio/gio.h>
#include <png.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

#define BUS_NAME "org.openwave.Wallpaper"
#define OBJECT_PATH "/org/openwave/Wallpaper"
#define INTERFACE "org.openwave.Wallpaper1"
#define WALLPAPER_DIR "/var/openwave/wallpaper"
#define MAX_PNG_BYTES (16 * 1024 * 1024)

typedef struct {
    GDBusConnection *bus;
    GMainLoop *loop;
    char *root;
} Service;

static char *at_root(Service *service, const char *absolute)
{
    return g_build_filename(service->root, absolute, NULL);
}

static char *user_directory(guint uid)
{
    return uid == 0 ? g_strdup(WALLPAPER_DIR)
        : g_strdup_printf(WALLPAPER_DIR "/users/%u", uid);
}

static gboolean caller_uid(Service *service, const char *sender, guint *uid)
{
    GError *error = NULL;
    GVariant *reply = g_dbus_connection_call_sync(service->bus,
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetConnectionUnixUser",
        g_variant_new("(s)", sender), G_VARIANT_TYPE("(u)"),
        G_DBUS_CALL_FLAGS_NONE, 3000, NULL, &error);
    if (!reply) { g_clear_error(&error); return FALSE; }
    g_variant_get(reply, "(u)", uid);
    g_variant_unref(reply);
    return TRUE;
}

static gboolean prepare_directory(Service *service, guint uid, const char *public_dir)
{
    char *parent = at_root(service, "/var/openwave");
    char *base = at_root(service, WALLPAPER_DIR);
    char *directory = at_root(service, public_dir);
    gboolean okay = g_mkdir_with_parents(directory, 0755) == 0;
    if (okay && g_str_equal(service->root, "/")) {
        okay = chown(parent, 0, 0) == 0 && chmod(parent, 0755) == 0
            && chown(base, 0, 0) == 0 && chmod(base, 0755) == 0;
        if (okay && uid != 0) {
            struct passwd *user = getpwuid(uid);
            char *users = at_root(service, WALLPAPER_DIR "/users");
            okay = user && chown(users, 0, 0) == 0 && chmod(users, 0755) == 0
                && chown(directory, uid, user->pw_gid) == 0
                && chmod(directory, 0700) == 0;
            g_free(users);
        }
    }
    g_free(parent); g_free(base); g_free(directory);
    return okay;
}

static gboolean valid_hash(const char *hash)
{
    if (!hash || strlen(hash) != 64) return FALSE;
    for (const char *p = hash; *p; ++p)
        if (!g_ascii_isxdigit(*p) || g_ascii_isupper(*p)) return FALSE;
    return TRUE;
}

static gboolean valid_png(const guint8 *bytes, gsize size)
{
    if (!bytes || size < 33 || size > MAX_PNG_BYTES) return FALSE;
    png_image image = { 0 };
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, bytes, size)) return FALSE;
    gboolean valid = image.width == 768 && image.height == 1366;
    if (valid) {
        image.format = PNG_FORMAT_RGBA;
        void *pixels = g_malloc(PNG_IMAGE_SIZE(image));
        valid = png_image_finish_read(&image, NULL, pixels, 0, NULL);
        g_free(pixels);
    }
    png_image_free(&image);
    return valid;
}

static gboolean atomic_write(const char *path, const void *bytes, gsize length, GError **error)
{
    char *temporary = g_strdup_printf("%s.tmp.%ld", path, (long) getpid());
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "%s", g_strerror(errno));
        g_free(temporary);
        return FALSE;
    }
    const guint8 *cursor = bytes;
    gsize remaining = length;
    while (remaining) {
        ssize_t written = write(fd, cursor, remaining);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) break;
        cursor += written;
        remaining -= written;
    }
    int saved_errno = remaining ? (errno ? errno : EIO) : 0;
    if (!saved_errno && fsync(fd) != 0) saved_errno = errno;
    if (close(fd) != 0 && !saved_errno) saved_errno = errno;
    if (!saved_errno && rename(temporary, path) != 0) saved_errno = errno;
    if (saved_errno) {
        unlink(temporary);
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved_errno),
                    "%s", g_strerror(saved_errno));
    }
    g_free(temporary);
    return saved_errno == 0;
}

static char *current_hash(Service *service, const char *public_dir)
{
    char *name = g_build_filename(public_dir, "current", NULL);
    char *marker = at_root(service, name);
    char *hash = NULL;
    if (g_file_get_contents(marker, &hash, NULL, NULL)) {
        g_strstrip(hash);
        if (valid_hash(hash)) { g_free(name); g_free(marker); return hash; }
    }
    g_free(name);
    g_free(marker);
    g_free(hash);
    return NULL;
}

static char *verified_path(Service *service, const char *public_dir, const char *hash)
{
    if (!valid_hash(hash)) return NULL;
    char *name = g_strdup_printf("%s/%s.png", public_dir, hash);
    char *path = at_root(service, name);
    struct stat info;
    char *bytes = NULL;
    gsize length = 0;
    gboolean okay = lstat(path, &info) == 0 && S_ISREG(info.st_mode)
        && info.st_size > 0 && info.st_size <= MAX_PNG_BYTES
        && g_file_get_contents(path, &bytes, &length, NULL);
    if (okay) {
        char *actual = g_compute_checksum_for_data(G_CHECKSUM_SHA256,
                                                    (const guint8 *) bytes, length);
        okay = g_str_equal(actual, hash) && valid_png((const guint8 *) bytes, length);
        g_free(actual);
    }
    g_free(bytes);
    g_free(path);
    if (!okay) { g_free(name); return NULL; }
    return name;
}

static void method_call(GDBusConnection *connection, const char *sender,
                        const char *object_path, const char *interface_name,
                        const char *method, GVariant *parameters,
                        GDBusMethodInvocation *invocation, gpointer data)
{
    (void) object_path; (void) interface_name;
    Service *service = data;
    guint uid = 0;
    if (!caller_uid(service, sender, &uid)) {
        g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".Unauthorized",
            "Could not verify the requesting user");
        return;
    }
    if (!strcmp(service->root, "/") && uid) {
        if (!strcmp(method, "SetWallpaper")) {
            g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".Unauthorized", "Only trusted system components may change the wallpaper"); return;
        }
        GVariant *identity = openwave_resolve_caller(service->bus, sender, NULL);
        if (!identity || !g_variant_lookup(identity, "userId", "u", &uid)) {
            if (identity) g_variant_unref(identity);
            g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".Unauthorized", "Application identity is unavailable"); return;
        }
        g_variant_unref(identity);
    }
    char *public_dir = user_directory(uid);
    if (!strcmp(method, "GetImage")) {
        char *hash = current_hash(service, public_dir), *path = verified_path(service, public_dir, hash), *bytes = NULL;
        gsize length = 0;
        if (path) g_file_get_contents(path, &bytes, &length, NULL);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(@ay)",
            g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, bytes, length, 1)));
        g_free(hash); g_free(path); g_free(bytes); g_free(public_dir); return;
    }
    if (g_str_equal(method, "GetCurrent")) {
        char *hash = current_hash(service, public_dir);
        char *path = verified_path(service, public_dir, hash);
        g_dbus_method_invocation_return_value(invocation,
            g_variant_new("(ss)", path ? path : "", path ? hash : ""));
        g_free(path);
        g_free(hash);
        g_free(public_dir);
        return;
    }
    if (!g_str_equal(method, "SetWallpaper")) { g_free(public_dir); return; }
    GVariant *array = g_variant_get_child_value(parameters, 0);
    gsize length = 0;
    const guint8 *bytes = g_variant_get_fixed_array(array, &length, sizeof(guint8));
    if (!valid_png(bytes, length)) {
        g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".InvalidImage",
            "Wallpaper must be a valid 768x1366 PNG under 16 MiB");
        g_variant_unref(array);
        g_free(public_dir);
        return;
    }
    char *hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes, length);
    char *directory = at_root(service, public_dir);
    char *name = g_strdup_printf("%s.png", hash);
    char *path = g_build_filename(directory, name, NULL);
    char *marker = g_build_filename(directory, "current", NULL);
    GError *error = NULL;
    gboolean okay = prepare_directory(service, uid, public_dir)
        && atomic_write(path, bytes, length, &error)
        && atomic_write(marker, hash, strlen(hash), &error);
    if (!okay) {
        g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".OperationFailed",
            error ? error->message : "Could not create wallpaper directory");
        g_clear_error(&error);
    } else {
        char *public_path = g_build_filename(public_dir, name, NULL);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", hash));
        g_dbus_connection_emit_signal(connection, NULL, OBJECT_PATH, INTERFACE,
            "Changed", g_variant_new("(ss)", public_path, hash), NULL);
        g_free(public_path);
    }
    g_free(hash); g_free(directory); g_free(name); g_free(path); g_free(marker);
    g_free(public_dir);
    g_variant_unref(array);
}

static const GDBusInterfaceVTable vtable = { .method_call = method_call };

static void bus_closed(GDBusConnection *bus, gboolean vanished, GError *error, gpointer data)
{
    (void) bus; (void) vanished; (void) error;
    g_main_loop_quit(((Service *) data)->loop);
}

int main(int argc, char **argv)
{
    if (argc != 1 && (argc != 3 || strcmp(argv[1], "--root") != 0)) {
        g_printerr("usage: wallpaperagentd [--root TEST_ROOT]\n");
        return 2;
    }
    Service service = { .root = argc == 3 ? argv[2] : "/" };
    GError *error = NULL;
    service.bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!service.bus) {
        g_printerr("wallpaperagentd: system bus unavailable: %s\n", error->message);
        return 1;
    }
    if (!prepare_directory(&service, 0, WALLPAPER_DIR)) {
        g_printerr("wallpaperagentd: cannot secure wallpaper storage\n");
        return 1;
    }
    GDBusNodeInfo *node = openwave_xml_load_dbus_interface(INTERFACE, &error);
    if (!node || !g_dbus_connection_register_object(service.bus, OBJECT_PATH,
            node->interfaces[0], &vtable, &service, NULL, &error)) {
        g_printerr("wallpaperagentd: registration failed: %s\n", error->message);
        return 1;
    }
    service.loop = g_main_loop_new(NULL, FALSE);
    g_signal_connect(service.bus, "closed", G_CALLBACK(bus_closed), &service);
    g_bus_own_name_on_connection(service.bus, BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
                                 NULL, NULL, NULL, NULL);
    g_main_loop_run(service.loop);
    return 0;
}
