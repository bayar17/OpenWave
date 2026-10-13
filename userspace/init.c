#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void mount_fs(
    const char *source,
    const char *target,
    const char *type,
    unsigned long flags,
    const char *data)
{
    mkdir(target, 0755);

    if (mount(source, target, type, flags, data) != 0 &&
        errno != EBUSY) {
        fprintf(
            stderr,
            "mount %s failed: %m\n",
            target);
    }
}

static void setup_console(void)
{
    int fd = open(
        "/dev/console",
        O_RDWR | O_NOCTTY);

    if (fd < 0)
        return;

    dup2(fd, STDIN_FILENO);
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);

    if (fd > STDERR_FILENO)
        close(fd);
}

static void setup_display_mode(void)
{
    const char *mode = getenv("OPENWAVE_DISPLAY_MODE");
    if (!mode)
        return;

    unsigned int width, height;
    char trailing;
    if (sscanf(mode, "%ux%u%c", &width, &height, &trailing) != 2 ||
        width < 320 || width > 4096 || height < 320 || height > 4096 ||
        width % 8 != 0) {
        fprintf(stderr, "Invalid OPENWAVE_DISPLAY_MODE: %s\n", mode);
        return;
    }

    /* Keep the installed configuration as the fallback. The QEMU helper
     * passes the same dimensions to DRM, the virtual GPU, and this override. */
    const char *path = "/run/openwave/kms.json";
    FILE *config = fopen(path, "w");
    if (!config) {
        perror("create runtime KMS configuration");
        return;
    }
    int written = fprintf(config,
        "{\"device\":\"/dev/dri/card0\",\"hwcursor\":false,"
        "\"outputs\":[{\"name\":\"Virtual1\",\"mode\":\"%ux%u\"}]}\n",
        width, height);
    int closed = fclose(config);
    if (written < 0 || closed != 0) {
        perror("write runtime KMS configuration");
        return;
    }
    printf("OpenWave requested framebuffer: %ux%u\n", width, height);
}

static void setup_qemu_pointer(void)
{
    if (!getenv("OPENWAVE_QEMU_POINTER") ||
        getenv("QT_QPA_EVDEV_MOUSE_PARAMETERS"))
        return;

    /* USB enumeration can finish after PID 1 starts. Resolve the event node
     * by device identity, since event numbers depend on discovery order.
     * Qt's static discovery only recognizes relative mice automatically. */
    for (int attempt = 0; attempt < 30; ++attempt) {
        glob_t devices = {0};
        if (glob("/dev/input/event*", 0, NULL, &devices) == 0) {
            for (size_t i = 0; i < devices.gl_pathc; ++i) {
                int fd = open(devices.gl_pathv[i], O_RDONLY | O_NONBLOCK);
                if (fd < 0)
                    continue;
                char name[256] = {0};
                struct input_absinfo x, y;
                int tablet = ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
                    strstr(name, "QEMU USB Tablet") &&
                    ioctl(fd, EVIOCGABS(ABS_X), &x) >= 0 &&
                    ioctl(fd, EVIOCGABS(ABS_Y), &y) >= 0;
                close(fd);
                if (tablet) {
                    char spec[256];
                    snprintf(spec, sizeof(spec), "%s:abs", devices.gl_pathv[i]);
                    setenv("QT_QPA_EVDEV_MOUSE_PARAMETERS", spec, 0);
                    printf("OpenWave absolute pointer: %s\n", spec);
                    globfree(&devices);
                    return;
                }
            }
        }
        globfree(&devices);
        usleep(100000);
    }
    fprintf(stderr, "QEMU USB tablet not found; using Qt input discovery\n");
}

struct service {
    const char *name;
    const char *program;
    const char *arg1;
    const char *arg2;
    pid_t pid;
    int ocean;
};

