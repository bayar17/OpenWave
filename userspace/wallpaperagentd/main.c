#include <OpenWave/OpenWaveXml.h>
#include <OpenWave/OpenWaveIdentity.h>
#include <gio/gio.h>
#include <png.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <math.h>
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
#define MAX_SOURCE_BYTES (64 * 1024 * 1024)
#define MAX_SOURCE_PIXELS (64u * 1000u * 1000u)
#define OUTPUT_WIDTH 768
#define OUTPUT_HEIGHT 1366
#define MAX_DESKTOP_BYTES (16 * 1024)
#define SYSTEM_APPEARANCES "/System/QuickMenu/Appearances"
#define APPEARANCE_DIR "/var/openwave/appearances"
#define MAX_APPEARANCES 32
#define MAX_NAME_CHARS 32

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

/* Ocean is supervised by OpenRC and deliberately absent from appd's app
 * registry. Authenticate its process proof, including calls on another bus
 * connection in that same process. This permits wallpaper reads only. */
static gboolean supervised_ocean(Service *service, const char *sender, guint uid)
{
    struct passwd *ocean = getpwnam("ocean");
    if (!ocean || uid != ocean->pw_uid) return FALSE;
    uid_t ocean_uid = ocean->pw_uid;
    GVariant *reply = g_dbus_connection_call_sync(service->bus,
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "GetConnectionCredentials", g_variant_new("(s)", sender),
        G_VARIANT_TYPE("(a{sv})"), 0, 1000, NULL, NULL);
    if (!reply) return FALSE;
    GVariant *fields = g_variant_get_child_value(reply, 0);
    guint pid = 0, credential_uid = 0;
    gboolean okay = g_variant_lookup(fields, "UnixUserID", "u", &credential_uid)
        && credential_uid == uid && g_variant_lookup(fields, "ProcessID", "u", &pid)
        && openwave_supervised_process_uid(uid, pid, "/run/openwave/ocean.pid", ocean_uid);
    g_variant_unref(fields); g_variant_unref(reply);
    return okay;
}

static gboolean settings_identity(GVariant *identity)
{
    const char *app = NULL; gboolean trusted = FALSE;
    return identity && g_variant_lookup(identity, "appId", "&s", &app)
        && !strcmp(app, "com.openwave.settings")
        && g_variant_lookup(identity, "trusted", "b", &trusted) && trusted;
}

static gboolean appearance_caller(gboolean ocean, GVariant *identity)
{
    return ocean || settings_identity(identity);
}

static gboolean valid_appearance_id(const char *id)
{
    if (!id || !g_ascii_islower(id[0])) return FALSE;
    gsize length = strlen(id);
    if (length > 32) return FALSE;
    for (gsize i = 0; i < length; ++i)
        if (!g_ascii_islower(id[i]) && !g_ascii_isdigit(id[i]) && id[i] != '-') return FALSE;
    return TRUE;
}

static gboolean valid_plain_file_name(const char *name)
{
    if (!name || !*name || name[0] == '.' || strlen(name) > 128) return FALSE;
    for (const char *p = name; *p; ++p)
        if (*p == '/' || *p == '\\' || (guchar) *p < 0x20 || *p == 0x7f) return FALSE;
    return TRUE;
}

static gboolean read_regular_file(const char *path, gsize limit, guint8 **contents, gsize *length)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return FALSE;
    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size <= 0
        || (gsize) info.st_size > limit) {
        close(fd);
        return FALSE;
    }
    guint8 *buffer = g_malloc((gsize) info.st_size);
    gsize done = 0;
    while (done < (gsize) info.st_size) {
        ssize_t count = read(fd, buffer + done, (gsize) info.st_size - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        done += (gsize) count;
    }
    close(fd);
    if (done != (gsize) info.st_size) { g_free(buffer); return FALSE; }
    *contents = buffer;
    *length = done;
    return TRUE;
}

