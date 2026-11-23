#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

struct service_identity {
    const char *name;
    const char *pidfile;
    const char *executable;
    uid_t process_uid;
};

static const struct service_identity services[] = {
#ifndef OPENWAVE_APPD_EXECUTABLE
#define OPENWAVE_APPD_EXECUTABLE "/System/Services/appd"
#endif
#ifndef OPENWAVE_OCEAN_UID
#define OPENWAVE_OCEAN_UID 1000
#endif
#ifndef OPENWAVE_ROOT_SERVICE_UID
#define OPENWAVE_ROOT_SERVICE_UID 0
#endif
#ifndef OPENWAVE_OCEAN_EXECUTABLE
#define OPENWAVE_OCEAN_EXECUTABLE "/System/Applications/com.openwave.shell/bin/ocean-launcher"
#endif
    {"appd", "/run/openwave/appd.pid", OPENWAVE_APPD_EXECUTABLE, OPENWAVE_ROOT_SERVICE_UID},
    {"permissiond", "/run/openwave/permissiond.pid", "/System/Services/permissiond", OPENWAVE_ROOT_SERVICE_UID},
    {"portal", "/run/openwave/portal.pid", "/System/Services/openwave-portal", OPENWAVE_ROOT_SERVICE_UID},
    {"promptd", "/run/openwave/promptd.pid", "/System/Services/promptd", OPENWAVE_ROOT_SERVICE_UID},
    {"wallpaperagentd", "/run/openwave/wallpaper.pid", "/System/Services/wallpaperagentd", OPENWAVE_ROOT_SERVICE_UID},
    {"powerd", "/run/openwave/powerd.pid", "/System/Services/powerd", OPENWAVE_ROOT_SERVICE_UID},
    {"connectivityd", "/run/openwave/connectivityd.pid", "/System/Services/connectivityd", OPENWAVE_ROOT_SERVICE_UID},
    {"ocean", "/run/openwave/ocean.pid", OPENWAVE_OCEAN_EXECUTABLE, OPENWAVE_OCEAN_UID},
};

#ifndef OPENWAVE_RUNTIME_DIR
#define OPENWAVE_RUNTIME_DIR "/run/openwave"
#endif
#ifndef OPENWAVE_PROC_DIR
#define OPENWAVE_PROC_DIR "/proc"
#endif
#ifndef OPENWAVE_OWNER_UID
#define OPENWAVE_OWNER_UID 0
#endif

static int read_all(int fd, char *buffer, size_t capacity)
{
    size_t used = 0;
    while (used + 1 < capacity) {
        ssize_t count = read(fd, buffer + used, capacity - used - 1);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return -1;
        if (!count) break;
        used += (size_t)count;
    }
    if (used + 1 == capacity) {
        char extra;
        if (read(fd, &extra, 1) > 0) { errno = EOVERFLOW; return -1; }
    }
    buffer[used] = '\0';
    return (int)used;
}

static int parse_pid(const char *text, pid_t *pid)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || end == text || value < 2 || value > INT_MAX) return -1;
    while (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t') ++end;
    if (*end) return -1;
    *pid = (pid_t)value;
    return 0;
}

static unsigned long long process_start(pid_t pid)
{
    char path[64], text[4096];
    snprintf(path, sizeof(path), "%s/%ld/stat", OPENWAVE_PROC_DIR, (long)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || read_all(fd, text, sizeof(text)) <= 0) {
        if (fd >= 0) close(fd);
        return 0;
    }
    close(fd);
    char *end = strrchr(text, ')');
    if (!end || end[1] != ' ') return 0;
    char *save = NULL;
    char *field = strtok_r(end + 2, " \t\r\n", &save);
    for (int index = 0; field; ++index, field = strtok_r(NULL, " \t\r\n", &save)) {
        if (index == 19) {
            char *number_end = NULL;
            errno = 0;
            unsigned long long ticks = strtoull(field, &number_end, 10);
            return errno || number_end == field || *number_end ? 0 : ticks;
        }
    }
    return 0;
}

