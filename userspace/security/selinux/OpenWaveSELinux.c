#define _GNU_SOURCE
#include "OpenWaveSELinux.h"
#include <selinux/selinux.h>
#include <selinux/restorecon.h>
#include <selinux/label.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <linux/magic.h>
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
        if (!strcmp(arg, "openwave.audit=verbose")) {
            FILE *rate = fopen("/proc/sys/kernel/printk_ratelimit", "w");
            if (!rate || fputs("0\n", rate) < 0) {
                if (rate) fclose(rate);
                return -2;
            }
            fclose(rate);
            fputs("OpenWave SELinux: verbose audit diagnostics selected\n", stderr);
        }
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
    struct stat selinux_mountpoint;
    if (lstat("/sys/fs/selinux", &selinux_mountpoint) < 0) {
        perror("OpenWave SELinux: SELinux mount point");
        return -1;
    }
    if (!S_ISDIR(selinux_mountpoint.st_mode)) {
        fputs("OpenWave SELinux: SELinux mount point is not a directory\n", stderr);
        errno = ENOTDIR;
        return -1;
    }
    if (mount("selinuxfs", "/sys/fs/selinux", "selinuxfs", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL)
        && errno != EBUSY) {
        perror("OpenWave SELinux: mount selinuxfs");
        return -1;
    }
    set_selinuxmnt("/sys/fs/selinux");
    struct statfs fs;
    if (statfs("/sys/fs/selinux", &fs) || fs.f_type != SELINUX_MAGIC) {
        fputs("OpenWave SELinux: invalid selinuxfs mount\n", stderr);
        return -1;
    }
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
    int before_relabel = security_getenforce();
    if (before_relabel != 0) {
        fprintf(stderr, "OpenWave SELinux: expected permissive before relabel, observed %d (%s)\n",
                before_relabel, before_relabel < 0 ? strerror(errno) : "unexpected enforcement");
        return -1;
    }
    if (fsetfilecon(STDOUT_FILENO, "system_u:object_r:openwave_console_t:s0")) return -1;
    const char *exclude[] = {"/proc", "/sys", NULL};
    selinux_restorecon_set_exclude_list(exclude);
    /* New images are labeled and exhaustively checked by the image builder.
     * Refuse an unlabeled/mismatched new image; keep the legacy relabel path
     * for images that predate the build manifest. Never walk procfs/sysfs. */
    if (access("/etc/selinux/openwave/build-manifest.json", F_OK) == 0) {
        struct selabel_handle *labels = selabel_open(SELABEL_CTX_FILE, NULL, 0);
        if (!labels) return -1;
        const char *critical[] = {"/", "/init", "/sbin/openrc-init", "/bin/busybox",
            "/lib64/ld-linux-x86-64.so.2", "/lib64/libc.so.6",
            "/usr/lib64/libselinux.so.1",
            "/etc/selinux/openwave/policy/policy.35",
            "/System/Services/appd", "/System/Services/permissiond",
            "/System/Applications/com.openwave.shell/bin/ocean-launcher", NULL};
        for (int i = 0; critical[i]; ++i) {
            struct stat entry;
            char *expected = NULL, *actual = NULL;
            int invalid = lstat(critical[i], &entry)
                || selabel_lookup_raw(labels, &expected, critical[i], entry.st_mode)
                || lgetfilecon_raw(critical[i], &actual) < 0
                || strcmp(expected, actual);
            freecon(expected);
            freecon(actual);
            if (invalid) {
                fprintf(stderr, "OpenWave SELinux: offline label validation failed: %s\n", critical[i]);
                selabel_close(labels);
                return -1;
            }
        }
        selabel_close(labels);
        fputs("OpenWave SELinux: verified offline root labels\n", stderr);
    } else {
        fputs("OpenWave SELinux: legacy image, relabeling persistent root\n", stderr);
        if (openwave_selinux_restore("/")) return -1;
    }
    const char *paths[] = {"/dev", "/run", "/tmp", "/dev/shm", NULL};
    for (int i = 0; paths[i]; ++i)
        if (openwave_selinux_restore(paths[i])) return -1;
    if (security_setenforce(mode) < 0) {
        perror("OpenWave SELinux: set final enforcing mode");
        return -1;
    }
    int actual_mode = security_getenforce();
    if (actual_mode != mode) {
        fprintf(stderr, "OpenWave SELinux: final mode mismatch (expected=%d observed=%d%s%s)\n",
                mode, actual_mode, actual_mode < 0 ? " error=" : "",
                actual_mode < 0 ? strerror(errno) : "");
        return -1;
    }
    printf("OpenWave SELinux: policy loaded, filesystem labeled, %s\n", mode ? "enforcing" : "permissive recovery mode");
    return 0;
}