static char *desktop_value(const guint8 *text, gsize length, const char *wanted, gboolean *appearance_type)
{
    char *copy = g_strndup((const char *) text, length);
    char **lines = g_strsplit(copy, "\n", -1);
    gboolean in_entry = FALSE, appearance = FALSE;
    char *found = NULL;
    for (char **line = lines; *line; ++line) {
        char *trimmed = g_strstrip(*line);
        if (!*trimmed || trimmed[0] == '#') continue;
        if (trimmed[0] == '[') { in_entry = g_str_equal(trimmed, "[Desktop Entry]"); continue; }
        if (!in_entry) continue;
        char *separator = strchr(trimmed, '=');
        if (!separator) continue;
        *separator = '\0';
        char *key = g_strstrip(trimmed), *value = g_strstrip(separator + 1);
        if (g_str_equal(key, "Type")) appearance = g_str_equal(value, "X-OpenWave-Appearance");
        else if (g_str_equal(key, wanted) && !found) found = g_strdup(value);
    }
    g_strfreev(lines);
    g_free(copy);
    if (appearance_type) *appearance_type = appearance;
    return found;
}

static char *appearance_wallpaper_name(const guint8 *text, gsize length)
{
    gboolean appearance = FALSE;
    char *name = desktop_value(text, length, "X-OpenWave-Appearance-Wallpaper", &appearance);
    if (!appearance || !valid_plain_file_name(name)) { g_free(name); return NULL; }
    return name;
}

static gboolean appearance_find_in(Service *service, const char *directory, const char *id,
                                   guint8 **bytes, gsize *length, gboolean *found)
{
    *bytes = NULL;
    *length = 0;
    *found = FALSE;
    char *desktop_name = g_strdup_printf("%s.desktop", id);
    char *desktop_path = at_root(service, directory);
    char *desktop_file = g_build_filename(desktop_path, desktop_name, NULL);
    guint8 *text = NULL;
    gsize text_length = 0;
    gboolean okay = FALSE;
    if (read_regular_file(desktop_file, MAX_DESKTOP_BYTES, &text, &text_length)) {
        gboolean appearance = FALSE;
        char *wallpaper_key = desktop_value(text, text_length, "X-OpenWave-Appearance-Wallpaper", &appearance);
        if (appearance) {
            *found = TRUE;
            if (!wallpaper_key) {
                okay = TRUE;
            } else if (valid_plain_file_name(wallpaper_key)) {
                char *image_file = g_build_filename(desktop_path, wallpaper_key, NULL);
                okay = read_regular_file(image_file, MAX_SOURCE_BYTES, bytes, length);
                g_free(image_file);
            }
        }
        g_free(wallpaper_key);
    }
    g_free(text); g_free(desktop_file); g_free(desktop_path); g_free(desktop_name);
    return okay;
}

static char *appearance_store(guint uid)
{
    return g_strdup_printf(APPEARANCE_DIR "/users/%u", uid);
}

static gboolean appearance_png(Service *service, guint uid, const char *id,
                               guint8 **bytes, gsize *length, gboolean *found)
{
    *found = FALSE;
    if (!valid_appearance_id(id)) return FALSE;
    if (appearance_find_in(service, SYSTEM_APPEARANCES, id, bytes, length, found)) return TRUE;
    if (*found) return FALSE;
    char *directory = appearance_store(uid);
    gboolean okay = appearance_find_in(service, directory, id, bytes, length, found);
    g_free(directory);
    return okay;
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
            char *users = at_root(service, WALLPAPER_DIR "/users");
            struct stat info;
            /* Persisted selections belong to the broker, keyed by the
             * authenticated session UID. Clients receive verified bytes. */
            okay = chown(users, 0, 0) == 0 && chmod(users, 0755) == 0
                && !lstat(directory, &info) && S_ISDIR(info.st_mode)
                && info.st_uid == 0 && chmod(directory, 0700) == 0;
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

typedef struct {
    guint width;
    guint height;
    guint8 *rgb;
} Raster;

static void raster_free(Raster *raster)
{
    g_free(raster->rgb);
    raster->rgb = NULL;
}

static gboolean decode_png_raster(const guint8 *bytes, gsize size, Raster *raster)
{
    png_image image = { 0 };
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, bytes, size)) return FALSE;
    gboolean okay = image.width > 0 && image.height > 0
        && (guint64) image.width * image.height <= MAX_SOURCE_PIXELS;
    if (okay) {
        image.format = PNG_FORMAT_RGBA;
        const gsize pixels = (gsize) image.width * image.height;
        guint8 *rgba = g_try_malloc(pixels * 4);
        okay = rgba && png_image_finish_read(&image, NULL, rgba, 0, NULL);
        if (okay) {
            for (gsize i = 0; i < pixels; ++i) {
                const guint alpha = rgba[i * 4 + 3];
                for (int channel = 0; channel < 3; ++channel)
                    rgba[i * 3 + channel] = (guint8) ((rgba[i * 4 + channel] * alpha + 127) / 255);
            }
            raster->width = image.width;
            raster->height = image.height;
            raster->rgb = g_try_realloc(rgba, pixels * 3);
            if (!raster->rgb) { g_free(rgba); okay = FALSE; }
        } else {
            g_free(rgba);
        }
    }
    png_image_free(&image);
    return okay;
}

