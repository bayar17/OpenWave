#define _GNU_SOURCE
#include "appd.h"
#include "user-folders.h"
#include "OpenWaveSELinux.h"
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/landlock.h>
#include <pwd.h>
#include <grp.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

static gboolean migrate(int directory, uid_t uid)
{
    DIR *dir = fdopendir(dup(directory));
    if (!dir) return FALSE;
    struct dirent *entry; gboolean okay = TRUE;
    while (okay && (entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        struct stat st;
        if (fstatat(directory, entry->d_name, &st, AT_SYMLINK_NOFOLLOW)) { okay = FALSE; break; }
        if (S_ISLNK(st.st_mode)) continue;
        int fd = openat(directory, entry->d_name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0) { okay = FALSE; break; }
        if (fstat(fd, &st) || (!S_ISDIR(st.st_mode) && (!S_ISREG(st.st_mode) || st.st_nlink != 1))) okay = FALSE;
        if (okay && S_ISDIR(st.st_mode)) okay = migrate(fd, uid);
        if (okay) okay = !fchown(fd, uid, uid) && !fchmod(fd, st.st_mode & 0700);
        close(fd);
    }
    closedir(dir); return okay;
}

static gboolean update_account_home(uid_t uid, const char *data, GError **error)
{
    if (!appd_verified_file("/etc/passwd", FALSE, error)) return FALSE;
    char *contents = NULL; gsize length;
    if (!g_file_get_contents("/etc/passwd", &contents, &length, error)) return FALSE;
    if (length > 1048576) { g_free(contents); return FALSE; }
    char **lines = g_strsplit(contents, "\n", -1); g_free(contents);
    GString *updated = g_string_new(NULL); gboolean found = FALSE, changed = FALSE;
    for (guint i = 0; lines[i]; ++i) {
        if (!*lines[i]) continue;
        char **fields = g_strsplit(lines[i], ":", -1);
        if (g_strv_length(fields) == 7 && g_ascii_strtoull(fields[2], NULL, 10) == uid) {
            found = TRUE;
            if (g_strcmp0(fields[5], data)) { g_free(fields[5]); fields[5] = g_strdup(data); changed = TRUE; }
        }
        char *line = g_strjoinv(":", fields); g_string_append_printf(updated, "%s\n", line);
        g_free(line); g_strfreev(fields);
    }
    g_strfreev(lines);
    if (!found) { g_string_append_printf(updated, "owapp%u:x:%u:%u:OpenWave application:%s:/bin/false\n", uid, uid, uid, data); changed = TRUE; }
    gboolean okay = !changed || g_file_set_contents_full("/etc/passwd", updated->str, updated->len,
        G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0644, error);
    g_string_free(updated, TRUE); return okay;
}

static uid_t provision_uid(Manager *m, App *app, const char *data, GError **error)
{
    const char *directory = "/var/lib/openwave/app-identities";
    if (g_mkdir_with_parents(directory, 0700)) goto failure;
    char *path = g_build_filename(directory, "users.ini", NULL);
    GKeyFile *map = g_key_file_new();
    struct stat st;
    if (!lstat(path, &st)) {
        if (!S_ISREG(st.st_mode) || st.st_uid || (st.st_mode & 0077)
            || !g_key_file_load_from_file(map, path, 0, error)) goto invalid;
    } else if (errno != ENOENT) goto invalid;
    char *key = g_strdup_printf("%u:%s", m->uid, app->id);
    uid_t uid = g_key_file_get_uint64(map, "Applications", key, NULL);
    const char *legacy = appd_legacy_id(app);
    if (!uid && legacy) {
        char *old_key = g_strdup_printf("%u:%s", m->uid, legacy);
        uid = g_key_file_get_uint64(map, "Applications", old_key, NULL);
        if (uid && uid >= 200000 && uid < 300000) {
            g_key_file_set_uint64(map, "Applications", key, uid);
            g_key_file_remove_key(map, "Applications", old_key, NULL);
            gsize size; char *contents = g_key_file_to_data(map, &size, NULL);
            gboolean okay = g_file_set_contents_full(path, contents, size,
                G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
            g_free(contents);
            if (!okay) { g_free(old_key); g_free(key); goto invalid; }
        }
        g_free(old_key);
    }
    if (uid && (uid < 200000 || uid >= 300000)) { g_free(key); goto invalid; }
    if (uid && !update_account_home(uid, data, error)) { g_free(key); goto invalid; }
    if (!uid) {
        for (uid = 200000; uid < 300000; ++uid) if (!getpwuid(uid)) break;
        if (uid == 300000) { g_free(key); goto invalid; }
        char *account = g_strdup_printf("owapp%u", uid);
        int fd = open("/etc/passwd", O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0 || flock(fd, LOCK_EX) || dprintf(fd, "%s:x:%u:%u:OpenWave application:%s:/bin/false\n", account, uid, uid, data) < 0
            || fsync(fd)) {
            if (fd >= 0) close(fd); g_free(account); g_free(key); goto invalid;
        }
        close(fd); g_free(account);
        g_key_file_set_uint64(map, "Applications", key, uid);
        gsize length; char *contents = g_key_file_to_data(map, &length, NULL);
        gboolean saved = g_file_set_contents_full(path, contents, length,
            G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
        g_free(contents);
        if (!saved || chmod(path, 0600)) { g_free(key); goto invalid; }
    }
    g_free(key); g_free(path); g_key_file_unref(map); return uid;
invalid:
    g_free(path); g_key_file_unref(map);
failure:
    if (error && !*error) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED, "Cannot provision a protected application identity");
    return 0;
}

gboolean appd_prepare_isolation(Manager *m, Instance *instance, GError **error)
{
    instance->data = g_strdup_printf("%s/.DataLocation/Applications/%s", m->home, instance->app->id);
    if (!appd_prepare_storage(m, instance, error)) {
        if (error && !*error) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Cannot prepare safe application storage");
        return FALSE;
    }
    if (appd_trusted_app(instance->app)) {
        instance->uid = m->uid; instance->runtime = g_strdup(m->runtime);
        return g_mkdir_with_parents(instance->data, 0700) == 0;
    }
    if (!m->cgroup_root || syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION) < 6) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Application isolation requires cgroup v2 and Landlock ABI 6 or newer"); return FALSE;
    }
    if (g_mkdir_with_parents(instance->data, 0700)) goto failure;
    struct group *render = getgrnam("render");
    instance->render_gid = render ? render->gr_gid : 0;
    instance->uid = provision_uid(m, instance->app, instance->data, error);
    if (!instance->uid) return FALSE;
    int fd = open(instance->data, O_DIRECTORY | O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat st;
    if (fd < 0 || fstat(fd, &st)) { if (fd >= 0) close(fd); goto failure; }
    gboolean okay = (st.st_uid == instance->uid || !st.st_uid)
        && ((st.st_uid == instance->uid && !instance->storage_changed) || migrate(fd, instance->uid));
    okay = okay && !fchown(fd, instance->uid, instance->uid) && !fchmod(fd, 0700);
    close(fd); if (!okay) goto failure;
    if (!appd_user_folders(m->home, instance->data, instance->uid, error)) return FALSE;
    const char *ancestors[] = {m->home, NULL};
    for (guint i = 0; ancestors[i]; ++i) if (chmod(ancestors[i], 0711)) goto failure;
    char *parent = g_build_filename(m->home, ".DataLocation", NULL);
    okay = !chmod(parent, 0711); g_free(parent);
    parent = g_build_filename(m->home, ".DataLocation/Applications", NULL);
    okay = okay && !chmod(parent, 0711); g_free(parent);
    if (!okay || g_mkdir_with_parents("/run/openwave/apps", 0711)) goto failure;
    char *uuid = g_uuid_string_random();
    instance->runtime = g_build_filename("/run/openwave/apps", uuid, NULL); g_free(uuid);
    if (mkdir(instance->runtime, 0700) || chown(instance->runtime, instance->uid, instance->uid)) goto failure;
    char *temporary = g_build_filename(instance->runtime, "tmp", NULL);
    okay = !mkdir(temporary, 0700) && !chown(temporary, instance->uid, instance->uid); g_free(temporary);
    if (!okay) goto failure;
    temporary = g_build_filename(instance->runtime, "shm", NULL);
    okay = !mkdir(temporary, 0700) && !chown(temporary, instance->uid, instance->uid); g_free(temporary);
    if (!okay) goto failure;
    if (openwave_selinux_restore(instance->app->bundle)
        || openwave_selinux_restore(instance->data)
        || openwave_selinux_restore(instance->runtime)) goto failure;
    return TRUE;
failure:
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot provision private application storage: %s", g_strerror(errno)); return FALSE;
}

static int allow_path(int ruleset, const char *path, guint64 rights, gboolean required)
{
    int fd = open(path, O_PATH | O_CLOEXEC);
    if (fd < 0) {
        if (!required && errno == ENOENT) return 0;
        ssize_t ignored = write(STDERR_FILENO, path, strlen(path)); (void)ignored;
        const char *reason = errno == EACCES ? ": Landlock path denied\n" : errno == ENOENT ? ": Landlock path missing\n" : ": Landlock path open failed\n";
        ignored = write(STDERR_FILENO, reason, strlen(reason)); (void)ignored;
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st)) { close(fd); return -1; }
    if (!S_ISDIR(st.st_mode)) rights &= LANDLOCK_ACCESS_FS_EXECUTE | LANDLOCK_ACCESS_FS_READ_FILE
        | LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_TRUNCATE | LANDLOCK_ACCESS_FS_IOCTL_DEV;
    struct landlock_path_beneath_attr rule = {.allowed_access = rights, .parent_fd = fd};
    int result = syscall(SYS_landlock_add_rule, ruleset, LANDLOCK_RULE_PATH_BENEATH, &rule, 0);
    if (result) {
        ssize_t ignored = write(STDERR_FILENO, path, strlen(path)); (void)ignored;
        ignored = write(STDERR_FILENO, ": Landlock rule failed\n", sizeof(": Landlock rule failed\n") - 1); (void)ignored;
    }
    close(fd); return result;
}

