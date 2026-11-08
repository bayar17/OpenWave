/*
 * OpenWave Waydroid rootfs mount boundary, Stage 2A.
 *
 * Fixed operations only: no arbitrary source/destination paths or shell.
 * Not wired into the Waydroid lifecycle until bind/overlay/unmount audit.
 * GPL-3.0-or-later (OpenWave contribution).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/loop.h>
#include <linux/magic.h>
#include <linux/openat2.h>
#include <linux/stat.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#define WAYDROID_ROOT "/var/lib/waydroid"
#define SYSTEM_MOUNT WAYDROID_ROOT "/rootfs"
#define VENDOR_MOUNT WAYDROID_ROOT "/rootfs/vendor"
#define PREINSTALLED "/usr/share/waydroid-extra/images"
#define PERSISTENT WAYDROID_ROOT "/images"
#define CONTEXT ":openwave_waydroid_mount_t:"
#ifndef EROFS_SUPER_MAGIC
#define EROFS_SUPER_MAGIC 0xE0F5E1E2UL
#endif
#ifndef EXT4_SUPER_MAGIC
#define EXT4_SUPER_MAGIC 0xEF53UL
#endif

struct image_kind {
    const char *name, *filename, *mountpoint, *mount_parent, *fstype;
    off_t magic_offset;
    uint32_t magic;
    size_t magic_bytes;
    unsigned long fsmagic;
};
static const struct image_kind SYSTEM_IMAGE = {
    "system", "system.img", SYSTEM_MOUNT, WAYDROID_ROOT, "erofs",
    1024, 0xe0f5e1e2U, 4, EROFS_SUPER_MAGIC
};
static const struct image_kind VENDOR_IMAGE = {
    "vendor", "vendor.img", VENDOR_MOUNT, SYSTEM_MOUNT, "ext4",
    1080, 0xef53U, 2, EXT4_SUPER_MAGIC
};

static int err(const char *what)
{
    fprintf(stderr, "waydroid-mount: %s: %s\n", what, strerror(errno));
    return 1;
}
static int reject(const char *why)
{
    fprintf(stderr, "waydroid-mount: refused: %s\n", why);
    return 1;
}

/* Every pathname component must be real, not a symlink or magic link.
 * openat2 pins the inode to avoid caller-controlled traversal. */
static int open_fixed_path(const char *absolute, int flags)
{
    if (!absolute || absolute[0] != '/' || strstr(absolute, "..")) {
        errno = EINVAL;
        return -1;
    }
    int root = open("/", O_PATH | O_DIRECTORY | O_CLOEXEC);
    if (root == -1)
        return -1;
    struct open_how how = {
        .flags = (uint64_t)(flags | O_CLOEXEC | O_NOFOLLOW),
        .resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS,
    };
    int fd = (int)syscall(SYS_openat2, root, absolute + 1, &how, sizeof(how));
    int saved = errno;
    close(root);
    errno = saved;
    return fd;
}

static int validate_file(int fd, const struct image_kind *image, bool privileged)
{
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 4096)
        return reject("image must be a nonempty regular filesystem image");
    if (privileged && (st.st_uid != 0 || (st.st_mode & (S_IWGRP | S_IWOTH))))
        return reject("image must be root-owned and not group/world writable");
    uint8_t magic[4] = {0};
    if (pread(fd, magic, image->magic_bytes, image->magic_offset)
        != (ssize_t)image->magic_bytes)
        return reject("unable to read image superblock magic");
    uint32_t actual = (uint32_t)magic[0] | ((uint32_t)magic[1] << 8);
    if (image->magic_bytes == 4)
        actual |= ((uint32_t)magic[2] << 16) | ((uint32_t)magic[3] << 24);
    if (actual != image->magic)
        return reject("unexpected filesystem magic; no mount attempted");
    return 0;
}

static const char *store_path(const char *arg)
{
    if (!strcmp(arg, "preinstalled"))
        return PREINSTALLED;
    if (!strcmp(arg, "persistent"))
        return PERSISTENT;
    return NULL;
}
static int open_image(const struct image_kind *kind, const char *store)
{
    char path[256];
    if (!store || snprintf(path, sizeof(path), "%s/%s", store, kind->filename)
        >= (int)sizeof(path)) {
        errno = EINVAL;
        return -1;
    }
    return open_fixed_path(path, O_RDONLY | O_NONBLOCK);
}