static int process_is_expected(pid_t pid, const char *executable, uid_t expected_uid)
{
    char path[64], text[4096], actual[PATH_MAX + 1];
    snprintf(path, sizeof(path), "%s/%ld/status", OPENWAVE_PROC_DIR, (long)pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || read_all(fd, text, sizeof(text)) <= 0) {
        if (fd >= 0) close(fd);
        return 0;
    }
    close(fd);
    char *line = text;
    int expected_owner = 0;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next++ = '\0';
        if (!strncmp(line, "Uid:", 4)) {
            unsigned real, effective, saved, filesystem;
            if (sscanf(line + 4, "%u %u %u %u", &real, &effective, &saved, &filesystem) == 4
                && real == expected_uid && effective == expected_uid
                && saved == expected_uid && filesystem == expected_uid)
                expected_owner = 1;
            break;
        }
        line = next;
    }
    if (!expected_owner) return 0;
    snprintf(path, sizeof(path), "%s/%ld/exe", OPENWAVE_PROC_DIR, (long)pid);
    ssize_t length = readlink(path, actual, sizeof(actual) - 1);
    if (length < 0) return 0;
    actual[length] = '\0';
    return strcmp(actual, executable) == 0;
}

static int write_identity(const struct service_identity *service, pid_t pid,
                          unsigned long long ticks)
{
    int dirfd = open(OPENWAVE_RUNTIME_DIR, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dirfd < 0) return -1;
    struct stat directory;
    if (fstat(dirfd, &directory) || !S_ISDIR(directory.st_mode) || directory.st_uid != OPENWAVE_OWNER_UID
        || (directory.st_mode & 0022)) {
        close(dirfd); errno = EPERM; return -1;
    }

    const char *base = strrchr(service->pidfile, '/') + 1;
    char pidname[NAME_MAX + 1], marker[NAME_MAX + 1], temporary[NAME_MAX + 1];
    snprintf(pidname, sizeof(pidname), "%s", base);
    snprintf(marker, sizeof(marker), "%s.identity", base);
    int pidfd = openat(dirfd, pidname, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat pidstat;
    char pidtext[64];
    pid_t recorded;
    if (pidfd < 0 || fstat(pidfd, &pidstat) || !S_ISREG(pidstat.st_mode)
        || pidstat.st_uid != OPENWAVE_OWNER_UID || (pidstat.st_mode & 0022)
        || read_all(pidfd, pidtext, sizeof(pidtext)) <= 0 || parse_pid(pidtext, &recorded)
        || recorded != pid) {
        int saved = errno ? errno : EPERM;
        if (pidfd >= 0) close(pidfd);
        close(dirfd); errno = saved; return -1;
    }
    close(pidfd);

    char content[96];
    int content_len = snprintf(content, sizeof(content), "%ld %u %llu\n", (long)pid,
                               (unsigned)service->process_uid, ticks);
    if (content_len <= 0 || (size_t)content_len >= sizeof(content)) {
        close(dirfd); errno = EOVERFLOW; return -1;
    }
    int fd = -1;
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        snprintf(temporary, sizeof(temporary), ".%s.identity.%ld.%u", service->name,
                 (long)getpid(), attempt);
        fd = openat(dirfd, temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd >= 0 || errno != EEXIST) break;
    }
    if (fd < 0) { close(dirfd); return -1; }
    int failed = fchown(fd, OPENWAVE_OWNER_UID, OPENWAVE_OWNER_UID) || fchmod(fd, 0600);
    size_t written = 0;
    while (!failed && written < (size_t)content_len) {
        ssize_t count = write(fd, content + written, (size_t)content_len - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { failed = 1; break; }
        written += (size_t)count;
    }
    /* Clients running as Ocean or an isolated UID authenticate these public
     * PID/UID/start-time proofs. Root ownership and no write bits preserve
     * authenticity; the record contains no secret. Publish only complete data. */
    if (!failed && (fchmod(fd, 0644) || fsync(fd))) failed = 1;
    int saved = errno;
    if (close(fd) && !failed) { failed = 1; saved = errno; }
    if (!failed && renameat(dirfd, temporary, dirfd, marker)) { failed = 1; saved = errno; }
    if (!failed && fsync(dirfd)) { failed = 1; saved = errno; }
    if (failed) unlinkat(dirfd, temporary, 0);
    close(dirfd);
    if (failed) { errno = saved ? saved : EIO; return -1; }
    return 0;
}

static int invalidate_identity(const struct service_identity *service)
{
    int dirfd = open(OPENWAVE_RUNTIME_DIR, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dirfd < 0) return errno == ENOENT ? 0 : -1;
    struct stat directory;
    if (fstat(dirfd, &directory) || !S_ISDIR(directory.st_mode)
        || directory.st_uid != OPENWAVE_OWNER_UID || (directory.st_mode & 0022)) {
        close(dirfd); errno = EPERM; return -1;
    }
    const char *base = strrchr(service->pidfile, '/') + 1;
    char marker[NAME_MAX + 1];
    if (snprintf(marker, sizeof(marker), "%s.identity", base) >= (int)sizeof(marker)) {
        close(dirfd); errno = ENAMETOOLONG; return -1;
    }
    int fd = openat(dirfd, marker, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 && errno == ENOENT) { close(dirfd); return 0; }
    struct stat record;
    if (fd < 0 || fstat(fd, &record) || !S_ISREG(record.st_mode)
        || record.st_uid != OPENWAVE_OWNER_UID || (record.st_mode & 0022)) {
        int saved = errno ? errno : EPERM;
        if (fd >= 0) close(fd);
        close(dirfd); errno = saved; return -1;
    }
    close(fd);
    int failed = unlinkat(dirfd, marker, 0);
    int saved = errno;
    if (!failed && fsync(dirfd)) { failed = 1; saved = errno; }
    close(dirfd);
    if (failed) { errno = saved; return -1; }
    return 0;
}

int main(int argc, char **argv)
{
    int invalidate = argc == 3 && !strcmp(argv[1], "--invalidate");
    if ((!invalidate && argc != 2) || (invalidate && argc != 3)) {
        fprintf(stderr, "usage: %s [--invalidate] SERVICE\n", argv[0]); return 2;
    }
    const char *requested = argv[invalidate ? 2 : 1];
    const struct service_identity *service = NULL;
    for (size_t i = 0; i < sizeof(services) / sizeof(services[0]); ++i)
        if (!strcmp(requested, services[i].name)) { service = &services[i]; break; }
    if (!service) { fprintf(stderr, "unknown supervised service\n"); return 2; }
    if (geteuid() != 0) { fprintf(stderr, "must run as root\n"); return 1; }
    if (invalidate) {
        if (invalidate_identity(service)) { perror("invalidate supervised service identity"); return 1; }
        return 0;
    }
    const char *slash = strrchr(service->pidfile, '/');
    char pidpath[PATH_MAX];
    size_t parent_len = (size_t)(slash - service->pidfile);
    if (parent_len + 1 >= sizeof(pidpath)) return 1;
    memcpy(pidpath, service->pidfile, parent_len);
    pidpath[parent_len] = '\0';
    int dirfd = open(pidpath, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dirfd < 0) { perror("open service runtime directory"); return 1; }
    struct stat directory;
    int dir_ok = !fstat(dirfd, &directory) && S_ISDIR(directory.st_mode)
        && directory.st_uid == OPENWAVE_OWNER_UID && !(directory.st_mode & 0022);
    close(dirfd);
    if (!dir_ok) { fprintf(stderr, "unsafe service runtime directory\n"); return 1; }
    int fd = open(service->pidfile, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    char text[64]; pid_t pid; struct stat st;
    int valid = fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == OPENWAVE_OWNER_UID
        && !(st.st_mode & 0022) && read_all(fd, text, sizeof(text)) > 0 && !parse_pid(text, &pid);
    if (fd >= 0) close(fd);
    unsigned long long ticks = valid ? process_start(pid) : 0;
    if (!valid || !ticks || !process_is_expected(pid, service->executable, service->process_uid)
        || process_start(pid) != ticks) {
        fprintf(stderr, "pidfile does not identify the expected live supervised service\n"); return 1;
    }
    if (write_identity(service, pid, ticks)) { perror("register supervised service identity"); return 1; }
    return 0;
}
