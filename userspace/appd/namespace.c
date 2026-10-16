#define _GNU_SOURCE
#include "appd.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void argument(GPtrArray *args, const char *value) { g_ptr_array_add(args, g_strdup(value)); }
static void mount_path(GPtrArray *args, const char *option, const char *source, const char *target)
{
    char *parent = g_path_get_dirname(target);
    argument(args, "--dir"); argument(args, parent); g_free(parent);
    argument(args, option); argument(args, source); argument(args, target);
}
static void namespace_child(gpointer parent)
{
    sigset_t mask; sigemptyset(&mask); sigprocmask(SIG_SETMASK, &mask, NULL);
    if (setsid() < 0 || prctl(PR_SET_PDEATHSIG, SIGKILL) || getppid() != GPOINTER_TO_INT(parent)) _exit(126);
}
static gboolean read_until(int fd, char *text, size_t capacity, gboolean single, gint64 deadline)
{
    size_t used = 0;
    while (used + 1 < capacity) {
        gint64 remaining = (deadline - g_get_monotonic_time()) / 1000;
        if (remaining <= 0) { errno = ETIMEDOUT; return FALSE; }
        struct pollfd event = {.fd = fd, .events = POLLIN};
        int result = poll(&event, 1, (int)remaining);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) { if (!result) errno = ETIMEDOUT; return FALSE; }
        ssize_t count = read(fd, text + used, capacity - used - 1);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (count < 0) return FALSE;
        if (!count) { text[used] = 0; return used > 0; }
        used += count; text[used] = 0;
        if (single || strchr(text, '}')) return TRUE;
    }
    errno = EOVERFLOW; return FALSE;
}

