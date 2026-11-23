#include <errno.h>
#include <stdio.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    if (!fork()) { execl("/init.openwave", "/init.openwave", (char *)NULL); _exit(127); }
    for (int i = 0; i < 900; ++i) {
        if (!access("/run/openwave/ocean.pid", R_OK)) break;
        usleep(100000);
    }
    sleep(2);
    pid_t probe = fork();
    if (!probe) { execl("/usr/libexec/selinux-qa", "/usr/libexec/selinux-qa", (char *)NULL); _exit(127); }
    int status = 0, okay = 0;
    if (probe > 0) {
        pid_t result;
        do { result = waitpid(probe, &status, 0); } while (result < 0 && errno == EINTR);
        okay = result == probe && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    usleep(500000);
    printf("SELINUX_QA_EXIT=%d\n", okay ? 0 : 1);
    fflush(stdout); sync(); reboot(RB_POWER_OFF);
    for (;;) pause();
}
