#ifndef OPENWAVE_QA_DEBUG_CMDLINE_H
#define OPENWAVE_QA_DEBUG_CMDLINE_H

/* Debug-only exact kernel-token gate. It has no effect on SELinux itself. */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static inline bool openwave_permissive_qa_cmdline(const char *cmdline)
{
    if (!cmdline)
        return false;
    bool explicit_debug = false;
    bool opt_in = false;
    bool permissive = false;
    bool verbose = false;
    const char *cursor = cmdline;
    while (*cursor) {
        while (*cursor == ' ' || *cursor == '\t' ||
               *cursor == '\r' || *cursor == '\n')
            ++cursor;
        const char *start = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t' &&
               *cursor != '\r' && *cursor != '\n')
            ++cursor;
        size_t length = (size_t)(cursor - start);
#define TOKEN_IS(text) (length == sizeof(text) - 1 &&                        memcmp(start, text, sizeof(text) - 1) == 0)
        if (TOKEN_IS("openwave.qa=selinux-debug"))
            explicit_debug = true;
        else if (TOKEN_IS("openwave.qa=mount-preflight"))
            opt_in = true;
        else if (TOKEN_IS("openwave.selinux=permissive"))
            permissive = true;
        else if (TOKEN_IS("openwave.audit=verbose"))
            verbose = true;
#undef TOKEN_IS
    }
    return explicit_debug && opt_in && permissive && verbose;
}
#endif