static int verify_context(void)
{
    if (getuid() != 0 || geteuid() != 0 || getgid() != 0 || getegid() != 0)
        return reject("privileged operations require root identity");
    char context[256] = {0};
    int fd = open("/proc/self/attr/current", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd == -1)
        return err("cannot read process SELinux context");
    ssize_t nr = read(fd, context, sizeof(context) - 1);
    close(fd);
    if (nr <= 0 || !strstr(context, CONTEXT))
        return reject("wrong SELinux domain; expected openwave_waydroid_mount_t");
    fd = open("/sys/fs/selinux/enforce", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd == -1)
        return err("cannot read SELinux enforcement state");
    char enforcing = 0;
    ssize_t n = read(fd, &enforcing, 1);
    close(fd);
    if (n != 1 || enforcing != '1')
        return reject("SELinux is not enforcing");
    return 0;
}
static int mount_fd_id(int fd, uint64_t *id)
{
    struct statx st = {0};
    if (syscall(SYS_statx, fd, "", AT_EMPTY_PATH, STATX_MNT_ID, &st) != 0)
        return -1;
    if (!(st.stx_mask & STATX_MNT_ID)) {
        errno = ENOTSUP;
        return -1;
    }
    *id = st.stx_mnt_id;
    return 0;
}
static int open_target(const struct image_kind *image, int *parent_fd)
{
    int parent = open_fixed_path(image->mount_parent, O_PATH | O_DIRECTORY);
    if (parent == -1)
        return -1;
    int target = open_fixed_path(image->mountpoint, O_PATH | O_DIRECTORY);
    if (target == -1) {
        int saved = errno;
        close(parent);
        errno = saved;
        return -1;
    }
    struct stat st;
    if (fstat(target, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != 0) {
        close(target);
        close(parent);
        errno = EACCES;
        return -1;
    }
    *parent_fd = parent;
    return target;
}
static int target_is_separate_mount(int target, int parent, bool *separate)
{
    uint64_t a, b;
    if (mount_fd_id(target, &a) || mount_fd_id(parent, &b))
        return -1;
    *separate = a != b;
    return 0;
}
static int fd_string(int fd, char out[64])
{
    return snprintf(out, 64, "/proc/self/fd/%d", fd) < 64 ? 0 : -1;
}

static int mount_image(const struct image_kind *kind, const char *store)
{
    int src = open_image(kind, store);
    if (src == -1)
        return err("opening fixed image");
    if (validate_file(src, kind, true)) {
        close(src);
        return 1;
    }
    int parent = -1;
    int dst = open_target(kind, &parent);
    if (dst == -1) {
        close(src);
        return err("opening fixed destination");
    }
    bool separate = false;
    if (target_is_separate_mount(dst, parent, &separate)) {
        close(dst); close(parent); close(src);
        return err("checking mount identity");
    }
    if (separate) {
        close(dst); close(parent); close(src);
        return reject("destination is already a mountpoint");
    }
    int ctl = open("/dev/loop-control", O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (ctl == -1) {
        close(dst); close(parent); close(src);
        return err("opening loop-control");
    }
    int index = ioctl(ctl, LOOP_CTL_GET_FREE);
    close(ctl);
    if (index < 0 || index > 4095) {
        close(dst); close(parent); close(src);
        return err("allocating a bounded loop device");
    }
    char devname[64];
    snprintf(devname, sizeof(devname), "/dev/loop%d", index);
    int loop = open(devname, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
    if (loop == -1) {
        close(dst); close(parent); close(src);
        return err("opening dedicated loop device");
    }
    struct loop_config config = {0};
    config.fd = (uint32_t)src;
    config.info.lo_flags = LO_FLAGS_READ_ONLY | LO_FLAGS_AUTOCLEAR;
    if (ioctl(loop, LOOP_CONFIGURE, &config) != 0) {
        close(loop); close(dst); close(parent); close(src);
        return err("configuring readonly autoclear loop device");
    }
    char target[64];
    if (fd_string(dst, target)) {
        ioctl(loop, LOOP_CLR_FD);
        close(loop); close(dst); close(parent); close(src);
        return reject("failed to construct pinned target descriptor");
    }
    int rc = mount(devname, target, kind->fstype,
                   MS_RDONLY | MS_NOSUID | MS_NODEV, NULL);
    int saved = errno;
    if (rc != 0)
        ioctl(loop, LOOP_CLR_FD);
    close(loop); close(dst); close(parent); close(src);
    errno = saved;
    if (rc)
        return err("mounting approved readonly image");
    printf("waydroid-mount: %s mounted readonly\n", kind->name);
    return 0;
}

static int unmount_image(const struct image_kind *kind)
{
    int parent = -1;
    int target = open_target(kind, &parent);
    if (target == -1)
        return err("opening fixed unmount target");
    bool separate = false;
    if (target_is_separate_mount(target, parent, &separate)) {
        close(target); close(parent);
        return err("checking unmount target mount ID");
    }
    if (!separate) {
        close(target); close(parent);
        return reject("destination is not a distinct mountpoint");
    }
    struct statfs fs;
    if (fstatfs(target, &fs) || (unsigned long)fs.f_type != kind->fsmagic) {
        close(target); close(parent);
        return reject("unexpected mounted filesystem type");
    }
    char path[64];
    if (fd_string(target, path)) {
        close(target); close(parent);
        return reject("failed to pin unmount target");
    }
    int rc = umount2(path, 0); /* never lazy/recursive unmount */
    int saved = errno;
    close(target); close(parent);
    errno = saved;
    if (rc)
        return err("unmounting fixed image");
    printf("waydroid-mount: %s unmounted\n", kind->name);
    return 0;
}
static void usage(void)
{
    puts("Usage: waydroid-mount --verify-system|--verify-vendor preinstalled|persistent\n"
         "       waydroid-mount --mount-system|--mount-vendor preinstalled|persistent\n"
         "       waydroid-mount --unmount-vendor|--unmount-system\n"
         "This Stage 2A helper is not wired into the upstream Waydroid lifecycle.");
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--help")) {
        usage();
        return 0;
    }
    /* Arbitrary read-only fixture paths exist ONLY in host QA builds.
     * The production executable has no caller-controlled path interface. */
#ifdef OPENWAVE_MOUNT_TESTING
    if (argc == 4 && !strcmp(argv[1], "--inspect-image")) {
        const struct image_kind *kind = !strcmp(argv[2], "system") ? &SYSTEM_IMAGE
            : !strcmp(argv[2], "vendor") ? &VENDOR_IMAGE : NULL;
        if (!kind)
            return reject("unknown image type");
        int fd = open(argv[3], O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0)
            return err("inspecting image");
        int rc = validate_file(fd, kind, false);
        close(fd);
        if (!rc)
            printf("waydroid-mount: %s filesystem image signature OK\n", kind->name);
        return rc;
    }
#endif
    if (argc == 3) {
        const char *store = store_path(argv[2]);
        if (!store)
            return reject("unknown image store");
        const struct image_kind *kind = NULL;
        bool verify = false;
        if (!strcmp(argv[1], "--verify-system")) {
            kind = &SYSTEM_IMAGE; verify = true;
        } else if (!strcmp(argv[1], "--verify-vendor")) {
            kind = &VENDOR_IMAGE; verify = true;
        } else if (!strcmp(argv[1], "--mount-system")) {
            kind = &SYSTEM_IMAGE;
        } else if (!strcmp(argv[1], "--mount-vendor")) {
            kind = &VENDOR_IMAGE;
        } else {
            return reject("unknown operation");
        }
        if (!verify && verify_context())
            return 1;
        if (verify) {
            int fd = open_image(kind, store);
            if (fd < 0)
                return err("opening fixed image");
            int rc = validate_file(fd, kind, true);
            close(fd);
            return rc;
        }
        return mount_image(kind, store);
    }
    if (argc == 2 && (!strcmp(argv[1], "--unmount-vendor") ||
                      !strcmp(argv[1], "--unmount-system"))) {
        if (verify_context())
            return 1;
        return unmount_image(!strcmp(argv[1], "--unmount-vendor")
                             ? &VENDOR_IMAGE : &SYSTEM_IMAGE);
    }
    return reject("invalid argument count or operation");
}