typedef struct {
    struct jpeg_error_mgr manager;
    jmp_buf jump;
} JpegErrors;

static void jpeg_fail(j_common_ptr cinfo)
{
    longjmp(((JpegErrors *) cinfo->err)->jump, 1);
}

static void jpeg_quiet(j_common_ptr cinfo, int level)
{
    (void) cinfo; (void) level;
}

static gboolean decode_jpeg_raster(const guint8 *bytes, gsize size, Raster *raster)
{
    struct jpeg_decompress_struct cinfo;
    JpegErrors errors;
    cinfo.err = jpeg_std_error(&errors.manager);
    errors.manager.error_exit = jpeg_fail;
    errors.manager.emit_message = jpeg_quiet;
    raster->rgb = NULL;
    if (setjmp(errors.jump)) {
        jpeg_destroy_decompress(&cinfo);
        raster_free(raster);
        return FALSE;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, bytes, size);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK
        || cinfo.image_width == 0 || cinfo.image_height == 0
        || (guint64) cinfo.image_width * cinfo.image_height > MAX_SOURCE_PIXELS
        || (cinfo.jpeg_color_space != JCS_GRAYSCALE && cinfo.jpeg_color_space != JCS_RGB
            && cinfo.jpeg_color_space != JCS_YCbCr)) {
        jpeg_destroy_decompress(&cinfo);
        return FALSE;
    }
    cinfo.out_color_space = JCS_RGB;
    raster->width = cinfo.image_width;
    raster->height = cinfo.image_height;
    raster->rgb = g_try_malloc((gsize) raster->width * raster->height * 3);
    if (!raster->rgb) {
        jpeg_destroy_decompress(&cinfo);
        return FALSE;
    }
    jpeg_start_decompress(&cinfo);
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = raster->rgb + (gsize) cinfo.output_scanline * raster->width * 3;
        if (jpeg_read_scanlines(&cinfo, &row, 1) != 1) break;
    }
    gboolean complete = cinfo.output_scanline == cinfo.output_height;
    if (complete) jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    if (!complete) raster_free(raster);
    return complete;
}

static gboolean decode_raster(const guint8 *bytes, gsize size, Raster *raster)
{
    if (!bytes || size < 8 || size > MAX_SOURCE_BYTES) return FALSE;
    memset(raster, 0, sizeof(*raster));
    if (!png_sig_cmp(bytes, 0, 8)) return decode_png_raster(bytes, size, raster);
    if (bytes[0] == 0xff && bytes[1] == 0xd8 && bytes[2] == 0xff) return decode_jpeg_raster(bytes, size, raster);
    return FALSE;
}

