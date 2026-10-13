#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    if (!fork()) { execl("/init.openwave", "/init.openwave", NULL); _exit(127); }
    sleep(12);
    pid_t probe = fork();
    if (!probe) {
        setenv("DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/run/dbus/system_bus_socket", 1);
        execl("/usr/libexec/appd-qa", "/usr/libexec/appd-qa", NULL); _exit(127);
    }
    int status = 0;
    for (;;) {
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid == probe) break;
        usleep(100000);
    }
    printf("APPD_QA_EXIT=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    fflush(stdout); sync(); reboot(RB_POWER_OFF);
    for (;;) pause();
}
