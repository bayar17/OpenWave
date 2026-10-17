#include <cil/cil.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    cil_db_t *db = NULL;
    cil_db_init(&db);
    cil_set_policy_version(db, 35);
    for (int i = 2; i < argc; ++i) {
        FILE *file = fopen(argv[i], "rb");
        if (!file || fseek(file, 0, SEEK_END)) return 1;
        long length = ftell(file);
        if (length < 0 || length > 1048576 || fseek(file, 0, SEEK_SET)) return 1;
        char *data = malloc((size_t)length + 1);
        if (!data || fread(data, 1, length, file) != (size_t)length || fclose(file)) return 1;
        data[length] = 0;
        int result = cil_add_file(db, argv[i], data, length);
        free(data);
        if (result) return 1;
    }
    sepol_policydb_t *policy = NULL;
    void *image = NULL; size_t size = 0;
    if (cil_compile(db) || cil_build_policydb(db, &policy)
        || sepol_policydb_to_image(NULL, policy, &image, &size)) return 1;
    FILE *output = fopen(argv[1], "wb");
    if (!output || fwrite(image, 1, size, output) != size || fclose(output)) return 1;
    free(image); sepol_policydb_free(policy); cil_db_destroy(&db);
    return 0;
}