static void sample_pixel(const Raster *source, double x, double y, double *color)
{
    double fx = x - 0.5, fy = y - 0.5;
    int x0 = (int) floor(fx), y0 = (int) floor(fy);
    double tx = fx - x0, ty = fy - y0;
    for (int channel = 0; channel < 3; ++channel) {
        double total = 0;
        for (int dy = 0; dy < 2; ++dy) {
            for (int dx = 0; dx < 2; ++dx) {
                int px = CLAMP(x0 + dx, 0, (int) source->width - 1);
                int py = CLAMP(y0 + dy, 0, (int) source->height - 1);
                double weight = (dx ? tx : 1 - tx) * (dy ? ty : 1 - ty);
                total += weight * source->rgb[((gsize) py * source->width + (guint) px) * 3 + channel];
            }
        }
        color[channel] = total;
    }
}

static void sample_area(const Raster *source, double left, double top, double right, double bottom,
                        double *color)
{
    if (right - left <= 1.0 && bottom - top <= 1.0) {
        sample_pixel(source, (left + right) / 2, (top + bottom) / 2, color);
        return;
    }
    int first_x = MAX(0, (int) floor(left)), last_x = MIN((int) source->width - 1, (int) ceil(right) - 1);
    int first_y = MAX(0, (int) floor(top)), last_y = MIN((int) source->height - 1, (int) ceil(bottom) - 1);
    double sum[3] = { 0, 0, 0 }, area = 0;
    for (int y = first_y; y <= last_y; ++y) {
        double height = MIN(bottom, y + 1.0) - MAX(top, (double) y);
        if (height <= 0) continue;
        const guint8 *row = source->rgb + (gsize) y * source->width * 3;
        for (int x = first_x; x <= last_x; ++x) {
            double width = MIN(right, x + 1.0) - MAX(left, (double) x);
            if (width <= 0) continue;
            double weight = width * height;
            sum[0] += weight * row[x * 3];
            sum[1] += weight * row[x * 3 + 1];
            sum[2] += weight * row[x * 3 + 2];
            area += weight;
        }
    }
    for (int channel = 0; channel < 3; ++channel) color[channel] = area > 0 ? sum[channel] / area : 0;
}

static guint8 *cover_crop(const Raster *source)
{
    double scale = MAX((double) OUTPUT_WIDTH / source->width, (double) OUTPUT_HEIGHT / source->height);
    double window_width = OUTPUT_WIDTH / scale, window_height = OUTPUT_HEIGHT / scale;
    double offset_x = (source->width - window_width) / 2, offset_y = (source->height - window_height) / 2;
    guint8 *output = g_malloc((gsize) OUTPUT_WIDTH * OUTPUT_HEIGHT * 3);
    double step = 1.0 / scale;
    for (int y = 0; y < OUTPUT_HEIGHT; ++y) {
        for (int x = 0; x < OUTPUT_WIDTH; ++x) {
            double color[3];
            sample_area(source, offset_x + x * step, offset_y + y * step,
                        offset_x + (x + 1) * step, offset_y + (y + 1) * step, color);
            guint8 *pixel = output + ((gsize) y * OUTPUT_WIDTH + x) * 3;
            for (int channel = 0; channel < 3; ++channel)
                pixel[channel] = (guint8) CLAMP((int) lround(color[channel]), 0, 255);
        }
    }
    return output;
}

static gboolean encode_png(const guint8 *rgb, guint8 **bytes, gsize *length)
{
    png_image image = { 0 };
    image.version = PNG_IMAGE_VERSION;
    image.width = OUTPUT_WIDTH;
    image.height = OUTPUT_HEIGHT;
    image.format = PNG_FORMAT_RGB;
    png_alloc_size_t needed = 0;
    if (!png_image_write_to_memory(&image, NULL, &needed, 0, rgb, 0, NULL) || !needed) {
        png_image_free(&image);
        return FALSE;
    }
    guint8 *buffer = g_try_malloc(needed);
    if (!buffer) { png_image_free(&image); return FALSE; }
    png_alloc_size_t actual = needed;
    gboolean okay = png_image_write_to_memory(&image, buffer, &actual, 0, rgb, 0, NULL);
    png_image_free(&image);
    if (!okay) { g_free(buffer); return FALSE; }
    *bytes = buffer;
    *length = actual;
    return TRUE;
}

