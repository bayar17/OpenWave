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
#ifndef OVERLAYFS_SUPER_MAGIC
#define OVERLAYFS_SUPER_MAGIC 0x794c7630UL
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

/* Waydroid 1.6.3 binds the generated per-session property file into vendor.
 * Stage 2B exposes one fixed file bind, but the SELinux mount authority
 * remains denied and upstream is deliberately not redirected to this helper.
 */
#define PROP_SOURCE WAYDROID_ROOT "/waydroid.prop"
#define PROP_DEST VENDOR_MOUNT "/waydroid.prop"

static int open_valid_property(int *fd_out)
{
    int fd = open_fixed_path(PROP_SOURCE, O_RDONLY | O_NONBLOCK);
    if (fd == -1)
        return err("opening fixed Waydroid property source");
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        st.st_size > 65536 || st.st_uid != 0 || st.st_nlink != 1 ||
        (st.st_mode & (S_IWGRP | S_IWOTH))) {
        close(fd);
        return reject("property source must be a small root-owned regular file");
    }
    *fd_out = fd;
    return 0;
}

static int open_valid_property_target(int *parent_out, int *target_out)
{
    int parent = open_fixed_path(VENDOR_MOUNT, O_PATH | O_DIRECTORY);
    if (parent == -1)
        return err("opening vendor directory");
    struct statfs fs;
    if (fstatfs(parent, &fs) ||
        ((unsigned long)fs.f_type != EXT4_SUPER_MAGIC &&
         (unsigned long)fs.f_type != OVERLAYFS_SUPER_MAGIC)) {
        close(parent);
        return reject("vendor must be an ext4 image or approved overlayfs layer");
    }
    int target = open_fixed_path(PROP_DEST, O_PATH);
    if (target == -1) {
        close(parent);
        return err("opening existing vendor property target");
    }
    struct stat st;
    if (fstat(target, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0) {
        close(parent);
        close(target);
        return reject("vendor property target must be an existing root-owned file");
    }
    *parent_out = parent;
    *target_out = target;
    return 0;
}

static int bind_property(void)
{
    int source = -1;
    if (open_valid_property(&source))
        return 1;
    int parent = -1, target = -1;
    if (open_valid_property_target(&parent, &target)) {
        close(source);
        return 1;
    }
    bool is_mounted = false;
    if (target_is_separate_mount(target, parent, &is_mounted)) {
        close(target); close(parent); close(source);
        return err("checking vendor property mount IDs");
    }
    if (is_mounted) {
        close(target); close(parent); close(source);
        return reject("property target is already a mountpoint");
    }
    char source_path[64], target_path[64];
    if (fd_string(source, source_path) || fd_string(target, target_path)) {
        close(target); close(parent); close(source);
        return reject("could not construct pinned property descriptor");
    }
    int rc = mount(source_path, target_path, NULL, MS_BIND, NULL);
    int saved = errno;
    if (!rc) {
        /* MS_BIND alone does not impose readonly on the new mount. */
        rc = mount(NULL, target_path, NULL,
                   MS_REMOUNT | MS_BIND | MS_RDONLY | MS_NOSUID | MS_NODEV, NULL);
        saved = errno;
        if (rc)
            (void)umount2(target_path, 0);
    }
    close(target); close(parent); close(source);
    errno = saved;
    if (rc)
        return err("binding readonly property file");
    puts("waydroid-mount: property file bound readonly");
    return 0;
}

static int unbind_property(void)
{
    int source = -1;
    if (open_valid_property(&source))
        return 1;
    int parent = -1, target = -1;
    if (open_valid_property_target(&parent, &target)) {
        close(source);
        return 1;
    }
    bool is_mounted = false;
    if (target_is_separate_mount(target, parent, &is_mounted)) {
        close(source); close(target); close(parent);
        return err("checking property mount identity");
    }
    struct stat source_st, target_st;
    if (!is_mounted || fstat(source, &source_st) ||
        fstat(target, &target_st) || source_st.st_dev != target_st.st_dev ||
        source_st.st_ino != target_st.st_ino) {
        close(source); close(target); close(parent);
        return reject("property target is not a bind of the approved source");
    }
    char path[64];
    if (fd_string(target, path)) {
        close(source); close(target); close(parent);
        return reject("could not pin property unmount target");
    }
    int rc = umount2(path, 0); /* no lazy or recursive unmount */
    int saved = errno;
    close(source); close(target); close(parent);
    errno = saved;
    if (rc)
        return err("unbinding fixed Waydroid property file");
    puts("waydroid-mount: property file unbound");
    return 0;
}


/*
 * Stage 2C: strict, fixed-path overlay operations matching the pinned
 * Waydroid helper sequence. No dynamic source, destination or mount options.
 *
 * These are staged only. The helper has NO SELinux sys_admin grant and no
 * upstream Waydroid integration. A successful host C compile is not a
 * substitute for kernel, mount namespace and SELinux validation in QEMU.
 */
#define OVERLAY_ROOT WAYDROID_ROOT "/overlay"
#define OVERLAY_RW WAYDROID_ROOT "/overlay_rw"
#define OVERLAY_WORK WAYDROID_ROOT "/overlay_work"

struct overlay_kind {
    const char *name, *lower_overlay, *upper, *work;
    const struct image_kind *image;
};
static const struct overlay_kind SYSTEM_OVERLAY = {
    "system", OVERLAY_ROOT, OVERLAY_RW "/system",
    OVERLAY_WORK "/system", &SYSTEM_IMAGE
};
static const struct overlay_kind VENDOR_OVERLAY = {
    "vendor", OVERLAY_ROOT "/vendor", OVERLAY_RW "/vendor",
    OVERLAY_WORK "/vendor", &VENDOR_IMAGE
};

static int owned_directory(const char *path)
{
    int fd = open_fixed_path(path, O_PATH | O_DIRECTORY);
    if (fd < 0)
        return -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISDIR(st.st_mode) || st.st_uid != 0 ||
        (st.st_mode & (S_IWGRP | S_IWOTH))) {
        close(fd);
        errno = EACCES;
        return -1;
    }
    return fd;
}

