#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *data = getenv("XDG_DATA_HOME");
    char path[4096];
    FILE *file;
    (void)argc;
    printf("appdeploy-test-app %s uid=%u\n", argv[0], (unsigned)getuid());
    if (!data || snprintf(path, sizeof(path), "%s/launched", data) >= (int)sizeof(path)) return 2;
    file = fopen(path, "w");
    if (!file) return 3;
    fprintf(file, "uid=%u pid=%u\n", (unsigned)getuid(), (unsigned)getpid());
    fclose(file);
    sleep(2);
    return 0;
}