static gboolean normalize_wallpaper(const guint8 *bytes, gsize length, guint8 **normalized, gsize *normalized_length)
{
    *normalized = NULL;
    *normalized_length = 0;
    if (valid_png(bytes, length)) {
        *normalized = g_memdup2(bytes, length);
        *normalized_length = length;
        return TRUE;
    }
    Raster raster;
    if (!decode_raster(bytes, length, &raster)) return FALSE;
    guint8 *cropped = cover_crop(&raster);
    raster_free(&raster);
    gboolean okay = encode_png(cropped, normalized, normalized_length);
    g_free(cropped);
    return okay;
}

static gboolean valid_appearance_name(const char *name)
{
    if (!name || !g_utf8_validate(name, -1, NULL)) return FALSE;
    glong characters = g_utf8_strlen(name, -1);
    if (characters < 1 || characters > MAX_NAME_CHARS) return FALSE;
    for (const char *p = name; *p; ++p)
        if ((guchar) *p < 0x20 || *p == 0x7f) return FALSE;
    char *copy = g_strdup(name);
    gboolean blank = g_str_equal(g_strstrip(copy), "");
    g_free(copy);
    return !blank;
}

static char *desktop_escape(const char *value)
{
    GString *out = g_string_new(NULL);
    for (const char *p = value; *p; ++p) {
        if (*p == '\\') g_string_append(out, "\\\\");
        else g_string_append_c(out, *p);
    }
    return g_string_free(out, FALSE);
}

static char *appearance_slug(const char *name)
{
    GString *slug = g_string_new(NULL);
    gboolean separator = FALSE;
    for (const char *p = name; *p && slug->len < 24; ++p) {
        if (g_ascii_isalnum(*p)) {
            if (separator && slug->len) g_string_append_c(slug, '-');
            separator = FALSE;
            g_string_append_c(slug, g_ascii_tolower(*p));
        } else {
            separator = TRUE;
        }
    }
    if (!slug->len || !g_ascii_islower(slug->str[0])) g_string_prepend(slug, "a");
    return g_string_free(slug, FALSE);
}

static gboolean appearance_exists(Service *service, const char *directory, const char *id)
{
    char *base = at_root(service, directory);
    char *name = g_strdup_printf("%s.desktop", id);
    char *path = g_build_filename(base, name, NULL);
    struct stat info;
    gboolean exists = lstat(path, &info) == 0;
    g_free(path); g_free(name); g_free(base);
    return exists;
}

static guint count_appearances(const char *directory_path)
{
    guint count = 0;
    GDir *directory = g_dir_open(directory_path, 0, NULL);
    if (!directory) return 0;
    const char *entry;
    while ((entry = g_dir_read_name(directory)))
        if (g_str_has_suffix(entry, ".desktop")) ++count;
    g_dir_close(directory);
    return count;
}

static gboolean prepare_appearance_directory(Service *service, const char *store)
{
    char *directory = at_root(service, store);
    gboolean okay = g_mkdir_with_parents(directory, 0755) == 0;
    if (okay && g_str_equal(service->root, "/")) {
        char *parent = at_root(service, "/var/openwave");
        char *base = at_root(service, APPEARANCE_DIR);
        char *users = at_root(service, APPEARANCE_DIR "/users");
        okay = chown(parent, 0, 0) == 0 && chmod(parent, 0755) == 0
            && chown(base, 0, 0) == 0 && chmod(base, 0755) == 0
            && chown(users, 0, 0) == 0 && chmod(users, 0755) == 0
            && chown(directory, 0, 0) == 0 && chmod(directory, 0755) == 0;
        g_free(parent); g_free(base); g_free(users);
    }
    g_free(directory);
    return okay;
}