int appd_enforce_isolation(Instance *instance)
{
    if (appd_trusted_app(instance->app)) return 0;
    const guint64 read = LANDLOCK_ACCESS_FS_READ_FILE | LANDLOCK_ACCESS_FS_READ_DIR;
    const guint64 write = LANDLOCK_ACCESS_FS_WRITE_FILE | LANDLOCK_ACCESS_FS_REMOVE_DIR | LANDLOCK_ACCESS_FS_REMOVE_FILE
        | LANDLOCK_ACCESS_FS_MAKE_DIR | LANDLOCK_ACCESS_FS_MAKE_REG | LANDLOCK_ACCESS_FS_MAKE_SOCK | LANDLOCK_ACCESS_FS_MAKE_FIFO
        | LANDLOCK_ACCESS_FS_MAKE_SYM | LANDLOCK_ACCESS_FS_REFER | LANDLOCK_ACCESS_FS_TRUNCATE;
    struct landlock_ruleset_attr policy = {.handled_access_fs = (1ULL << 16) - 1,
        .scoped = LANDLOCK_SCOPE_ABSTRACT_UNIX_SOCKET | LANDLOCK_SCOPE_SIGNAL};
    int fd = syscall(SYS_landlock_create_ruleset, &policy, sizeof(policy), 0);
    if (fd < 0) return -1;
    const char *ro[] = {"/usr", "/lib", "/lib64", "/etc", "/proc", "/sys", "/run/openwave", "/run/dbus", NULL};
    int result = allow_path(fd, instance->app->bundle, read | LANDLOCK_ACCESS_FS_EXECUTE, TRUE);
    for (guint i = 0; !result && ro[i]; ++i) result = allow_path(fd, ro[i], read | LANDLOCK_ACCESS_FS_EXECUTE, FALSE);
    const char *rw[] = {instance->data, instance->runtime, "/dev/null", "/dev/zero", "/dev/random", "/dev/urandom", "/dev/dri", "/dev/shm", NULL};
    for (guint i = 0; !result && rw[i]; ++i) result = allow_path(fd, rw[i], read | write | LANDLOCK_ACCESS_FS_IOCTL_DEV, i < 2);
    if (!result) result = syscall(SYS_landlock_restrict_self, fd, 0);
    close(fd); return result;
}

