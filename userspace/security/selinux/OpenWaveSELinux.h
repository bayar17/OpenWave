#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int openwave_selinux_boot(void);
int openwave_selinux_restore(const char *path);
int openwave_selinux_app_exec(void);
#ifdef __cplusplus
}
#endif