static gboolean save_appearance(Service *service, guint uid, const char *requested_id, const char *name,
                                double hue, const guint8 *png, gsize png_length,
                                char **saved_id, const char **error_name, const char **error_text)
{
    *saved_id = NULL;
    if (!valid_appearance_name(name)) {
        *error_name = ".InvalidAppearance"; *error_text = "The name must be 1 to 32 characters without control characters"; return FALSE;
    }
    if (!(hue >= 0.0 && hue <= 1.0)) {
        *error_name = ".InvalidAppearance"; *error_text = "The hue must be between 0 and 1"; return FALSE;
    }
    char *store = appearance_store(uid);
    char *directory = at_root(service, store);
    gboolean creating = !requested_id || !*requested_id;
    char *id = NULL, *order_text = NULL;
    guint8 *normalized = NULL;
    gsize normalized_length = 0;
    gboolean okay = FALSE;
    if (!creating && !valid_appearance_id(requested_id)) {
        *error_name = ".InvalidAppearance"; *error_text = "The appearance id is invalid"; goto done;
    }
    if (!creating && !appearance_exists(service, store, requested_id)) {
        *error_name = ".InvalidAppearance"; *error_text = "The appearance does not exist"; goto done;
    }
    if (creating && count_appearances(directory) >= MAX_APPEARANCES) {
        *error_name = ".InvalidAppearance"; *error_text = "Too many appearances"; goto done;
    }
    if (creating && !png_length) {
        *error_name = ".InvalidImage"; *error_text = "A wallpaper image is required"; goto done;
    }
    if (png_length && !normalize_wallpaper(png, png_length, &normalized, &normalized_length)) {
        *error_name = ".InvalidImage"; *error_text = "The image must be a PNG or JPEG of at most 64 megapixels and 64 MiB"; goto done;
    }
    if (!prepare_appearance_directory(service, store)) {
        *error_name = ".OperationFailed"; *error_text = "Could not create the appearance directory"; goto done;
    }
    if (creating) {
        char *base = appearance_slug(name);
        id = g_strdup(base);
        for (guint suffix = 2; (appearance_exists(service, store, id) || appearance_exists(service, SYSTEM_APPEARANCES, id)) && suffix < 1000; ++suffix) {
            g_free(id);
            id = g_strdup_printf("%.24s-%u", base, suffix);
        }
        g_free(base);
        guint highest = 1000;
        GDir *entries = g_dir_open(directory, 0, NULL);
        const char *entry;
        while (entries && (entry = g_dir_read_name(entries))) {
            if (!g_str_has_suffix(entry, ".desktop")) continue;
            char *file = g_build_filename(directory, entry, NULL);
            guint8 *text = NULL; gsize length = 0;
            if (read_regular_file(file, MAX_DESKTOP_BYTES, &text, &length)) {
                char *value = desktop_value(text, length, "X-OpenWave-Appearance-Order", NULL);
                if (value) highest = MAX(highest, (guint) g_ascii_strtoull(value, NULL, 10));
                g_free(value); g_free(text);
            }
            g_free(file);
        }
        if (entries) g_dir_close(entries);
        order_text = g_strdup_printf("%u", highest + 10);
    } else {
        id = g_strdup(requested_id);
        char *name_file = g_strdup_printf("%s.desktop", id);
        char *existing = g_build_filename(directory, name_file, NULL);
        guint8 *text = NULL; gsize length = 0;
        if (read_regular_file(existing, MAX_DESKTOP_BYTES, &text, &length)) {
            order_text = desktop_value(text, length, "X-OpenWave-Appearance-Order", NULL);
            g_free(text);
        }
        g_free(existing); g_free(name_file);
        if (!order_text) order_text = g_strdup("1000");
    }
    char *wallpaper_name = g_strdup_printf("%s-wallpaper.png", id);
    char *wallpaper_path = g_build_filename(directory, wallpaper_name, NULL);
    char *desktop_name = g_strdup_printf("%s.desktop", id);
    char *desktop_path = g_build_filename(directory, desktop_name, NULL);
    char *escaped = desktop_escape(name);
    char *text = g_strdup_printf("[Desktop Entry]\nType=X-OpenWave-Appearance\nName=%s\n"
        "X-OpenWave-Appearance-Order=%s\nX-OpenWave-Appearance-Hue=%.4f\n"
        "X-OpenWave-Appearance-Wallpaper=%s\n", escaped, order_text, hue, wallpaper_name);
    GError *error = NULL;
    okay = (!png_length || atomic_write(wallpaper_path, normalized, normalized_length, &error))
        && atomic_write(desktop_path, text, strlen(text), &error);
    if (!okay) {
        *error_name = ".OperationFailed"; *error_text = "Could not save the appearance";
        g_clear_error(&error);
    } else {
        *saved_id = id; id = NULL;
    }
    g_free(text); g_free(escaped); g_free(desktop_path); g_free(desktop_name);
    g_free(wallpaper_path); g_free(wallpaper_name);
done:
    g_free(normalized);
    g_free(id); g_free(order_text); g_free(directory); g_free(store);
    return okay;
}