static int overlay_image(const struct overlay_kind *kind)
{
    int parent = -1;
    int target = open_target(kind->image, &parent);
    if (target < 0)
        return err("opening fixed overlay target");
    bool mounted = false;
    struct statfs fs;
    if (target_is_separate_mount(target, parent, &mounted) ||
        fstatfs(target, &fs)) {
        close(target); close(parent);
        return err("checking backing image mount");
    }
    if (!mounted || (unsigned long)fs.f_type != kind->image->fsmagic) {
        close(target); close(parent);
        return reject("overlay requires the expected EROFS/ext4 backing mount");
    }

    int lower = owned_directory(kind->lower_overlay);
    int upper = owned_directory(kind->upper);
    int work = owned_directory(kind->work);
    if (lower < 0 || upper < 0 || work < 0) {
        if (lower >= 0) close(lower);
        if (upper >= 0) close(upper);
        if (work >= 0) close(work);
        close(target); close(parent);
        return err("opening fixed root-owned overlay directories");
    }
    struct stat us, ws;
    if (fstat(upper, &us) || fstat(work, &ws) ||
        us.st_dev != ws.st_dev || us.st_ino == ws.st_ino) {
        close(work); close(upper); close(lower);
        close(target); close(parent);
        return reject("overlay upper/work must be distinct directories on one filesystem");
    }
    char lower_a[64], lower_b[64], upper_path[64], work_path[64], dest[64];
    if (fd_string(lower, lower_a) || fd_string(target, lower_b) ||
        fd_string(upper, upper_path) || fd_string(work, work_path) ||
        fd_string(target, dest)) {
        close(work); close(upper); close(lower);
        close(target); close(parent);
        return reject("unable to pin an overlay path descriptor");
    }
    char options[512];
    int len = snprintf(options, sizeof(options),
        "lowerdir=%s:%s,upperdir=%s,workdir=%s,xino=off",
        lower_a, lower_b, upper_path, work_path);
    if (len < 0 || (size_t)len >= sizeof(options)) {
        close(work); close(upper); close(lower);
        close(target); close(parent);
        return reject("approved overlay options overflow fixed buffer");
    }
    /*
     * Waydroid's mount_overlay(readonly=True) requests -o ro even with
     * upper/work. Preserve that request and do not retry with write flags.
     * Guest kernels may reject the combination; that is a validation gate.
     */
    int rc = mount("overlay", dest, "overlay",
                   MS_RDONLY | MS_NOSUID | MS_NODEV, options);
    int saved = errno;
    close(work); close(upper); close(lower);
    close(target); close(parent);
    errno = saved;
    if (rc)
        return err("mounting fixed readonly overlay");
    printf("waydroid-mount: %s overlay mounted readonly\n", kind->name);
    return 0;
}

