#include <stdio.h>
#include <stdlib.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
static pid_t supervisor;
static void stop_service(const char *path)
{
    FILE *file = fopen(path, "r"); int pid = 0;
    if (file) { fscanf(file, "%d", &pid); fclose(file); }
    if (pid > 1) kill(pid, SIGTERM);
}
static void shutdown_test(void)
{
    kill(supervisor, SIGSTOP);
    stop_service("/run/openwave/appd.pid"); sleep(4);
    stop_service("/run/openwave/ocean.pid"); sleep(1);
    fflush(stdout); sync(); reboot(RB_POWER_OFF);
}
int main(void)
{
    supervisor = fork();
    if (!supervisor) { execl("/init.openwave", "/init.openwave", NULL); _exit(127); }
    for (int second = 0; second < 240; ++second) {
        FILE *file = fopen("/tmp/permission-qa-result.json", "r");
        if (file) {
            char result[2048] = {0}; fread(result, 1, sizeof(result) - 1, file); fclose(file);
            printf("PERMISSION_QA_REPORT %s\n", result); shutdown_test();
        }
        while (waitpid(-1, NULL, WNOHANG) > 0) {}
        sleep(1);
    }
    printf("PERMISSION_QA_TIMEOUT\n"); shutdown_test();
    for (;;) pause();
}