static pid_t start_service(struct service *service)
{
    if (strcmp(service->name, "appd") == 0) unlink("/run/openwave/appd/ready");
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork service");
        return -1;
    }
    if (pid == 0) {
        if (service->ocean) {
            setenv("QT_QPA_PLATFORM", "eglfs", 1);
            setenv("QT_QPA_EGLFS_INTEGRATION", "eglfs_kms", 1);
            setenv("QT_QPA_EGLFS_ALWAYS_SET_MODE", "1", 1);
            setenv("QSG_RHI_BACKEND", "opengl", 1);
            setenv("QT_QPA_EGLFS_KMS_CONFIG",
                   access("/run/openwave/kms.json", R_OK) == 0
                       ? "/run/openwave/kms.json" : "/etc/openwave/kms.json", 1);
        }
        char *const args[] = {
            (char *) service->program,
            (char *) service->arg1,
            (char *) service->arg2,
            NULL,
        };
        execv(service->program, args);
        perror(service->program);
        _exit(127);
    }
    service->pid = pid;
    if (service->ocean || strcmp(service->name, "promptd") == 0 || strcmp(service->name, "appd") == 0) {
        const char *path = service->ocean ? "/run/openwave/ocean.pid"
            : strcmp(service->name, "appd") == 0 ? "/run/openwave/appd.pid" : "/run/openwave/promptd.pid";
        char temporary[128];
        snprintf(temporary, sizeof(temporary), "%s.new", path);
        int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd >= 0) {
            dprintf(fd, "%ld\n", (long) pid);
            close(fd);
            if (rename(temporary, path) != 0) perror("OpenWave service PID registration");
        } else perror("OpenWave service PID registration");
    }
    printf("OpenWave started %s pid=%ld\n", service->name, (long) pid);
    return pid;
}

static int wait_for_path(const char *path)
{
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (access(path, F_OK) == 0)
            return 0;
        usleep(100000);
    }
    fprintf(stderr, "OpenWave timed out waiting for %s\n", path);
    return -1;
}

static void trigger_udev(void)
{
    pid_t pid = fork();
    if (pid == 0) {
        execl("/usr/bin/udevadm", "udevadm", "trigger", "--action=add", NULL);
        _exit(127);
    }
    if (pid > 0) {
        int status;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
    }
}

