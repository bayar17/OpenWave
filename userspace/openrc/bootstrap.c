#define _GNU_SOURCE

#include "bootstrap.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/magic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

#include "../security/selinux/OpenWaveSELinux.h"

int openwave_bootstrap_ensure_directory(const char *path, mode_t mode)
{
    if (!path || path[0] != '/') {
        errno = EINVAL;
        return -1;
    }
    size_t length = strlen(path);
    if (length == 0 || length >= PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }

    char current[PATH_MAX];
    memcpy(current, path, length + 1);
    for (char *cursor = current + 1; ; ++cursor) {
        if (*cursor != '/' && *cursor != '\0') continue;
        char saved = *cursor;
        *cursor = '\0';
        if (current[1] != '\0') {
            struct stat st;
            if (lstat(current, &st)) {
                if (errno != ENOENT || mkdir(current, saved ? 0755 : mode))
                    return -1;
                if (lstat(current, &st)) return -1;
            }
            if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
                errno = S_ISLNK(st.st_mode) ? ELOOP : ENOTDIR;
                return -1;
            }
        }
        *cursor = saved;
        if (!saved) break;
    }
    return 0;
}

int openwave_bootstrap_attach_console(const struct OpenWaveConsoleOps *ops)
{
    if (!ops || !ops->open_console || !ops->duplicate_fd || !ops->close_fd) {
        errno = EINVAL;
        return -1;
    }
    int fd = ops->open_console();
    if (fd < 0) {
        fprintf(stderr, "openwave bootstrap: open /dev/console: %s\n", strerror(errno));
        return -1;
    }
    for (int target = STDIN_FILENO; target <= STDERR_FILENO; ++target) {
        if (ops->duplicate_fd(fd, target) < 0) {
            int error = errno;
            if (fd > STDERR_FILENO) ops->close_fd(fd);
            errno = error;
            fprintf(stderr, "openwave bootstrap: attach console fd %d: %s\n",
                    target, strerror(errno));
            return -1;
        }
    }
    if (fd > STDERR_FILENO && ops->close_fd(fd)) {
        fprintf(stderr, "openwave bootstrap: close console fd: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int openwave_bootstrap_filesystem_matches(long actual, long expected)
{
    if (actual == expected) return 1;
    errno = ENODEV;
    return 0;
}

#ifndef OPENWAVE_BOOTSTRAP_NO_MAIN
static int ensure_mountpoint(const char *path, mode_t mode)
{
    if (openwave_bootstrap_ensure_directory(path, mode)) {
        fprintf(stderr, "openwave bootstrap: validate directory %s: %s\n",
                path, strerror(errno));
        return -1;
    }
    return 0;
}

static int mount_filesystem(const char *source, const char *target, const char *type,
                            unsigned long flags, const char *data, long expected_magic)
{
    struct stat st;
    struct statfs fs;
    if (lstat(target, &st)) {
        fprintf(stderr, "openwave bootstrap: invalid mountpoint %s: %s\n",
                target, strerror(errno));
        return -1;
    }
    if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
        errno = S_ISLNK(st.st_mode) ? ELOOP : ENOTDIR;
        fprintf(stderr, "openwave bootstrap: invalid mountpoint %s: %s\n",
                target, strerror(errno));
        return -1;
    }
    if (mount(source, target, type, flags, data) && errno != EBUSY) {
        fprintf(stderr, "openwave bootstrap: mount %s: %s\n", target, strerror(errno));
        return -1;
    }
    if (lstat(target, &st)) {
        fprintf(stderr, "openwave bootstrap: invalid mounted path %s: %s\n",
                target, strerror(errno));
        return -1;
    }
    if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
        errno = S_ISLNK(st.st_mode) ? ELOOP : ENOTDIR;
        fprintf(stderr, "openwave bootstrap: invalid mounted path %s: %s\n",
                target, strerror(errno));
        return -1;
    }
    if (statfs(target, &fs)) {
        fprintf(stderr, "openwave bootstrap: inspect filesystem at %s: %s\n",
                target, strerror(errno));
        return -1;
    }
    if (!openwave_bootstrap_filesystem_matches((long)fs.f_type, expected_magic)) {
        fprintf(stderr, "openwave bootstrap: unexpected filesystem at %s\n", target);
        return -1;
    }
    return 0;
}

static int open_console(void)
{
    return open("/dev/console", O_RDWR | O_NOCTTY);
}

static int dup_console_fd(int source, int target)
{
    return dup2(source, target);
}

static int close_console_fd(int fd)
{
    return close(fd);
}

static int attach_console(void)
{
    static const struct OpenWaveConsoleOps console_ops = {
        .open_console = open_console,
        .duplicate_fd = dup_console_fd,
        .close_fd = close_console_fd,
    };
    return openwave_bootstrap_attach_console(&console_ops);
}

static int set_environment(const char *name, const char *value)
{
    return setenv(name, value, 1);
}

static int exec_openrc_init(void)
{
    char *const args[] = {(char *)"/sbin/openrc-init", NULL};
    execv(args[0], args);
    fprintf(stderr, "openwave bootstrap: exec /sbin/openrc-init: %s\n", strerror(errno));
    return -1;
}

static const struct OpenWaveBootstrapOps production_ops = {
    .get_pid = getpid,
    .ensure_directory = ensure_mountpoint,
    .mount_filesystem = mount_filesystem,
    .attach_console = attach_console,
    .selinux_boot = openwave_selinux_boot,
    .set_environment = set_environment,
    .exec_openrc_init = exec_openrc_init,
};
#endif

int openwave_bootstrap_run(const struct OpenWaveBootstrapOps *ops)
{
    if (!ops || !ops->get_pid || !ops->ensure_directory || !ops->mount_filesystem
        || !ops->attach_console || !ops->selinux_boot || !ops->set_environment
        || !ops->exec_openrc_init) {
        errno = EINVAL;
        return -1;
    }
    if (ops->get_pid() != 1) {
        fputs("openwave bootstrap: must execute as PID 1\n", stderr);
        errno = EPERM;
        return -1;
    }

    /* Only top-level mountpoints are prepared before their filesystems mount. */
    static const struct { const char *path; mode_t mode; } initial_dirs[] = {
        {"/dev", 0755}, {"/proc", 0555}, {"/sys", 0555},
        {"/run", 0755}, {"/tmp", 01777},
    };
    for (size_t i = 0; i < sizeof(initial_dirs) / sizeof(initial_dirs[0]); ++i)
        if (ops->ensure_directory(initial_dirs[i].path, initial_dirs[i].mode)) return -1;

    if (ops->mount_filesystem("devtmpfs", "/dev", "devtmpfs", MS_NOSUID,
                              "mode=0755", TMPFS_MAGIC)
        || ops->attach_console()
        || ops->mount_filesystem("proc", "/proc", "proc",
                                 MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL, PROC_SUPER_MAGIC)
        || ops->mount_filesystem("sysfs", "/sys", "sysfs",
                                 MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL, SYSFS_MAGIC))
        return -1;

    /* sysfs owns this subtree; establish parents only after verifying sysfs. */
    if (ops->ensure_directory("/sys/fs", 0755)
        || ops->ensure_directory("/sys/fs/selinux", 0755))
        return -1;
    if (ops->mount_filesystem("selinuxfs", "/sys/fs/selinux", "selinuxfs",
                              MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL, SELINUX_MAGIC))
        return -1;

    if (ops->ensure_directory("/dev/pts", 0755)
        || ops->ensure_directory("/dev/shm", 01777)
        || ops->mount_filesystem("tmpfs", "/run", "tmpfs", MS_NOSUID | MS_NODEV,
                                 "mode=0755", TMPFS_MAGIC)
        || ops->mount_filesystem("tmpfs", "/tmp", "tmpfs", MS_NOSUID | MS_NODEV,
                                 "mode=1777", TMPFS_MAGIC)
        || ops->mount_filesystem("tmpfs", "/dev/shm", "tmpfs", MS_NOSUID | MS_NODEV,
                                 "mode=1777", TMPFS_MAGIC)
        || ops->mount_filesystem("devpts", "/dev/pts", "devpts",
                                 MS_NOSUID | MS_NOEXEC,
                                 "gid=5,mode=0620,ptmxmode=0666", DEVPTS_SUPER_MAGIC))
        return -1;

    if (ops->selinux_boot()) return -1;
    if (ops->set_environment("PATH", "/sbin:/bin:/usr/sbin:/usr/bin:/System/Services")
        || ops->set_environment("HOME", "/root")
        || ops->set_environment("DBUS_SYSTEM_BUS_ADDRESS",
                                "unix:path=/run/dbus/system_bus_socket"))
        return -1;
    return ops->exec_openrc_init();
}

#ifndef OPENWAVE_BOOTSTRAP_NO_MAIN
static void emergency_wait(void)
{
    for (;;) pause();
}

int main(void)
{
    if (openwave_bootstrap_run(&production_ops)) emergency_wait();
    /* Reached only when a test exec shim returns success; production exec replaces us. */
    emergency_wait();
    return 1;
}
#endif
