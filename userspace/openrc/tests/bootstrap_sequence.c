#define _GNU_SOURCE
#include "../bootstrap.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *events[64];
static size_t event_count;
static int sysfs_mounted;
static int selinuxfs_mounted;
static int enforce_enabled = 1;
static const char *fail_mount_target;
static int fail_dup_target = -1;
static int console_closed;

static void record(const char *event)
{
    if (event_count >= sizeof(events) / sizeof(events[0])) abort();
    events[event_count++] = event;
}

static pid_t fake_getpid(void) { return 1; }

static int fake_ensure_directory(const char *path, mode_t mode)
{
    (void)mode;
    if (!strncmp(path, "/sys/fs", 7) && !sysfs_mounted) {
        errno = ENOENT;
        return -1;
    }
    if (!strcmp(path, "/sys/fs/selinux")) record("dir:selinuxfs");
    else if (!strcmp(path, "/sys/fs")) record("dir:sysfs-fs");
    else record("dir:top");
    return 0;
}

static int fake_mount(const char *source, const char *target, const char *type,
                      unsigned long flags, const char *data, long magic)
{
    (void)type; (void)flags; (void)data; (void)magic;
    if (fail_mount_target && !strcmp(target, fail_mount_target)) {
        errno = EIO;
        return -1;
    }
    if (!strcmp(target, "/sys") && !strcmp(source, "sysfs")) sysfs_mounted = 1;
    if (!strcmp(target, "/sys/fs/selinux") && !strcmp(source, "selinuxfs")) {
        if (!sysfs_mounted) { errno = ENOENT; return -1; }
        selinuxfs_mounted = 1;
    }
    record(!strcmp(target, "/sys") ? "mount:sysfs"
           : !strcmp(target, "/sys/fs/selinux") ? "mount:selinuxfs"
           : !strcmp(target, "/proc") ? "mount:proc"
           : !strcmp(target, "/dev") ? "mount:devtmpfs"
           : "mount:other");
    return 0;
}

static int fake_open_console(void) { record("console:open"); return 9; }
static int fake_dup_console(int source, int target)
{
    if (source != 9) return -1;
    if (target == fail_dup_target) { errno = EIO; return -1; }
    record(target == 0 ? "console:stdin" : target == 1 ? "console:stdout" : "console:stderr");
    return target;
}
static int fake_close_console(int fd)
{
    if (fd != 9) return -1;
    console_closed++;
    record("console:close");
    return 0;
}
static const struct OpenWaveConsoleOps console_ops = {
    .open_console = fake_open_console,
    .duplicate_fd = fake_dup_console,
    .close_fd = fake_close_console,
};
static int fake_console(void)
{
    if (openwave_bootstrap_attach_console(&console_ops)) return -1;
    record("console:ready");
    return 0;
}

static int fake_selinux_boot(void)
{
    if (!selinuxfs_mounted) { errno = ENODEV; return -1; }
    if (!enforce_enabled) { errno = EACCES; return -1; }
    record("selinux:loaded-enforcing");
    return 0;
}

static int fake_setenv(const char *name, const char *value)
{
    if (!strcmp(name, "PATH") && !strcmp(value, "/sbin:/bin:/usr/sbin:/usr/bin:/System/Services")) record("env:path");
    else if (!strcmp(name, "HOME") && !strcmp(value, "/root")) record("env:home");
    else if (!strcmp(name, "DBUS_SYSTEM_BUS_ADDRESS")
             && !strcmp(value, "unix:path=/run/dbus/system_bus_socket")) record("env:dbus");
    else { errno = EINVAL; return -1; }
    return 0;
}

static int fake_exec(void) { record("exec:/sbin/openrc-init"); return 0; }

static struct OpenWaveBootstrapOps test_ops = {
    .get_pid = fake_getpid,
    .ensure_directory = fake_ensure_directory,
    .mount_filesystem = fake_mount,
    .attach_console = fake_console,
    .selinux_boot = fake_selinux_boot,
    .set_environment = fake_setenv,
    .exec_openrc_init = fake_exec,
};

