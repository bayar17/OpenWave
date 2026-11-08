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

static gboolean identity_error(GError **error)
{
    if (error && !*error) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
        "Cannot provision a protected application identity");
    return FALSE;
}

static gboolean protected_metadata(const struct stat *st, mode_t mode)
{
    return S_ISREG(st->st_mode) && !st->st_uid && !st->st_gid
        && st->st_nlink == 1 && (st->st_mode & 07777) == mode;
}

/* Pin every parent and reject links, including links in the parent path. */
static int identity_directory(const char *path)
{
    int fd = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    char **parts = g_strsplit(path, "/", -1);
    for (guint i = 0; fd >= 0 && parts[i]; ++i) {
        if (!*parts[i]) continue;
        if (!strcmp(parts[i], ".") || !strcmp(parts[i], "..")) { close(fd); fd = -1; break; }
        guint after = i + 1;
        while (parts[after] && !*parts[after]) ++after;
        int access = parts[after] ? O_PATH : O_RDONLY;
        int next = openat(fd, parts[i], access | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        close(fd); fd = next;
        struct stat st;
        if (fd >= 0 && (fstat(fd, &st) || st.st_uid || st.st_gid || (st.st_mode & 0022))) {
            close(fd); fd = -1;
        }
    }
    g_strfreev(parts);
    return fd;
}

static gboolean read_identity_file(int directory, const char *name, mode_t mode,
    gboolean optional, char **contents, gsize *length, GError **error)
{
    *contents = NULL; *length = 0;
    int fd = openat(directory, name, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return optional && errno == ENOENT ? TRUE : identity_error(error);
    struct stat st;
    gboolean okay = !fstat(fd, &st) && protected_metadata(&st, mode)
        && st.st_size >= 0 && st.st_size <= 1048576;
    GString *text = g_string_new(NULL);
    char buffer[4096];
    while (okay) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { okay = FALSE; break; }
        if (!count) break;
        if (text->len + (gsize)count > 1048576 || memchr(buffer, '\0', count)) { okay = FALSE; break; }
        g_string_append_len(text, buffer, count);
    }
    close(fd);
    if (!okay) { g_string_free(text, TRUE); return identity_error(error); }
    *length = text->len; *contents = g_string_free(text, FALSE);
    return TRUE;
}

static gboolean write_identity_file(int directory, const char *name, mode_t mode,
    const char *contents, gsize length, GError **error)
{
    char *uuid = g_uuid_string_random();
    char *temporary = g_strdup_printf(".%s.%s", name, uuid); g_free(uuid);
    int fd = openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    gboolean okay = fd >= 0;
    for (gsize done = 0; okay && done < length;) {
        ssize_t count = write(fd, contents + done, length - done);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { okay = FALSE; break; }
        done += count;
    }
    if (okay) okay = !fchown(fd, 0, 0) && !fchmod(fd, mode) && !fsync(fd);
    if (fd >= 0 && close(fd)) okay = FALSE;
    /* Never replace a symlink or an unexpected hard-linked account file. */
    struct stat st;
    if (okay && fstatat(directory, name, &st, AT_SYMLINK_NOFOLLOW)) okay = errno == ENOENT;
    else if (okay) okay = protected_metadata(&st, mode);
    if (okay) okay = !renameat(directory, temporary, directory, name) && !fsync(directory);
    if (!okay) unlinkat(directory, temporary, 0);
    g_free(temporary);
    return okay || identity_error(error);
}

static gboolean account_uid_present(const char *contents, uid_t uid)
{
    char **lines = g_strsplit(contents, "\n", -1);
    gboolean found = FALSE;
    for (guint i = 0; !found && lines[i]; ++i) {
        char **fields = g_strsplit(lines[i], ":", -1);
        char *end = NULL;
        if (g_strv_length(fields) == 7) {
            guint64 value = g_ascii_strtoull(fields[2], &end, 10);
            found = end != fields[2] && !*end && value == uid;
        }
        g_strfreev(fields);
    }
    g_strfreev(lines); return found;
}

static gboolean update_account_home(int directory, uid_t uid, const char *data, GError **error)
{
    if (!data || data[0] != '/' || strpbrk(data, ":\r\n")) return identity_error(error);
    char *contents = NULL; gsize length;
    if (!read_identity_file(directory, "passwd", 0644, FALSE, &contents, &length, error)) return FALSE;
    char **lines = g_strsplit(contents, "\n", -1); g_free(contents);
    GString *updated = g_string_new(NULL); gboolean found = FALSE, changed = FALSE;
    for (guint i = 0; lines[i]; ++i) {
        if (!*lines[i]) continue;
        char **fields = g_strsplit(lines[i], ":", -1);
        char *end = NULL;
        guint64 value = g_strv_length(fields) == 7 ? g_ascii_strtoull(fields[2], &end, 10) : 0;
        if (end && end != fields[2] && !*end && value == uid) {
            found = TRUE;
            if (g_strcmp0(fields[5], data)) { g_free(fields[5]); fields[5] = g_strdup(data); changed = TRUE; }
        }
        char *line = g_strjoinv(":", fields); g_string_append_printf(updated, "%s\n", line);
        g_free(line); g_strfreev(fields);
    }
    g_strfreev(lines);
    if (!found) { g_string_append_printf(updated, "owapp%u:x:%u:%u:OpenWave application:%s:/bin/false\n", uid, uid, uid, data); changed = TRUE; }
    gboolean okay = !changed || write_identity_file(directory, "passwd", 0644, updated->str, updated->len, error);
    g_string_free(updated, TRUE); return okay;
}

static uid_t provision_uid_at(Manager *m, App *app, const char *data,
    int accounts, int identities, GError **error)
{
    /* Lock an inode which is never renamed, across both passwd and UID-map
     * transactions. Locking passwd itself races with atomic replacement. */
    int lock = openat(accounts, ".lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    struct stat st;
    if (lock < 0 || fstat(lock, &st) || !protected_metadata(&st, 0600) || flock(lock, LOCK_EX)) {
        if (lock >= 0) close(lock);
        identity_error(error); return 0;
    }
    uid_t uid = 0;
    GKeyFile *map = g_key_file_new();
    char *contents = NULL, *key = NULL; gsize length;
    if (!read_identity_file(identities, "users.ini", 0600, TRUE, &contents, &length, error)) goto invalid;
    if (contents && !g_key_file_load_from_data(map, contents, length, 0, error)) goto invalid;
    g_clear_pointer(&contents, g_free);
    key = g_strdup_printf("%u:%s", m->uid, app->id);
    guint64 mapped = g_key_file_get_uint64(map, "Applications", key, NULL);
    gboolean changed = FALSE;
    const char *legacy = appd_legacy_id(app);
    if (!mapped && legacy) {
        char *old_key = g_strdup_printf("%u:%s", m->uid, legacy);
        mapped = g_key_file_get_uint64(map, "Applications", old_key, NULL);
        if (mapped && mapped >= 200000 && mapped < 300000) {
            g_key_file_set_uint64(map, "Applications", key, mapped);
            g_key_file_remove_key(map, "Applications", old_key, NULL);
            changed = TRUE;
        }
        g_free(old_key);
    }
    if (mapped && (mapped < 200000 || mapped >= 300000)) goto invalid;
    uid = mapped;
    if (!uid) {
        if (!read_identity_file(accounts, "passwd", 0644, FALSE, &contents, &length, error)) goto invalid;
        for (uid = 200000; uid < 300000; ++uid)
            if (!account_uid_present(contents, uid) && !getpwuid(uid)) break;
        g_clear_pointer(&contents, g_free);
        if (uid == 300000) goto invalid;
        g_key_file_set_uint64(map, "Applications", key, uid);
        changed = TRUE;
    }
    if (!update_account_home(accounts, uid, data, error)) goto invalid;
    if (changed) {
        contents = g_key_file_to_data(map, &length, NULL);
        if (!write_identity_file(identities, "users.ini", 0600, contents, length, error)) goto invalid;
    }
    g_free(contents); g_free(key); g_key_file_unref(map); close(lock); return uid;
invalid:
    g_free(contents); g_free(key); g_key_file_unref(map); close(lock);
    identity_error(error);
    return 0;
}

static uid_t provision_uid(Manager *m, App *app, const char *data, GError **error)
{
    /* libc reads /etc/passwd through the image's protected relative symlink.
     * All daemon verification and mutations use only its mutable backing. */
    int accounts = identity_directory("/var/lib/openwave-accounts");
    int identities = identity_directory("/var/lib/openwave/app-identities");
    uid_t uid = 0;
    if (accounts >= 0 && identities >= 0) uid = provision_uid_at(m, app, data, accounts, identities, error);
    else identity_error(error);
    if (accounts >= 0) close(accounts);
    if (identities >= 0) close(identities);
    return uid;
}

static gboolean prepare_data_owner(Instance *instance)
{
    int fd = open(instance->data, O_DIRECTORY | O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat st;
    if (fd < 0) return FALSE;
    gboolean okay = !fstat(fd, &st) && (st.st_uid == instance->uid || !st.st_uid)
        && ((st.st_uid == instance->uid && !instance->storage_changed) || migrate(fd, instance->uid));
    okay = okay && !fchown(fd, instance->uid, instance->uid) && !fchmod(fd, 0700);
    close(fd);
    return okay;
}

gboolean appd_prepare_isolation(Manager *m, Instance *instance, GError **error)
{
    instance->data = g_strdup_printf("%s/.DataLocation/Applications/%s", m->home, instance->app->id);
    if (!appd_prepare_storage(m, instance, error)) {
        if (error && !*error) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Cannot prepare safe application storage");
        return FALSE;
    }
    struct group *render = getgrnam("render");
    instance->render_gid = render ? render->gr_gid : 0;
    if (appd_trusted_app(instance->app)) {
        instance->uid = m->uid; instance->runtime = g_strdup(m->runtime);
        if (!prepare_data_owner(instance)) goto failure;
        return TRUE;
    }
    if (!m->cgroup_root || syscall(SYS_landlock_create_ruleset, NULL, 0, LANDLOCK_CREATE_RULESET_VERSION) < 6) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Application isolation requires cgroup v2 and Landlock ABI 6 or newer"); return FALSE;
    }
    if (g_mkdir_with_parents(instance->data, 0700)) goto failure;
    instance->uid = provision_uid(m, instance->app, instance->data, error);
    if (!instance->uid) return FALSE;
    if (!prepare_data_owner(instance)) goto failure;
    gboolean okay;
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
    const char *ro[] = {"/usr", "/lib", "/lib64", "/etc", "/proc", "/sys", "/run/openwave", "/run/dbus", "/run/NetworkManager", NULL};
    int result = allow_path(fd, instance->app->bundle, read | LANDLOCK_ACCESS_FS_EXECUTE, TRUE);
    if (!result) result = allow_path(fd, "/var/lib/openwave-accounts", read, TRUE);
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
    if (lstat(source, &original) || !S_ISSOCK(original.st_mode) || original.st_uid != uid) return FALSE;
    if ((original.st_mode & 0777) != 0666 && chmod(source, 0666)) return FALSE;
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
        && publish_socket(bus, "/run/openwave/session-bus", manager->uid);
    char *audio = g_build_filename(manager->runtime, "pipewire-0", NULL);
    if (g_file_test(audio, G_FILE_TEST_EXISTS)) {
        static int policy = -1;
        if (policy < 0) {
            void *library = dlopen("libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL);
            unsigned int (*version)(void) = library ? dlsym(library, "pw_openwave_access_policy_version") : NULL;
            policy = version && version() == 1;
            if (library) dlclose(library);
        }
        if (policy) publish_socket(audio, "/run/openwave/pipewire-0", manager->uid);
    }
    g_free(audio); g_free(wayland); g_free(bus); return okay;
}