gboolean appd_prepare_namespace(Instance *instance, GError **error)
{
    if (appd_trusted_app(instance->app)) return TRUE;
    const char *keeper = "/System/Services/openwave-namespace-keeper";
    if (geteuid() || !appd_verified_file("/usr/bin/bwrap", TRUE, error)
        || !appd_verified_file(keeper, TRUE, error)) {
        if (error && !*error) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Bubblewrap namespace setup requires the supervised system daemon");
        return FALSE;
    }
    int info[2] = {-1, -1}, ready[2] = {-1, -1};
    int descriptors[4] = {-1, -1, -1, -1}; GPid child = 0;
    gboolean okay = FALSE;
    const char *stage = "launch";
    GPtrArray *args = g_ptr_array_new_with_free_func(g_free);
    argument(args, "/usr/bin/bwrap"); argument(args, "--unshare-ipc"); argument(args, "--unshare-uts");
    argument(args, "--hostname"); argument(args, "openwave-app");
    argument(args, "--cap-drop"); argument(args, "ALL"); argument(args, "--die-with-parent");
    argument(args, "--info-fd"); argument(args, "3");
    const char *runtime[] = {"/usr", "/lib", "/lib64", "/etc", "/proc", "/sys", NULL};
    for (guint i = 0; runtime[i]; ++i) mount_path(args, "--ro-bind-try", runtime[i], runtime[i]);
    mount_path(args, "--ro-bind", instance->app->bundle, instance->app->bundle);
    mount_path(args, "--ro-bind", keeper, keeper);
    mount_path(args, "--ro-bind", "/run/openwave", "/run/openwave");
    argument(args, "--tmpfs"); argument(args, "/run/openwave/apps");
    argument(args, "--tmpfs"); argument(args, "/run/openwave/appd");
    mount_path(args, "--ro-bind", "/run/dbus", "/run/dbus");
    mount_path(args, "--bind", instance->data, instance->data);
    mount_path(args, "--bind", instance->runtime, instance->runtime);
    argument(args, "--dir"); argument(args, "/dev");
    const char *devices[] = {"/dev/null", "/dev/zero", "/dev/random", "/dev/urandom", "/dev/dri", NULL};
    for (guint i = 0; devices[i]; ++i) mount_path(args, "--dev-bind-try", devices[i], devices[i]);
    char *shm = g_build_filename(instance->runtime, "shm", NULL);
    mount_path(args, "--bind", shm, "/dev/shm"); g_free(shm);
    char *tmp = g_build_filename(instance->runtime, "tmp", NULL);
    argument(args, "--symlink"); argument(args, tmp); argument(args, "/tmp"); g_free(tmp);
    argument(args, "--chdir"); argument(args, "/"); argument(args, "--"); argument(args, keeper);
    g_ptr_array_add(args, NULL);
    if (pipe2(info, O_CLOEXEC | O_NONBLOCK) || pipe2(ready, O_CLOEXEC | O_NONBLOCK)) goto done;
    const int sources[] = {info[1], ready[1]}, targets[] = {3, 4};
    const char *environment[] = {"PATH=/usr/bin:/bin", "LC_ALL=C", NULL};
    if (!g_spawn_async_with_pipes_and_fds("/", (const char *const *)args->pdata, environment,
        G_SPAWN_DO_NOT_REAP_CHILD, namespace_child, GINT_TO_POINTER(getpid()), -1, -1, -1,
        sources, targets, 2, &child, NULL, NULL, NULL, error)) goto done;
    close(info[1]); info[1] = -1; close(ready[1]); ready[1] = -1;
    gint64 deadline = g_get_monotonic_time() + 5000000;
    char details[4096] = {0}, initialized[8] = {0};
    stage = "process information";
    if (!read_until(info[0], details, sizeof(details), FALSE, deadline)) goto done;
    stage = "namespace readiness";
    if (!read_until(ready[0], initialized, sizeof(initialized), TRUE, deadline) || initialized[0] != '1') goto done;
    const char *pid_field = strstr(details, "\"child-pid\":");
    guint64 pid = pid_field ? g_ascii_strtoull(pid_field + strlen("\"child-pid\":"), NULL, 10) : 0;
    if (pid < 2 || pid > INT_MAX) { errno = EPROTO; goto done; }
    stage = "namespace descriptors";
    const char *names[] = {"ns/mnt", "ns/ipc", "ns/uts", "root"};
    for (guint i = 0; i < G_N_ELEMENTS(names); ++i) {
        char *path = g_strdup_printf("/proc/%u/%s", (guint)pid, names[i]);
        descriptors[i] = open(path, (i == 3 ? O_DIRECTORY : 0) | O_RDONLY | O_CLOEXEC); g_free(path);
        if (descriptors[i] < 0) goto done;
    }
    stage = "namespace verification";
    for (guint i = 0; i < 3; ++i) {
        struct stat namespace, current;
        char *path = g_strdup_printf("/proc/self/%s", names[i]);
        int result = stat(path, &current); g_free(path);
        if (result || fstat(descriptors[i], &namespace) || namespace.st_ino == current.st_ino) { errno = EPROTO; goto done; }
    }
    memcpy(instance->namespace_fds, descriptors, sizeof(descriptors));
    instance->namespace_sandboxed = TRUE; okay = TRUE;
done:
    {
        int saved = errno;
        if (child > 0) {
            kill(-child, SIGKILL); kill(child, SIGKILL);
            while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
            g_spawn_close_pid(child);
        }
        for (guint i = 0; i < 2; ++i) { if (info[i] >= 0) close(info[i]); if (ready[i] >= 0) close(ready[i]); }
        if (!okay) {
            for (guint i = 0; i < 4; ++i) if (descriptors[i] >= 0) close(descriptors[i]);
            if (error && !*error) g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved), "Bubblewrap %s failed: %s", stage, g_strerror(saved));
            g_warning("appd: Bubblewrap %s failed: %s", stage, error && *error ? (*error)->message : g_strerror(saved));
        }
        g_ptr_array_unref(args); return okay;
    }
}

int appd_enter_namespace(Instance *instance)
{
    if (!instance->namespace_sandboxed) return appd_trusted_app(instance->app) ? 0 : -1;
    if (setns(4, CLONE_NEWIPC) || setns(5, CLONE_NEWUTS) || setns(3, CLONE_NEWNS)
        || fchdir(6) || chroot(".") || chdir("/")) return -1;
    for (int i = 3; i <= 6; ++i) close(i);
    return 0;
}

void appd_close_namespace(Instance *instance)
{
    if (!instance->namespace_sandboxed) return;
    for (guint i = 0; i < 4; ++i) {
        if (instance->namespace_fds[i] >= 0) close(instance->namespace_fds[i]);
        instance->namespace_fds[i] = -1;
    }
}