int main(void)
{
    mkdir("/dev", 0755);
    mkdir("/proc", 0555);
    mkdir("/sys", 0555);
    mkdir("/run", 0755);
    mkdir("/tmp", 01777);

    mount_fs(
        "devtmpfs",
        "/dev",
        "devtmpfs",
        MS_NOSUID,
        "mode=0755");

    setup_console();

    mount_fs(
        "proc",
        "/proc",
        "proc",
        MS_NOSUID | MS_NODEV | MS_NOEXEC,
        NULL);

    mount_fs(
        "sysfs",
        "/sys",
        "sysfs",
        MS_NOSUID | MS_NODEV | MS_NOEXEC,
        NULL);

    mount_fs(
        "tmpfs",
        "/run",
        "tmpfs",
        MS_NOSUID | MS_NODEV,
        "mode=0755");

    mount_fs(
        "tmpfs",
        "/tmp",
        "tmpfs",
        MS_NOSUID | MS_NODEV,
        "mode=1777");

    mkdir("/sys/fs/cgroup", 0755);
    mount_fs("cgroup2", "/sys/fs/cgroup", "cgroup2", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);

    printf("OpenWave init\n");

    /* Persist shell and Settings preferences outside the volatile /run tree. */
    setenv("XDG_CONFIG_HOME", "/var/lib/openwave", 1);
    setenv("XDG_STATE_HOME", "/var/lib/openwave/state", 1);
    setenv("DBUS_SYSTEM_BUS_ADDRESS",
           "unix:path=/run/dbus/system_bus_socket", 1);
    setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/run/ocean/bus", 1);


    mkdir("/run/openwave", 0700);
    mkdir("/run/ocean", 0700);
    mkdir("/run/dbus", 0755);
    mkdir("/run/udev", 0755);
    mkdir("/var/lib", 0755);
    mkdir("/var/lib/NetworkManager", 0700);
    mkdir("/var/lib/iwd", 0700);
    mkdir("/var/lib/bluetooth", 0700);
    mkdir("/var/lib/openwave", 0700);
    mkdir("/var/lib/openwave/state", 0700);

    setenv(
        "XDG_RUNTIME_DIR",
        "/run/ocean",
        1);
    
    setup_display_mode();

    /* All services stay in the foreground so PID 1 can reap and restart them.
     * D-Bus is ready before its clients start; Ocean remains the sole DRM owner. */
    struct service services[] = {
        { "udevd", "/usr/sbin/udevd", "--resolve-names=never", NULL, -1, 0 },
        { "system D-Bus", "/usr/bin/dbus-daemon", "--system", "--nofork", -1, 0 },
        { "NetworkManager dispatcher", "/usr/libexec/nm-dispatcher", "--persist", NULL, -1, 0 },
        { "session D-Bus", "/usr/bin/dbus-daemon", "--config-file=/etc/dbus-1/openwave-session-bus.conf", "--nofork", -1, 0 },
        { "iwd", "/usr/libexec/iwd", NULL, NULL, -1, 0 },
        { "bluetoothd", "/usr/libexec/bluetooth/bluetoothd", "-n", NULL, -1, 0 },
        { "NetworkManager", "/usr/sbin/NetworkManager", "--no-daemon", NULL, -1, 0 },
        { "mDNS", "/usr/sbin/avahi-daemon", NULL, NULL, -1, 0 },
        { "connectivityd", "/usr/libexec/connectivityd", NULL, NULL, -1, 0 },
        { "timed", "/usr/libexec/timed", NULL, NULL, -1, 0 },
        { "wallpaperagentd", "/usr/libexec/wallpaperagentd", NULL, NULL, -1, 0 },
        { "openwave-portal", "/usr/libexec/openwave-portal", NULL, NULL, -1, 0 },
        { "promptd", "/usr/libexec/promptd", NULL, NULL, -1, 0 },
        { "PipeWire", "/usr/bin/pipewire", NULL, NULL, -1, 0 },
        { "WirePlumber", "/usr/bin/wireplumber", "-p", "main-systemwide", -1, 0 },
        { "audiod", "/usr/libexec/audiod", NULL, NULL, -1, 0 },
        { "appd", "/usr/libexec/appd", NULL, NULL, -1, 0 },
        { "Ocean", "/usr/bin/ocean-launcher", NULL, NULL, -1, 1 },
    };
    const size_t service_count = sizeof(services) / sizeof(services[0]);
    start_service(&services[0]);
    if (wait_for_path("/run/udev/control") == 0)
        trigger_udev();
    start_service(&services[1]);
    wait_for_path("/run/dbus/system_bus_socket");
    /* D-Bus cannot activate nm-dispatcher in this build. Own its bus name
     * before NetworkManager starts so connection state changes can complete. */
    start_service(&services[2]);
    usleep(500000);
    for (size_t i = 3; i < service_count - 1; ++i) {
        start_service(&services[i]);
        if (strcmp(services[i].name, "session D-Bus") == 0)
            wait_for_path("/run/ocean/bus");
        if (strcmp(services[i].name, "PipeWire") == 0)
            wait_for_path("/run/ocean/pipewire-0");
        if (strcmp(services[i].name, "appd") == 0)
            wait_for_path("/run/openwave/appd/ready");
    }
    setup_qemu_pointer();
    start_service(&services[service_count - 1]);

    for (;;) {
        int status = 0;
        pid_t waited = waitpid(-1, &status, 0);
        if (waited < 0) {
            if (errno == EINTR)
                continue;
            perror("waitpid service");
            sleep(1);
            continue;
        }
        for (size_t i = 0; i < service_count; ++i) {
            if (services[i].pid != waited)
                continue;
            services[i].pid = -1;
            printf("OpenWave stopped %s pid=%ld code=%d\n", services[i].name,
                   (long) waited, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            sleep(1);
            if (i > 1 && i < service_count - 1)
                wait_for_path("/run/dbus/system_bus_socket");
            start_service(&services[i]);
            break;
        }
    }

    return 0;
}
