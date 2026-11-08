#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <selinux/selinux.h>

#include "../security/selinux/OpenWaveSELinux.h"

static int check(const char *path, uid_t uid, gid_t gid, mode_t mode,
                 const char *context)
{
    struct stat st;
    char *actual = NULL;
    if (lstat(path, &st) || !S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode)
        || st.st_uid != uid || st.st_gid != gid || (st.st_mode & 07777) != mode) {
        fprintf(stderr, "openwave runtime check: invalid directory ownership/mode: %s\n", path);
        errno = EPERM;
        return -1;
    }
    if (getfilecon(path, &actual) < 0) {
        fprintf(stderr, "openwave runtime check: cannot label/read context: %s\n", path);
        freecon(actual);
        return -1;
    }
    int okay = !strcmp(actual, context);
    if (!okay)
        fprintf(stderr, "openwave runtime check: %s has context %s, expected %s\n",
                path, actual, context);
    freecon(actual);
    if (!okay) { errno = EACCES; return -1; }
    return 0;
}

int main(int argc, char **argv)
{
    static const struct {
        const char *path;
        uid_t uid;
        gid_t gid;
        mode_t mode;
        const char *context;
    } native_required[] = {
        {"/run/openwave", 0, 0, 0755, "system_u:object_r:openwave_service_run_t:s0"},
        {"/run/NetworkManager", 0, 0, 0755, "system_u:object_r:openwave_networkmanager_state_t:s0"},
        {"/var/lib/NetworkManager", 0, 0, 0700, "system_u:object_r:openwave_networkmanager_state_t:s0"},
        {"/var/lib/iwd", 0, 0, 0700, "system_u:object_r:openwave_iwd_state_t:s0"},
        {"/run/openwave/appd", 0, 0, 0700, "system_u:object_r:openwave_appd_runtime_t:s0"},
        {"/run/user", 0, 0, 0755, "system_u:object_r:openwave_run_t:s0"},
        {"/run/user/1000", 1000, 1000, 0700, "system_u:object_r:openwave_run_t:s0"},
        {"/run/user/1000/openwave-logs", 1000, 1000, 0700, "system_u:object_r:openwave_qa_evidence_t:s0"},
        {"/var/lib/openwave-accounts", 0, 0, 0755, "system_u:object_r:openwave_accounts_t:s0"},
        {"/var/lib/openwave", 0, 0, 0700, "system_u:object_r:openwave_state_t:s0"},
        {"/var/lib/openwave/state", 0, 0, 0700, "system_u:object_r:openwave_state_t:s0"},
        {"/var/lib/openwave/app-identities", 0, 0, 0700, "system_u:object_r:openwave_appd_state_t:s0"},
        {"/var/lib/openwave/permissions", 0, 0, 0700, "system_u:object_r:openwave_permission_state_t:s0"},
        {"/var/lib/ocean", 1000, 1000, 0750, "system_u:object_r:openwave_shell_state_t:s0"},
        {"/var/lib/ocean/.config", 1000, 1000, 0750, "system_u:object_r:openwave_shell_state_t:s0"},
        {"/var/lib/ocean/.local", 1000, 1000, 0750, "system_u:object_r:openwave_shell_state_t:s0"},
        {"/var/lib/ocean/.local/state", 1000, 1000, 0750, "system_u:object_r:openwave_shell_state_t:s0"},
        {"/var/openwave", 0, 0, 0755, "system_u:object_r:openwave_wallpaper_state_t:s0"},
        {"/var/openwave/wallpaper", 0, 0, 0755, "system_u:object_r:openwave_wallpaper_state_t:s0"},
    };
    static const struct {
        const char *path;
        uid_t uid;
        gid_t gid;
        mode_t mode;
        const char *context;
    } waydroid_required[] = {
        {"/run/openwave/waydroid", 0, 0, 0700, "system_u:object_r:openwave_waydroid_snapshot_t:s0"},
        {"/var/lib/waydroid", 0, 0, 0755, "system_u:object_r:openwave_waydroid_state_t:s0"},
        {"/var/lib/waydroid-user", 990, 990, 0700, "system_u:object_r:openwave_waydroid_user_t:s0"},
        {"/run/waydroid", 990, 990, 0700, "system_u:object_r:openwave_waydroid_runtime_t:s0"},
    };
    int waydroid = argc == 2 && !strcmp(argv[1], "--waydroid");
    if (argc > 2 || (argc == 2 && !waydroid)) {
        fputs("usage: openwave-runtime-check [--waydroid]\n", stderr);
        return 2;
    }
    if (geteuid() != 0) return 126;
    if (is_selinux_enabled() != 1 || security_getenforce() != 1) {
        fputs("openwave runtime check: SELinux is not active and enforcing\n", stderr);
        return 1;
    }
    set_selinuxmnt("/sys/fs/selinux");
    if (selinux_set_policy_root("/etc/selinux/openwave")) {
        perror("openwave runtime check: set SELinux policy root");
        return 1;
    }
    const size_t count = waydroid
        ? sizeof(waydroid_required) / sizeof(waydroid_required[0])
        : sizeof(native_required) / sizeof(native_required[0]);
    for (size_t i = 0; i < count; ++i) {
        const char *path = waydroid ? waydroid_required[i].path : native_required[i].path;
        uid_t uid = waydroid ? waydroid_required[i].uid : native_required[i].uid;
        gid_t gid = waydroid ? waydroid_required[i].gid : native_required[i].gid;
        mode_t mode = waydroid ? waydroid_required[i].mode : native_required[i].mode;
        const char *context = waydroid ? waydroid_required[i].context : native_required[i].context;
        if (check(path, uid, gid, mode, context)) return 1;
    }
    puts(waydroid
        ? "OpenWave Waydroid runtime directories: ownership, mode, and SELinux labels verified"
        : "OpenWave native runtime directories: ownership, mode, and SELinux labels verified");
    return 0;
}
