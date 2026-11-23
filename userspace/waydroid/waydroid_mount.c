/*
 * OpenWave Waydroid rootfs mount boundary (Stage 2A, wired in Stage 7).
 *
 * Fixed operations only: no arbitrary source/destination paths or shell.
 * Upstream Waydroid reaches it through the mount/umount dispatcher
 * (mount_dispatch.py); this helper is the only Waydroid component that holds
 * CAP_SYS_ADMIN and SELinux mount authority (openwave_waydroid_mount_t).
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
#include <sys/xattr.h>
#include <unistd.h>

#define WAYDROID_ROOT "/var/lib/waydroid"
#define SYSTEM_MOUNT WAYDROID_ROOT "/rootfs"
#define VENDOR_MOUNT WAYDROID_ROOT "/rootfs/vendor"
#define PREINSTALLED "/usr/share/waydroid-extra/images"
#define PERSISTENT WAYDROID_ROOT "/images"
#define CONTEXT ":openwave_waydroid_mount_t:"
/*
 * The vendor image is ext4, which the host policy labels from xattrs
 * (fs_use_xattr -> openwave_default_t superblock). Its files carry Android
 * contexts the host policy does not know, so they would all be "unlabeled".
 * A context= mount gives the whole superblock and every inode the Android
 * image type instead, so the helper never needs mount authority over the
 * generic host filesystem type and Android sees one known label.
 */
#define VENDOR_CONTEXT_OPTION "context=system_u:object_r:openwave_android_system_t:s0"
#ifndef EROFS_SUPER_MAGIC
#define EROFS_SUPER_MAGIC 0xE0F5E1E2UL
#endif
#ifndef EXT4_SUPER_MAGIC
#define EXT4_SUPER_MAGIC 0xEF53UL
#endif
#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994UL
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
/* EROFS is labeled by genfscon; only the ext4 vendor image needs context=. */
static const char *image_mount_data(const struct image_kind *kind)
{
    return kind == &VENDOR_IMAGE ? VENDOR_CONTEXT_OPTION : NULL;
}

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

/*
 * Unmount the mount whose root *target is, after the caller validated it
 * through that descriptor. A descriptor on the mount root holds a reference
 * to the mount, and umount2() refuses a mount with extra references (EBUSY:
 * do_umount -> propagate_mount_busy(mnt, 2); stage7g). So: record its mount
 * ID, close it, re-open the fixed final name beneath the pinned parent
 * directory, refuse if a different mount is now on top, and unmount through
 * the pinned parent. Never lazy, recursive or forced; the final component
 * is never followed.
 */
static int unmount_pinned(int *target, int parent, const char *mountpoint)
{
    uint64_t expected, now;
    if (mount_fd_id(*target, &expected))
        return -1;
    close(*target);
    *target = -1;
    const char *name = strrchr(mountpoint, '/');
    if (!name || !name[1] || !strcmp(name, "/.") || !strcmp(name, "/..")) {
        errno = EINVAL;
        return -1;
    }
    name++;
    int again = openat(parent, name, O_PATH | O_NOFOLLOW | O_CLOEXEC);
    if (again == -1)
        return -1;
    int rc = mount_fd_id(again, &now);
    int saved = errno;
    close(again);
    errno = saved;
    if (rc)
        return -1;
    if (now != expected) {
        errno = ESTALE;
        return -1;
    }
    char path[128];
    if (snprintf(path, sizeof(path), "/proc/self/fd/%d/%s", parent, name)
        >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return umount2(path, UMOUNT_NOFOLLOW);
}

/*
 * Bind `source` onto `target` with fixed per-mount attributes, without
 * a remount. open_tree(OPEN_TREE_CLONE) makes a detached, non-recursive copy
 * of the source mount; mount_setattr() sets attributes on that copy only;
 * move_mount() attaches it. The source mount and its superblock are never
 * changed, and if any step fails the detached copy disappears on close(), so
 * no half-configured mount is ever left attached.
 */
static int bind_fd_onto(int source, int target, uint64_t attributes)
{
    int tree = open_tree(source, "", OPEN_TREE_CLONE | OPEN_TREE_CLOEXEC | AT_EMPTY_PATH);
    if (tree == -1)
        return -1;
    struct mount_attr attr = { .attr_set = attributes };
    int rc = mount_setattr(tree, "", AT_EMPTY_PATH, &attr, sizeof(attr));
    if (!rc)
        rc = move_mount(tree, "", target, "",
                        MOVE_MOUNT_F_EMPTY_PATH | MOVE_MOUNT_T_EMPTY_PATH);
    int saved = errno;
    close(tree);
    errno = saved;
    return rc;
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
                   MS_RDONLY | MS_NOSUID | MS_NODEV, image_mount_data(kind));
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
    int rc = unmount_pinned(&target, parent, kind->mountpoint);
    int saved = errno;
    close(target); close(parent);
    errno = saved;
    if (rc)
        return err("unmounting fixed image");
    printf("waydroid-mount: %s unmounted\n", kind->name);
    return 0;
}

