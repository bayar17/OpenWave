#include <stdio.h>
#include <string.h>

struct security_class_mapping { const char *name; const char *perms[33]; };
#include "classmap.h"
#include "initial_sid_to_string.h"

int main(void)
{
    for (int i = 0; secclass_map[i].name; ++i) {
        printf("(class %s (", secclass_map[i].name);
        for (int j = 0; secclass_map[i].perms[j]; ++j)
            printf("%s ", secclass_map[i].perms[j]);
        puts("))");
        printf("(allow openwave_system_domain openwave_all_types (%s (all)))\n", secclass_map[i].name);
    }
    printf("(classorder (");
    for (int i = 0; secclass_map[i].name; ++i) printf("%s ", secclass_map[i].name);
    puts("))");
    size_t count = sizeof(initial_sid_to_string) / sizeof(*initial_sid_to_string);
    for (size_t i = 1; i < count; ++i) {
        char unused[32]; snprintf(unused, sizeof(unused), "unused%zu", i);
        const char *name = initial_sid_to_string[i] ? initial_sid_to_string[i] : unused;
        const char *type = !strcmp(name, "kernel") || !strcmp(name, "init")
            ? "openwave_system_t" : !strcmp(name, "security") ? "openwave_security_t"
            : !strcmp(name, "devnull") ? "openwave_null_t" : "openwave_default_t";
        printf("(sid %s)\n(sidcontext %s (system_u system_r %s ((s0) (s0))))\n", name, name, type);
    }
    printf("(sidorder (");
    for (size_t i = 1; i < count; ++i) {
        if (initial_sid_to_string[i]) printf("%s ", initial_sid_to_string[i]);
        else printf("unused%zu ", i);
    }
    puts("))");
    return ferror(stdout) ? 1 : 0;
}
