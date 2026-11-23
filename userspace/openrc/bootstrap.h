#ifndef OPENWAVE_OPENRC_BOOTSTRAP_H
#define OPENWAVE_OPENRC_BOOTSTRAP_H

#include <sys/types.h>
#include <sys/stat.h>

struct OpenWaveBootstrapOps {
    pid_t (*get_pid)(void);
    int (*ensure_directory)(const char *path, mode_t mode);
    int (*mount_filesystem)(const char *source, const char *target,
                            const char *type, unsigned long flags,
                            const char *data, long expected_magic);
    int (*attach_console)(void);
    int (*selinux_boot)(void);
    int (*set_environment)(const char *name, const char *value);
    int (*exec_openrc_init)(void);
};

struct OpenWaveConsoleOps {
    int (*open_console)(void);
    int (*duplicate_fd)(int source, int target);
    int (*close_fd)(int fd);
};

/* Returns 0 only when exec_openrc_init returned successfully in a test shim.
 * The production exec does not return on success. */
int openwave_bootstrap_run(const struct OpenWaveBootstrapOps *ops);

/* Exposed for host regression tests; rejects symlinks in every path component. */
int openwave_bootstrap_ensure_directory(const char *path, mode_t mode);
int openwave_bootstrap_attach_console(const struct OpenWaveConsoleOps *ops);
int openwave_bootstrap_filesystem_matches(long actual, long expected);

#endif