/* Waydroid 1.6.3 binds the generated per-session property file into vendor.
 *
 * Android's waydroid-preinit opens /vendor/waydroid.prop read-write
 * (stage5a AVC; android_vendor_waydroid_init waydroid_prop.cpp), so on a
 * read-only bind its open fails with EROFS. Binding the manager's file
 * read-write would let Android modify host-side Waydroid state, and any bind
 * of a file on the host filesystem needs authority over the host superblock to
 * unmount it again. Stage 7 therefore gives Android an isolated copy on its
 * own filesystem: the helper creates a private, size-limited tmpfs with the
 * new mount API (fsopen/fsconfig/fsmount, never attached anywhere) labeled
 * openwave_android_prop_t by context=, copies the validated manager file into
 * it, and binds only that file over the vendor path, nosuid,nodev,noexec.
 * When the bind is unmounted the tmpfs and the copy disappear. Nothing on the
 * host filesystem is created, mounted, remounted or written.
 */
#define PROP_SOURCE WAYDROID_ROOT "/waydroid.prop"
#define PROP_DEST VENDOR_MOUNT "/waydroid.prop"
#define PROP_COPY_NAME "waydroid.prop"
#define PROP_CONTEXT "system_u:object_r:openwave_android_prop_t:s0"
#define PROP_MAX_BYTES 65536
#define PROP_FS_SIZE "131072"

