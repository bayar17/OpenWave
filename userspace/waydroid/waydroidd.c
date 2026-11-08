/* OpenRC shim; the pinned upstream manager owns all Android lifecycle work. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1 || getuid() || geteuid() || getgid() || getegid()) {
        fputs("waydroidd: expected root OpenRC service identity and no arguments\n", stderr);
        return 126;
    }
    if (clearenv()
        || setenv("PATH", "/usr/libexec/openwave-waydroid:/usr/bin:/usr/sbin:/bin:/sbin", 1)
        || setenv("HOME", "/var/lib/waydroid", 1)
        || setenv("TMPDIR", "/run/openwave/waydroid", 1)
        || setenv("LANG", "C.UTF-8", 1)
        || setenv("PYTHONNOUSERSITE", "1", 1)
        || setenv("PYTHONDONTWRITEBYTECODE", "1", 1)
        || setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/run/dbus/system_bus_socket", 1)) {
        perror("waydroidd: fixed manager environment");
        return 126;
    }
    if (close_range(3, ~0U, 0)) {
        perror("waydroidd: close inherited descriptors");
        return 126;
    }
    char *const command[] = {"/usr/bin/python3.14", "/usr/lib/waydroid/waydroid.py",
                            "container", "start", NULL};
    execv(command[0], command);
    perror("waydroidd: exec upstream manager");
    return 127;
}
