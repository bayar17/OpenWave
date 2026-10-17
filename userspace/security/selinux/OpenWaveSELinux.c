#define _GNU_SOURCE
#include "OpenWaveSELinux.h"
#include <selinux/selinux.h>
#include <selinux/restorecon.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static int boot_mode(void)
{
    char line[8192];
    FILE *file = fopen("/proc/cmdline", "r");
    if (!file) return -2;
    char *read = fgets(line, sizeof(line), file);
    fclose(file);
    if (!read) return -2;
    char *save = NULL;
    int mode = 1;
    if (selinux_getenforcemode(&mode)) return -2;
    for (char *arg = strtok_r(line, " \n", &save); arg; arg = strtok_r(NULL, " \n", &save)) {
        if (!strcmp(arg, "openwave.selinux=disabled")) mode = -1;
        else if (!strcmp(arg, "openwave.selinux=permissive")) mode = 0;
    }
    return mode;
}

int openwave_selinux_restore(const char *path)
{
    if (is_selinux_enabled() <= 0) return 0;
    return selinux_restorecon(path, SELINUX_RESTORECON_RECURSE | SELINUX_RESTORECON_XDEV
        | SELINUX_RESTORECON_SET_SPECFILE_CTX | SELINUX_RESTORECON_ABORT_ON_ERROR
        | SELINUX_RESTORECON_IGNORE_NOENTRY | SELINUX_RESTORECON_SKIP_DIGEST);
}

int openwave_selinux_app_exec(void)
{
    if (is_selinux_enabled() <= 0) return 0;
    const char context[] = "system_u:system_r:openwave_app_t:s0";
    int fd = open("/proc/self/attr/exec", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    int okay = write(fd, context, sizeof(context)) == sizeof(context);
    close(fd);
    return okay ? 0 : -1;
}

int openwave_selinux_boot(void)
{
    int mode = boot_mode();
    if (mode == -1) {
        fprintf(stderr, "OpenWave SELinux: disabled by recovery configuration\n");
        return 0;
    }
    if (mode < 0) return -1;
    mkdir("/sys/fs/selinux", 0755);
    if (mount("selinuxfs", "/sys/fs/selinux", "selinuxfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL)
        && errno != EBUSY) return -1;
    set_selinuxmnt("/sys/fs/selinux");
    if (selinux_set_policy_root("/etc/selinux/openwave") || security_setenforce(0)) return -1;
    int fd = open("/etc/selinux/openwave/policy/policy.35", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat st;
    if (fd < 0) return -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid || (st.st_mode & 0022)
        || st.st_size <= 0 || st.st_size > 16777216) { close(fd); errno = EINVAL; return -1; }
    void *policy = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (policy == MAP_FAILED) return -1;
    int result = security_load_policy(policy, st.st_size);
    munmap(policy, st.st_size);
    if (result) return -1;
    if (fsetfilecon(STDOUT_FILENO, "system_u:object_r:openwave_console_t:s0")) return -1;
    const char *exclude[] = {"/proc", "/sys", NULL};
    selinux_restorecon_set_exclude_list(exclude);
    const char *paths[] = {"/", "/dev", "/run", "/tmp", "/dev/shm", NULL};
    for (int i = 0; paths[i]; ++i)
        if (openwave_selinux_restore(paths[i])) return -1;
    if (security_setenforce(mode) || security_getenforce() != mode) return -1;
    printf("OpenWave SELinux: policy loaded, filesystem labeled, %s\n", mode ? "enforcing" : "permissive recovery mode");
    return 0;
}