static gboolean delete_appearance(Service *service, guint uid, const char *id,
                                  const char **error_name, const char **error_text)
{
    if (!valid_appearance_id(id)) {
        *error_name = ".InvalidAppearance"; *error_text = "The appearance id is invalid"; return FALSE;
    }
    char *store = appearance_store(uid);
    char *directory = at_root(service, store);
    char *desktop_name = g_strdup_printf("%s.desktop", id);
    char *desktop_path = g_build_filename(directory, desktop_name, NULL);
    guint8 *text = NULL; gsize length = 0;
    gboolean okay = read_regular_file(desktop_path, MAX_DESKTOP_BYTES, &text, &length);
    if (!okay) {
        *error_name = ".InvalidAppearance"; *error_text = "The appearance does not exist";
    } else {
        char *wallpaper = appearance_wallpaper_name(text, length);
        okay = unlink(desktop_path) == 0;
        if (okay && wallpaper) {
            char *wallpaper_path = g_build_filename(directory, wallpaper, NULL);
            unlink(wallpaper_path);
            g_free(wallpaper_path);
        }
        if (!okay) { *error_name = ".OperationFailed"; *error_text = "Could not delete the appearance"; }
        g_free(wallpaper);
    }
    g_free(text); g_free(desktop_path); g_free(desktop_name); g_free(directory); g_free(store);
    return okay;
}

