#define main openwave_registrar_main
#include "../register-service-identity.c"
#undef main

static const struct service_identity *find_service(const char *name)
{
    for (size_t i = 0; i < sizeof(services) / sizeof(services[0]); ++i)
        if (!strcmp(name, services[i].name)) return &services[i];
    return NULL;
}

static int marker_matches(const struct service_identity *service, pid_t pid)
{
    int dirfd = open(OPENWAVE_RUNTIME_DIR, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dirfd < 0) return 0;
    const char *base = strrchr(service->pidfile, '/') + 1;
    char marker[NAME_MAX + 1];
    snprintf(marker, sizeof(marker), "%s.identity", base);
    int fd = openat(dirfd, marker, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    close(dirfd);
    struct stat st;
    char text[96] = {0};
    unsigned recorded_pid = 0, recorded_uid = UINT_MAX;
    unsigned long long ticks = 0;
    int okay = fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode)
        && st.st_uid == OPENWAVE_OWNER_UID && !(st.st_mode & 0022)
        && read(fd, text, sizeof(text) - 1) > 0
        && sscanf(text, "%u %u %llu", &recorded_pid, &recorded_uid, &ticks) == 3
        && recorded_pid == (unsigned)pid && recorded_uid == service->process_uid
        && ticks && ticks == process_start(pid)
        && process_is_expected(pid, service->executable, service->process_uid);
    if (fd >= 0) close(fd);
    return okay;
}

int main(int argc, char **argv)
{
    const char *service_name = argc == 4 ? argv[2] : "appd";
    if (argc != 3 && argc != 4) return 2;
    const struct service_identity *service = find_service(service_name);
    if (!service) return 2;
    pid_t pid = (pid_t)strtol(argv[argc - 1], NULL, 10);
    if (!strcmp(argv[1], "register")) {
        unsigned long long ticks = process_start(pid);
        if (!ticks || !process_is_expected(pid, service->executable, service->process_uid)) return 10;
        return write_identity(service, pid, ticks) ? 11 : 0;
    }
    if (!strcmp(argv[1], "invalidate")) return invalidate_identity(service) ? 12 : 0;
    if (!strcmp(argv[1], "check")) return marker_matches(service, pid) ? 0 : 13;
    return 2;
}
