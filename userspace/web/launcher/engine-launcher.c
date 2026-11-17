#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int bundle_root(const char *root)
{
    static const char *const prefixes[] = {"/System/Applications/", "/Applications/",
#ifdef OPENWAVE_ENGINE_LAUNCHER_TEST_PREFIX
        OPENWAVE_ENGINE_LAUNCHER_TEST_PREFIX,
#endif
    };
    if (!root || strlen(root) >= PATH_MAX - 64) return 0;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(*prefixes); ++i) {
        size_t length = strlen(prefixes[i]);
        if (strncmp(root, prefixes[i], length) == 0 && root[length] && !strchr(root + length, '/')
            && strcmp(root + length, "..") != 0 && strcmp(root + length, ".") != 0)
            return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const char *const helpers[] = {"WPEWebProcess", "WPENetworkProcess", "WPEGPUProcess"};
    const char *name = argc > 0 && argv[0] ? strrchr(argv[0], '/') : NULL;
    name = name ? name + 1 : (argc > 0 ? argv[0] : "");
    int known = 0;
    for (size_t i = 0; i < sizeof(helpers) / sizeof(*helpers); ++i) known |= strcmp(name, helpers[i]) == 0;
    const char *root = getenv("OPENWAVE_WEB_ENGINE_ROOT");
    if (!known || !bundle_root(root)) {
        fprintf(stderr, "engine-launcher: %s is not a web engine helper, or no engine bundle is configured\n", name);
        return 127;
    }
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/sbin/%s", root, name) >= (int)sizeof(path)) return 127;
    execv(path, argv);
    fprintf(stderr, "engine-launcher: cannot start %s: %s\n", path, strerror(errno));
    return 127;
}