static void store_wallpaper(Service *service, GDBusConnection *connection,
                            GDBusMethodInvocation *invocation, guint uid, const char *public_dir,
                            const guint8 *bytes, gsize length)
{
    guint8 *normalized = NULL;
    gsize normalized_length = 0;
    if (!normalize_wallpaper(bytes, length, &normalized, &normalized_length)) {
        g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".InvalidImage",
            "The image must be a PNG or JPEG of at most 64 megapixels and 64 MiB");
        return;
    }
    bytes = normalized;
    length = normalized_length;
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
        g_message("wallpaper: updated user=%u sha256=%s", uid, hash);
        g_dbus_connection_emit_signal(connection, NULL, OBJECT_PATH, INTERFACE,
            "Changed", g_variant_new("(ss)", public_path, hash), NULL);
        g_free(public_path);
    }
    g_free(hash); g_free(directory); g_free(name); g_free(path); g_free(marker);
    g_free(normalized);
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
        gboolean ocean = supervised_ocean(service, sender, uid);
        GVariant *identity = ocean ? NULL : openwave_resolve_caller(service->bus, sender, NULL);
        if (ocean) {
            /* Retain the authenticated session UID for its wallpaper. */
        } else if (!identity || !g_variant_lookup(identity, "userId", "u", &uid)) {
            if (identity) g_variant_unref(identity);
            g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".Unauthorized", "Application identity is unavailable"); return;
        }
        if (!strcmp(method, "SetWallpaper")) {
            if (!settings_identity(identity)) {
                if (identity) g_variant_unref(identity);
                g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".Unauthorized",
                    "Only the managed Settings application may change the user's wallpaper"); return;
            }
        }
        if (!strcmp(method, "SaveAppearance") || !strcmp(method, "DeleteAppearance")) {
            if (!settings_identity(identity)) {
                if (identity) g_variant_unref(identity);
                g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".Unauthorized",
                    "Only the managed Settings application may change appearances"); return;
            }
        }
        if (!strcmp(method, "ApplyAppearance")) {
            if (!appearance_caller(ocean, identity)) {
                if (identity) g_variant_unref(identity);
                g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".Unauthorized",
                    "Only the shell or the managed Settings application may apply an appearance"); return;
            }
        }
        if (identity) g_variant_unref(identity);
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
    if (g_str_equal(method, "SaveAppearance")) {
        const char *id = NULL, *name = NULL;
        double hue = 0;
        GVariant *array = NULL;
        g_variant_get(parameters, "(&s&sd@ay)", &id, &name, &hue, &array);
        gsize length = 0;
        const guint8 *bytes = g_variant_get_fixed_array(array, &length, sizeof(guint8));
        char *saved = NULL;
        const char *error_name = ".OperationFailed", *error_text = "";
        if (save_appearance(service, uid, id, name, hue, bytes, length, &saved, &error_name, &error_text)) {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", saved));
            g_message("wallpaper: saved appearance user=%u id=%s", uid, saved);
        } else {
            char *full = g_strconcat(BUS_NAME, error_name, NULL);
            g_dbus_method_invocation_return_dbus_error(invocation, full, error_text);
            g_free(full);
        }
        g_free(saved);
        g_variant_unref(array);
        g_free(public_dir);
        return;
    }
    if (g_str_equal(method, "DeleteAppearance")) {
        const char *id = NULL;
        g_variant_get(parameters, "(&s)", &id);
        const char *error_name = ".OperationFailed", *error_text = "";
        if (delete_appearance(service, uid, id, &error_name, &error_text)) {
            g_dbus_method_invocation_return_value(invocation, NULL);
            g_message("wallpaper: deleted appearance user=%u id=%s", uid, id);
        } else {
            char *full = g_strconcat(BUS_NAME, error_name, NULL);
            g_dbus_method_invocation_return_dbus_error(invocation, full, error_text);
            g_free(full);
        }
        g_free(public_dir);
        return;
    }
    if (g_str_equal(method, "ApplyAppearance")) {
        const char *id = NULL;
        g_variant_get(parameters, "(&s)", &id);
        guint8 *bytes = NULL;
        gsize length = 0;
        gboolean found = FALSE;
        if (!appearance_png(service, uid, id, &bytes, &length, &found)) {
            g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".InvalidAppearance",
                found ? "The appearance has no usable PNG or JPEG wallpaper" : "The appearance is not installed");
            g_free(bytes);
            g_free(public_dir);
            return;
        }
        guint8 *prepared = NULL;
        gsize prepared_length = 0;
        if (bytes && !normalize_wallpaper(bytes, length, &prepared, &prepared_length)) {
            g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".InvalidImage",
                "The image must be a PNG or JPEG of at most 64 megapixels and 64 MiB");
            g_free(bytes);
            g_free(public_dir);
            return;
        }
        g_free(bytes);
        bytes = prepared;
        length = prepared_length;
        char *store = appearance_store(uid);
        char *directory = at_root(service, store);
        char *marker = g_build_filename(directory, "current", NULL);
        GError *marker_error = NULL;
        gboolean recorded = prepare_appearance_directory(service, store)
            && atomic_write(marker, id, strlen(id), &marker_error);
        g_clear_error(&marker_error);
        g_free(marker); g_free(directory); g_free(store);
        if (!recorded) {
            g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".OperationFailed",
                "Could not record the current appearance");
        } else if (!bytes) {
            g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", ""));
            g_message("wallpaper: selected appearance user=%u id=%s", uid, id);
        } else {
            store_wallpaper(service, connection, invocation, uid, public_dir, bytes, length);
        }
        g_free(bytes);
        g_free(public_dir);
        return;
    }
    if (!g_str_equal(method, "SetWallpaper")) { g_free(public_dir); return; }
    GVariant *array = g_variant_get_child_value(parameters, 0);
    gsize length = 0;
    const guint8 *bytes = g_variant_get_fixed_array(array, &length, sizeof(guint8));
    store_wallpaper(service, connection, invocation, uid, public_dir, bytes, length);
    g_variant_unref(array);
    g_free(public_dir);
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