static int unoverlay_image(const struct overlay_kind *kind)
{
    int parent = -1;
    int target = open_target(kind->image, &parent);
    if (target < 0)
        return err("opening fixed overlay teardown target");
    bool mounted = false;
    struct statfs fs;
    if (target_is_separate_mount(target, parent, &mounted) ||
        fstatfs(target, &fs)) {
        close(target); close(parent);
        return err("checking fixed overlay mount identity");
    }
    if (!mounted || (unsigned long)fs.f_type != OVERLAYFS_SUPER_MAGIC) {
        close(target); close(parent);
        return reject("expected distinct overlayfs mount is absent");
    }
    /* Do not detach a parent while child mounts are still attached. */
    const char *child = kind == &VENDOR_OVERLAY ? PROP_DEST : VENDOR_MOUNT;
    int child_fd = open_fixed_path(child, O_PATH);
    if (child_fd >= 0) {
        uint64_t target_id, child_id;
        if (mount_fd_id(target, &target_id) ||
            mount_fd_id(child_fd, &child_id) ||
            target_id != child_id) {
            close(child_fd); close(target); close(parent);
            return reject("detach nested vendor/property mount before parent overlay");
        }
        close(child_fd);
    } else if (errno != ENOENT) {
        close(target); close(parent);
        return err("inspecting nested mount before overlay teardown");
    }
    char dest[64];
    if (fd_string(target, dest)) {
        close(target); close(parent);
        return reject("unable to pin overlay unmount descriptor");
    }
    int rc = umount2(dest, 0); /* no lazy or recursive detach */
    int saved = errno;
    close(target); close(parent);
    errno = saved;
    if (rc)
        return err("unmounting fixed Waydroid overlay");
    printf("waydroid-mount: %s overlay unmounted\n", kind->name);
    return 0;
}

static void usage(void)
{
    puts("Usage: waydroid-mount --verify-system|--verify-vendor preinstalled|persistent\n"
         "       waydroid-mount --mount-system|--mount-vendor preinstalled|persistent\n"
         "       waydroid-mount --unmount-vendor|--unmount-system\n"
         "       waydroid-mount --bind-prop|--unbind-prop\n"
         "       waydroid-mount --overlay-system|--overlay-vendor\n"
         "       waydroid-mount --unoverlay-vendor|--unoverlay-system\n"
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
    if (argc == 2 && (!strcmp(argv[1], "--overlay-system") ||
                      !strcmp(argv[1], "--overlay-vendor") ||
                      !strcmp(argv[1], "--unoverlay-system") ||
                      !strcmp(argv[1], "--unoverlay-vendor"))) {
        if (verify_context())
            return 1;
        const struct overlay_kind *kind =
            strstr(argv[1], "vendor") ? &VENDOR_OVERLAY : &SYSTEM_OVERLAY;
        return strncmp(argv[1], "--unoverlay-", 12) == 0
            ? unoverlay_image(kind) : overlay_image(kind);
    }
    if (argc == 2 && (!strcmp(argv[1], "--bind-prop") ||
                      !strcmp(argv[1], "--unbind-prop"))) {
        if (verify_context())
            return 1;
        return !strcmp(argv[1], "--bind-prop") ? bind_property() : unbind_property();
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