static int expect_order(void)
{
    size_t sys = 0, proc = 0, selinux_dir = 0, selinux_mount = 0, load = 0, handoff = 0;
    for (size_t i = 0; i < event_count; ++i) {
        if (!strcmp(events[i], "mount:sysfs")) sys = i + 1;
        if (!strcmp(events[i], "mount:proc")) proc = i + 1;
        if (!strcmp(events[i], "dir:selinuxfs")) selinux_dir = i + 1;
        if (!strcmp(events[i], "mount:selinuxfs")) selinux_mount = i + 1;
        if (!strcmp(events[i], "selinux:loaded-enforcing")) load = i + 1;
        if (!strcmp(events[i], "exec:/sbin/openrc-init")) handoff = i + 1;
    }
    return proc && sys && proc < sys && sys < selinux_dir && selinux_dir < selinux_mount
        && selinux_mount < load && load < handoff;
}

static int test_successful_order(void)
{
    event_count = sysfs_mounted = selinuxfs_mounted = 0;
    enforce_enabled = 1;
    fail_mount_target = NULL;
    if (openwave_bootstrap_run(&test_ops) || !expect_order()) return 1;
    if (strcmp(events[event_count - 1], "exec:/sbin/openrc-init")) return 1;
    return 0;
}

static int test_failure_stops_before_selinux_and_handoff(void)
{
    event_count = sysfs_mounted = selinuxfs_mounted = 0;
    enforce_enabled = 1;
    fail_mount_target = "/sys";
    if (openwave_bootstrap_run(&test_ops) == 0) return 1;
    for (size_t i = 0; i < event_count; ++i)
        if (!strcmp(events[i], "dir:selinuxfs") || !strcmp(events[i], "exec:/sbin/openrc-init"))
            return 1;
    fail_mount_target = NULL;
    return 0;
}

static int test_enforcing_failure_stops_handoff(void)
{
    event_count = sysfs_mounted = selinuxfs_mounted = 0;
    enforce_enabled = 0;
    fail_mount_target = NULL;
    if (openwave_bootstrap_run(&test_ops) == 0) return 1;
    for (size_t i = 0; i < event_count; ++i)
        if (!strcmp(events[i], "exec:/sbin/openrc-init")) return 1;
    enforce_enabled = 1;
    return 0;
}

static int test_safe_directory_creation_and_symlink_rejection(void)
{
    char root[] = "/tmp/openwave-bootstrap-test-XXXXXX";
    if (!mkdtemp(root)) return 1;
    char nested[512], link_path[512], outside[512], file_path[512];
    snprintf(nested, sizeof(nested), "%s/missing/parent/selinux", root);
    snprintf(link_path, sizeof(link_path), "%s/link", root);
    snprintf(outside, sizeof(outside), "%s/outside", root);
    snprintf(file_path, sizeof(file_path), "%s/not-directory", root);
    if (openwave_bootstrap_ensure_directory(nested, 0700)) return 1;
    if (symlink(outside, link_path)) return 1;
    char through_link[512];
    snprintf(through_link, sizeof(through_link), "%s/link/child", root);
    if (!openwave_bootstrap_ensure_directory(through_link, 0700) || errno != ELOOP) return 1;
    FILE *file = fopen(file_path, "w");
    if (!file) return 1;
    fclose(file);
    char through_file[512];
    snprintf(through_file, sizeof(through_file), "%s/not-directory/child", root);
    if (!openwave_bootstrap_ensure_directory(through_file, 0700) || errno != ENOTDIR) return 1;
    return 0;
}

static int test_console_descriptors_survive_handoff(void)
{
    console_closed = 0;
    fail_dup_target = -1;
    if (openwave_bootstrap_attach_console(&console_ops) || console_closed != 1) return 1;
    console_closed = 0;
    fail_dup_target = 1;
    if (!openwave_bootstrap_attach_console(&console_ops) || console_closed != 1) return 1;
    fail_dup_target = -1;
    return 0;
}

static int test_mount_type_validation(void)
{
    if (!openwave_bootstrap_filesystem_matches(0x62656572L, 0x62656572L)) return 1;
    errno = 0;
    if (openwave_bootstrap_filesystem_matches(0x01021994L, 0x62656572L)
        || errno != ENODEV) return 1;
    return 0;
}

int main(void)
{
    if (test_safe_directory_creation_and_symlink_rejection()
        || test_console_descriptors_survive_handoff()
        || test_mount_type_validation()
        || test_successful_order()
        || test_failure_stops_before_selinux_and_handoff()
        || test_enforcing_failure_stops_handoff()) {
        fputs("bootstrap regression: FAIL\n", stderr);
        return 1;
    }
    puts("bootstrap regression: PASS");
    return 0;
}
