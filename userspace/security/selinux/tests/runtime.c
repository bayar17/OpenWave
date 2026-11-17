#define _GNU_SOURCE
#include <selinux/selinux.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures, checks;
static void check(int okay, const char *name)
{
    ++checks;
    if (!okay) ++failures;
    printf("SELINUX_QA %s %s\n", okay ? "PASS" : "FAIL", name);
    fflush(stdout);
}
static int context_is(const char *path, const char *type)
{
    char *context = NULL;
    int okay = path ? getfilecon(path, &context) >= 0 : getcon(&context) == 0;
    okay = okay && strstr(context, type);
    freecon(context); return okay;
}
static int confined(void)
{
    check(context_is(NULL, ":openwave_app_t:"), "application domain transition");
    check(geteuid() == 0, "probe retains root UID to isolate MAC from DAC");
    int fd = open("/tmp/selinux-qa/private", O_RDONLY);
    check(fd < 0 && errno == EACCES, "deny reading world-readable protected data");
    if (fd >= 0) close(fd);
    fd = open("/tmp/selinux-qa/runtime", O_WRONLY);
    check(fd < 0 && errno == EACCES, "deny writing world-writable runtime file");
    if (fd >= 0) close(fd);
    check(security_setenforce(0) < 0, "deny disabling enforcement");
    check(setexeccon("system_u:system_r:openwave_system_t:s0") < 0, "deny selecting privileged exec domain");
    check(setcon("system_u:system_r:openwave_system_t:s0") < 0, "deny changing current domain");
    fd = open("/tmp/selinux-qa/data/created", O_CREAT | O_RDWR, 0600);
    check(fd >= 0, "allow creating private application data");
    if (fd >= 0) {
        check(write(fd, "ok", 2) == 2, "allow writing application data");
        check(lseek(fd, 0, SEEK_SET) == 0, "allow seeking application data");
        char bytes[2]; check(read(fd, bytes, 2) == 2 && !memcmp(bytes, "ok", 2), "allow reading application data");
        close(fd);
    }
    check(setfilecon("/tmp/selinux-qa/data/created", "system_u:object_r:openwave_runtime_t:s0") < 0,
          "deny relabeling own data to runtime type");
    check(mount("tmpfs", "/tmp/selinux-qa/data", "tmpfs", 0, NULL) < 0 && errno == EPERM,
          "deny mounting filesystems");
    check(access("/System/Services/appd", X_OK) < 0 && errno == EACCES, "deny executing privileged service");
    check(unlink("/tmp/selinux-qa/data/created") == 0, "allow deleting own data");
    printf("SELINUX_CONFINED_RESULT checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
static int service_type(const char *pidfile, const char *type)
{
    FILE *file = fopen(pidfile, "r");
    long pid = 0;
    if (!file) return 0;
    int count = fscanf(file, "%ld", &pid); fclose(file);
    if (count != 1 || pid < 2) return 0;
    char path[128], context[256];
    snprintf(path, sizeof(path), "/proc/%ld/attr/current", pid);
    file = fopen(path, "r");
    if (!file) return 0;
    char *read = fgets(context, sizeof(context), file); fclose(file);
    return read && strstr(context, type);
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--confined")) return confined();
    check(is_selinux_enabled() == 1, "SELinux enabled");
    check(security_getenforce() == 1, "enforcing boot");
    check(service_type("/run/openwave/appd.pid", ":openwave_appd_t:"), "appd domain transition");
    check(service_type("/run/openwave/promptd.pid", ":openwave_promptd_t:"), "promptd domain transition");
    check(service_type("/run/openwave/permissiond.pid", ":openwave_permissiond_t:"), "permissiond domain transition");
    check(service_type("/run/openwave/ocean.pid", ":openwave_shell_t:"), "compositor domain transition");
    check(context_is("/System/Services/appd", ":openwave_appd_exec_t:"), "appd entrypoint labeled");
    check(context_is("/etc/selinux/openwave/policy/policy.35", ":openwave_private_t:"), "policy protected");
    check(context_is("/System/Applications/com.openwave.settings/bin/ocean-settings", ":openwave_settings_exec_t:"), "Settings entrypoint labeled");
    mkdir("/tmp/selinux-qa", 0755);
    mkdir("/tmp/selinux-qa/data", 0777); chmod("/tmp/selinux-qa/data", 0777);
    check(setfilecon("/tmp/selinux-qa/data", "system_u:object_r:openwave_app_data_t:s0") == 0, "prepare private data label");
    for (int i = 0; i < 2; ++i) {
        const char *path = i ? "/tmp/selinux-qa/runtime" : "/tmp/selinux-qa/private";
        int fd = open(path, O_CREAT | O_WRONLY, 0666);
        check(fd >= 0, "prepare DAC-readable/writable fixture");
        if (fd >= 0) { fchmod(fd, 0666); close(fd); }
        check(setfilecon(path, i ? "system_u:object_r:openwave_runtime_t:s0" : "system_u:object_r:openwave_private_t:s0") == 0,
              "label fixture");
    }
    check(setfilecon(argv[0], "system_u:object_r:openwave_app_exec_t:s0") == 0, "prepare app probe entrypoint");
    pid_t child = fork();
    if (!child) {
        int console = open("/dev/console", O_WRONLY);
        if (console < 0) _exit(125);
        dup2(console, STDOUT_FILENO); dup2(console, STDERR_FILENO); close(console);
        if (setexeccon("system_u:system_r:openwave_app_t:s0") || prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) _exit(126);
        execl(argv[0], argv[0], "--confined", (char *)NULL); _exit(127);
    }
    int status = 0;
    if (child > 0) while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    check(child > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0, "confined child passes all checks");
    check(security_getenforce() == 1, "enforcement remains active");
    printf("SELINUX_QA_RESULT checks=%d failures=%d\n", checks, failures);
    return failures ? 1 : 0;
}