static void remove_contents(int directory)
{
    DIR *dir = fdopendir(dup(directory)); if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        int child = openat(directory, entry->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (child >= 0) { remove_contents(child); close(child); unlinkat(directory, entry->d_name, AT_REMOVEDIR); }
        else unlinkat(directory, entry->d_name, 0);
    }
    closedir(dir);
}
void appd_release_isolation(Instance *instance)
{
    appd_close_namespace(instance);
    if (instance->runtime && g_str_has_prefix(instance->runtime, "/run/openwave/apps/")) {
        int fd = open(instance->runtime, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (fd >= 0) { remove_contents(fd); close(fd); rmdir(instance->runtime); }
    }
    g_free(instance->runtime); g_free(instance->data);
    instance->runtime = NULL; instance->data = NULL;
}

gboolean appd_publish_preferences(gpointer data)
{
    Manager *manager = data;
    if (manager->ready) appd_publish_sockets(manager);
    char *path = manager->uid == 0 ? g_strdup("/var/lib/openwave/OpenWave/settings.ini")
        : g_build_filename(manager->home, ".config/OpenWave/settings.ini", NULL);
    GKeyFile *private = g_key_file_new(), *public = g_key_file_new();
    g_key_file_load_from_file(private, path, 0, NULL); g_free(path);
    const char *keys[] = {"reduceMotion", "largeText", NULL};
    for (guint i = 0; keys[i]; ++i)
        g_key_file_set_boolean(public, "accessibility", keys[i], g_key_file_get_boolean(private, "accessibility", keys[i], NULL));
    gsize length; char *contents = g_key_file_to_data(public, &length, NULL);
    if (g_strcmp0(manager->public_preferences, contents)) {
        if (g_file_set_contents("/run/openwave/public-preferences.ini", contents, length, NULL)
            && !chmod("/run/openwave/public-preferences.ini", 0644)) {
            g_free(manager->public_preferences); manager->public_preferences = g_strdup(contents);
        }
    }
    g_free(contents); g_key_file_unref(public); g_key_file_unref(private); return G_SOURCE_CONTINUE;
}

static gboolean publish_socket(const char *source, const char *target, uid_t uid)
{
    struct stat original, alias;
    if (lstat(source, &original) || !S_ISSOCK(original.st_mode) || original.st_uid != uid || chmod(source, 0666)) return FALSE;
    if (!lstat(target, &alias) && S_ISSOCK(alias.st_mode) && alias.st_dev == original.st_dev && alias.st_ino == original.st_ino) return TRUE;
    char *temporary = g_strconcat(target, ".new", NULL);
    unlink(temporary);
    gboolean okay = !link(source, temporary) && !rename(temporary, target);
    if (!okay) unlink(temporary);
    g_free(temporary); return okay;
}
gboolean appd_publish_sockets(Manager *manager)
{
    char *wayland = g_build_filename(manager->runtime, "wayland-0", NULL);
    char *bus = g_build_filename(manager->runtime, "bus", NULL);
    gboolean okay = publish_socket(wayland, "/run/openwave/wayland-0", manager->uid)
        && publish_socket(bus, "/run/openwave/session-bus", 0);
    char *audio = g_build_filename(manager->runtime, "pipewire-0", NULL);
    if (g_file_test(audio, G_FILE_TEST_EXISTS)) {
        static int policy = -1;
        if (policy < 0) {
            void *library = dlopen("libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL);
            unsigned int (*version)(void) = library ? dlsym(library, "pw_openwave_access_policy_version") : NULL;
            policy = version && version() == 1;
            if (library) dlclose(library);
        }
        if (policy) publish_socket(audio, "/run/openwave/pipewire-0", 0);
    }
    g_free(audio); g_free(wayland); g_free(bus); return okay;
}