static int open_valid_property(int *fd_out)
{
    int fd = open_fixed_path(PROP_SOURCE, O_RDONLY | O_NONBLOCK);
    if (fd == -1)
        return err("opening fixed Waydroid property source");
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        st.st_size > PROP_MAX_BYTES || st.st_uid != 0 || st.st_nlink != 1 ||
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

/*
 * Create the private tmpfs and the Android copy of the property file. Returns
 * the fsmount() descriptor of the detached tmpfs and an open descriptor of the
 * copy. The caller must keep *root_out open until the copy has been bound:
 * closing it dissolves the detached mount (dissolve_on_fput), after which the
 * copy can no longer be cloned (open_tree returns EINVAL; stage7e).
 */
static int make_property_copy(int source, int *root_out, int *copy_out)
{
    char buffer[PROP_MAX_BYTES];
    ssize_t total = 0, n;
    while (total < (ssize_t)sizeof(buffer) &&
           (n = pread(source, buffer + total, sizeof(buffer) - (size_t)total, total)) > 0)
        total += n;
    if (total <= 0)
        return reject("property source is empty or unreadable");

    int fs = fsopen("tmpfs", FSOPEN_CLOEXEC);
    if (fs == -1)
        return err("creating Android property filesystem");
    if (fsconfig(fs, FSCONFIG_SET_STRING, "context", PROP_CONTEXT, 0) ||
        fsconfig(fs, FSCONFIG_SET_STRING, "size", PROP_FS_SIZE, 0) ||
        fsconfig(fs, FSCONFIG_SET_STRING, "nr_inodes", "4", 0) ||
        fsconfig(fs, FSCONFIG_SET_STRING, "mode", "0755", 0) ||
        fsconfig(fs, FSCONFIG_CMD_CREATE, NULL, NULL, 0)) {
        int saved = errno;
        close(fs);
        errno = saved;
        return err("configuring Android property filesystem");
    }
    int root = fsmount(fs, FSMOUNT_CLOEXEC,
                       MOUNT_ATTR_NOSUID | MOUNT_ATTR_NODEV | MOUNT_ATTR_NOEXEC);
    int saved = errno;
    close(fs);
    if (root == -1) {
        errno = saved;
        return err("creating detached Android property filesystem");
    }
    int copy = openat(root, PROP_COPY_NAME,
                      O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (copy == -1) {
        saved = errno;
        close(root);
        errno = saved;
        return err("creating Android property copy");
    }
    for (ssize_t done = 0; done < total; done += n) {
        n = write(copy, buffer + done, (size_t)(total - done));
        if (n <= 0) {
            saved = n < 0 ? errno : EIO;
            close(copy);
            close(root);
            errno = saved;
            return err("writing Android property copy");
        }
    }
    *root_out = root;
    *copy_out = copy;
    return 0;
}

/* The bound file must be the helper's copy: a single root-owned regular file
 * on a tmpfs whose label only the helper can create. */
static int is_property_copy(int fd)
{
    struct stat st;
    struct statfs fs;
    char path[64], label[sizeof(PROP_CONTEXT) + 1] = {0};
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != 0 || st.st_nlink != 1 ||
        fstatfs(fd, &fs) || (unsigned long)fs.f_type != TMPFS_MAGIC || fd_string(fd, path))
        return 0;
    ssize_t n = getxattr(path, "security.selinux", label, sizeof(label) - 1);
    if (n > 0 && label[n - 1] == '\0')
        n--;
    return n == (ssize_t)strlen(PROP_CONTEXT) && !memcmp(label, PROP_CONTEXT, (size_t)n);
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
    int root = -1, copy = -1;
    if (make_property_copy(source, &root, &copy)) {
        close(target); close(parent); close(source);
        return 1;
    }
    /* Writable for Android's preinit; never setuid, device or executable.
     * Closing root afterwards dissolves the detached tmpfs mount; the bound
     * copy keeps the filesystem alive until it is unbound. */
    int rc = bind_fd_onto(copy, target,
                          MOUNT_ATTR_NOSUID | MOUNT_ATTR_NODEV | MOUNT_ATTR_NOEXEC);
    int saved = errno;
    close(copy); close(root); close(target); close(parent); close(source);
    if (rc) {
        errno = saved;
        return err("binding Android property copy");
    }
    puts("waydroid-mount: Android property copy bound nosuid,nodev,noexec");
    return 0;
}

static int unbind_property(void)
{
    int parent = -1, target = -1;
    if (open_valid_property_target(&parent, &target))
        return 1;
    bool is_mounted = false;
    if (target_is_separate_mount(target, parent, &is_mounted)) {
        close(target); close(parent);
        return err("checking property mount identity");
    }
    if (!is_mounted || !is_property_copy(target)) {
        close(target); close(parent);
        return reject("property target is not a bind of the Android property copy");
    }
    int rc = unmount_pinned(&target, parent, PROP_DEST);
    int saved = errno;
    close(target); close(parent);
    errno = saved;
    if (rc)
        return err("unbinding Android property copy");
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
    int rc = unmount_pinned(&target, parent, kind->image->mountpoint);
    int saved = errno;
    close(target); close(parent);
    errno = saved;
    if (rc)
        return err("unmounting fixed Waydroid overlay");
    printf("waydroid-mount: %s overlay unmounted\n", kind->name);
    return 0;
}


/*
 * Stage 2D: exact host EGL/ODM directory binds needed by pinned upstream
 * Waydroid 1.6.3. No user-supplied paths, no PATH shim, no active callsite.
 * Host sources are OPTIONAL upstream; a missing source fails closed.
 *
 * The runtime policy still denies sys_admin. Do not enable these commands
 * until guest namespace, SELinux, and mount-provenance tests have passed.
 */
struct host_dir_bind {
    const char *name;
    const char *source;
    const char *target;
    const char *parent;
    unsigned long guest_fs_primary, guest_fs_overlay;
};
static const struct host_dir_bind HOST_DIR_BINDS[] = {
    { "egl32", "/vendor/lib/egl", VENDOR_MOUNT "/lib/egl",
      VENDOR_MOUNT "/lib", EXT4_SUPER_MAGIC, OVERLAYFS_SUPER_MAGIC },
    { "egl64", "/vendor/lib64/egl", VENDOR_MOUNT "/lib64/egl",
      VENDOR_MOUNT "/lib64", EXT4_SUPER_MAGIC, OVERLAYFS_SUPER_MAGIC },
    { "odm", "/odm", SYSTEM_MOUNT "/odm_extra",
      SYSTEM_MOUNT, EROFS_SUPER_MAGIC, OVERLAYFS_SUPER_MAGIC },
    { "vendor-odm", "/vendor/odm", SYSTEM_MOUNT "/odm_extra",
      SYSTEM_MOUNT, EROFS_SUPER_MAGIC, OVERLAYFS_SUPER_MAGIC },
};
#define HOST_BIND_COUNT (sizeof(HOST_DIR_BINDS) / sizeof(HOST_DIR_BINDS[0]))

static int host_dir_bind_op(const struct host_dir_bind *kind, bool unbind)
{
    int source = owned_directory(kind->source);
    if (source < 0)
        return err("opening approved root-owned host source directory");
    int parent = owned_directory(kind->parent);
    if (parent < 0) {
        close(source);
        return err("opening approved Android destination parent");
    }
    struct statfs parent_fs;
    if (fstatfs(parent, &parent_fs) ||
        ((unsigned long)parent_fs.f_type != kind->guest_fs_primary &&
         (unsigned long)parent_fs.f_type != kind->guest_fs_overlay)) {
        close(parent); close(source);
        return reject("Android bind target parent is not expected mounted filesystem");
    }
    int target = owned_directory(kind->target);
    if (target < 0) {
        close(parent); close(source);
        return err("opening existing approved Android bind destination");
    }
    struct stat source_st, target_st;
    bool separate = false;
    if (fstat(source, &source_st) || fstat(target, &target_st) ||
        target_is_separate_mount(target, parent, &separate)) {
        close(target); close(parent); close(source);
        return err("validating host bind source and destination inodes");
    }
    if (source_st.st_dev == target_st.st_dev &&
        source_st.st_ino == target_st.st_ino && !unbind) {
        close(target); close(parent); close(source);
        return reject("host and Android target must not already be identical");
    }
    if (!unbind && separate) {
        close(target); close(parent); close(source);
        return reject("Android host-bind target is already a mountpoint");
    }
    if (unbind && (!separate || source_st.st_dev != target_st.st_dev ||
                   source_st.st_ino != target_st.st_ino)) {
        close(target); close(parent); close(source);
        return reject("host-bind target is not mounted from approved source");
    }
    int rc;
    int saved;
    if (unbind) {
        rc = unmount_pinned(&target, parent, kind->target);
        saved = errno;
    } else {
        /* Read-only from the moment it is attached; never remounted. */
        rc = bind_fd_onto(source, target, MOUNT_ATTR_RDONLY | MOUNT_ATTR_NOSUID |
                                          MOUNT_ATTR_NODEV);
        saved = errno;
    }
    close(target); close(parent); close(source);
    errno = saved;
    if (rc)
        return err(unbind ? "unbinding fixed host directory"
                          : "binding fixed host directory readonly");
    printf("waydroid-mount: %s host directory %s\n",
           kind->name, unbind ? "unbound" : "bound readonly");
    return 0;
}
static int dispatch_host_dir_bind(const char *argument)
{
    const char *verb = NULL;
    bool unbind = false;
    if (!strncmp(argument, "--bind-", 7)) {
        verb = argument + 7;
    } else if (!strncmp(argument, "--unbind-", 9)) {
        verb = argument + 9;
        unbind = true;
    } else {
        return -1;
    }
    for (size_t i = 0; i < HOST_BIND_COUNT; ++i) {
        if (!strcmp(verb, HOST_DIR_BINDS[i].name)) {
            if (verify_context())
                return 1;
            return host_dir_bind_op(&HOST_DIR_BINDS[i], unbind);
        }
    }
    return -1; /* preserve fail-closed unknown-operation handling */
}

static void usage(void)
{
    puts("Usage: waydroid-mount --verify-system|--verify-vendor preinstalled|persistent\n"
         "       waydroid-mount --mount-system|--mount-vendor preinstalled|persistent\n"
         "       waydroid-mount --unmount-vendor|--unmount-system\n"
         "       waydroid-mount --bind-prop|--unbind-prop\n"
         "       waydroid-mount --overlay-system|--overlay-vendor\n"
         "       waydroid-mount --unoverlay-vendor|--unoverlay-system\n"
         "       waydroid-mount --bind-egl32|--bind-egl64|--bind-odm|--bind-vendor-odm\n"
         "       waydroid-mount --unbind-egl32|--unbind-egl64|--unbind-odm|--unbind-vendor-odm\n"
         "Reached from upstream Waydroid only through the fixed mount dispatcher.");
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
    if (argc == 2) {
        int host_bind_status = dispatch_host_dir_bind(argv[1]);
        if (host_bind_status >= 0)
            return host_bind_status;
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
