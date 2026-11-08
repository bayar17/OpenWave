#define _GNU_SOURCE
#include <sepol/handle.h>
#include <sepol/policydb.h>
#include <sepol/policydb/avtab.h>
#include <sepol/policydb/policydb.h>
#include <sepol/policydb/symtab.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void check(int okay, const char *label)
{
    printf("QA_POLICY %s %s\n", okay ? "PASS" : "FAIL", label);
    if (!okay) ++failures;
}

static symtab_datum_t *symbol(symtab_t *table, const char *name)
{
    return (symtab_datum_t *)hashtab_search(table->table, name);
}

static uint32_t permissions(policydb_t *policy, const char *source,
                            const char *target, const char *class_name)
{
    symtab_datum_t *s = symbol(&policy->p_types, source);
    symtab_datum_t *t = symbol(&policy->p_types, target);
    class_datum_t *c = (class_datum_t *)symbol(&policy->p_classes, class_name);
    if (!s || !t || !c) return 0;
    avtab_key_t key = {
        .source_type = (uint16_t)s->value,
        .target_type = (uint16_t)t->value,
        .target_class = (uint16_t)c->s.value,
        .specified = AVTAB_ALLOWED,
    };
    uint32_t result = 0;
    /* Binary policies may retain attributes. Include every applicable source
     * and target attribute, not just concrete-type entries in the avtab. */
    for (uint32_t si = 0; si < policy->p_types.nprim; ++si) {
        if (si != s->value - 1 &&
            !ebitmap_get_bit(&policy->type_attr_map[s->value - 1], si)) continue;
        for (uint32_t ti = 0; ti < policy->p_types.nprim; ++ti) {
            if (ti != t->value - 1 &&
                !ebitmap_get_bit(&policy->type_attr_map[t->value - 1], ti)) continue;
            key.source_type = (uint16_t)(si + 1);
            key.target_type = (uint16_t)(ti + 1);
            avtab_datum_t *rule = avtab_search(&policy->te_avtab, &key);
            if (rule) result |= rule->data;
        }
    }
    return result;
}

static uint32_t permission_bit(policydb_t *policy, const char *class_name,
                               const char *permission)
{
    class_datum_t *c = (class_datum_t *)symbol(&policy->p_classes, class_name);
    if (!c) return 0;
    symtab_datum_t *p = symbol(&c->permissions, permission);
    if (!p && c->comdatum) p = symbol(&c->comdatum->permissions, permission);
    return p && p->value > 0 && p->value <= 32 ? 1U << (p->value - 1) : 0;
}

static int has(policydb_t *policy, const char *source, const char *target,
               const char *class_name, const char *permission)
{
    uint32_t bit = permission_bit(policy, class_name, permission);
    return bit && (permissions(policy, source, target, class_name) & bit);
}

static int has_any(policydb_t *policy, const char *source, const char *target,
                   const char *class_name, const char *const *permissions_list,
                   size_t count)
{
    for (size_t i = 0; i < count; ++i)
        if (has(policy, source, target, class_name, permissions_list[i])) return 1;
    return 0;
}

static int type_transition(policydb_t *policy, const char *source,
                           const char *entrypoint, const char *domain)
{
    symtab_datum_t *s = symbol(&policy->p_types, source);
    symtab_datum_t *t = symbol(&policy->p_types, entrypoint);
    symtab_datum_t *expected = symbol(&policy->p_types, domain);
    symtab_datum_t *process = symbol(&policy->p_classes, "process");
    if (!s || !t || !expected || !process) return 0;
    avtab_key_t key = { .source_type = (uint16_t)s->value,
        .target_type = (uint16_t)t->value, .target_class = (uint16_t)process->value,
        .specified = AVTAB_TRANSITION };
    avtab_datum_t *rule = avtab_search(&policy->te_avtab, &key);
    return rule && rule->data == expected->value;
}

static int named_directory_transition(policydb_t *policy, const char *source,
                                      const char *parent, const char *name,
                                      const char *result)
{
    symtab_datum_t *s = symbol(&policy->p_types, source);
    symtab_datum_t *p = symbol(&policy->p_types, parent);
    symtab_datum_t *r = symbol(&policy->p_types, result);
    class_datum_t *dir = (class_datum_t *)symbol(&policy->p_classes, "dir");
    if (!s || !p || !r || !dir || !s->value) return 0;
    filename_trans_key_t key = {
        .ttype = (uint32_t)p->value,
        .tclass = (uint32_t)dir->s.value,
        .name = (char *)name,
    };
    filename_trans_datum_t *transition =
        (filename_trans_datum_t *)hashtab_search(policy->filename_trans,
                                                  (const char *)&key);
    return transition && transition->otype == r->value &&
        ebitmap_get_bit(&transition->stypes, s->value - 1);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s compiled-policy\n", argv[0]);
        return 2;
    }
    FILE *input = fopen(argv[1], "rb");
    if (!input) { perror("open policy"); return 2; }
    if (fseek(input, 0, SEEK_END) || ftell(input) <= 0) {
        perror("measure policy"); fclose(input); return 2;
    }
    long length = ftell(input);
    rewind(input);
    void *image = malloc((size_t)length);
    if (!image || fread(image, 1, (size_t)length, input) != (size_t)length) {
        perror("read policy"); free(image); fclose(input); return 2;
    }
    fclose(input);
    sepol_handle_t *handle = sepol_handle_create();
    sepol_policydb_t *wrapped = NULL;
    if (!handle || sepol_policydb_create(&wrapped) ||
        sepol_policydb_from_image(handle, image, (size_t)length, wrapped)) {
        fprintf(stderr, "cannot parse compiled SELinux policy\n");
        free(image); sepol_policydb_free(wrapped); sepol_handle_destroy(handle);
        return 2;
    }
    policydb_t *policy = &wrapped->p;
    const char *executables[] = {"openwave_elf_loader_t", "openwave_openrc_exec_t"};
    const char *exec_permissions[] = {"read", "open", "getattr", "map", "execute", "execute_no_trans"};
    int boot_exec = 1;
    for (size_t i = 0; i < 2; ++i)
        for (size_t j = 0; j < 6; ++j)
            boot_exec &= has(policy, "openwave_system_t", executables[i], "file", exec_permissions[j]);
    check(boot_exec, "PID 1 can execute OpenRC and the separately labeled ELF interpreter");

    /* The dynamic loader and mapped libraries must be available in each
     * actual process domain. The labels do not authorize filesystem mutation. */
    const char *dynamic_domains[] = {
        "openwave_system_t", "openwave_app_t", "openwave_appd_t",
        "openwave_shell_t", "openwave_dbus_t",
        "openwave_waydroid_manager_t", "openwave_waydroid_launcher_t",
        "openwave_waydroid_helper_t", "openwave_waydroid_net_t",
        "openwave_lxc_t",
    };
    const char *loader_perms[] = {
        "read", "open", "getattr", "map", "execute", "execute_no_trans",
    };
    const char *library_perms[] = {
        "read", "open", "getattr", "map", "execute",
    };
    const char *mutation_perms[] = {
        "write", "append", "create", "setattr", "unlink", "rename",
        "relabelfrom", "relabelto",
    };
    int linker_access = 1;
    for (size_t i = 0; i < sizeof(dynamic_domains) / sizeof(dynamic_domains[0]); ++i) {
        for (size_t j = 0; j < sizeof(loader_perms) / sizeof(loader_perms[0]); ++j)
            linker_access &= has(policy, dynamic_domains[i], "openwave_elf_loader_t",
                                 "file", loader_perms[j]);
        for (size_t j = 0; j < sizeof(library_perms) / sizeof(library_perms[0]); ++j)
            linker_access &= has(policy, dynamic_domains[i], "openwave_shared_lib_t",
                                 "file", library_perms[j]);
        if (strcmp(dynamic_domains[i], "openwave_system_t") != 0) {
            linker_access &= !has_any(policy, dynamic_domains[i], "openwave_elf_loader_t",
                                      "file", mutation_perms,
                                      sizeof(mutation_perms) / sizeof(mutation_perms[0]));
            linker_access &= !has_any(policy, dynamic_domains[i], "openwave_shared_lib_t",
                                      "file", mutation_perms,
                                      sizeof(mutation_perms) / sizeof(mutation_perms[0]));
        }
    }
    check(linker_access, "Native, LXC and Waydroid domains map ELF objects without write authority");
    const char *producer_domains[] = {
        "openwave_appd_t", "openwave_wallpaper_t", "openwave_shell_t" };
    const char *log_permissions[] = {"write", "append"};
    int producers = 1;
    for (size_t i = 0; i < sizeof(producer_domains) / sizeof(producer_domains[0]); ++i) {
        const char *domain = producer_domains[i];
        producers &= has(policy, domain, "openwave_qa_evidence_t", "file", "write");
        producers &= has(policy, domain, "openwave_qa_evidence_t", "file", "append");
        producers &= has(policy, domain, "openwave_system_t", "fd", "use");
        producers &= !has_any(policy, domain, "openwave_private_t", "file",
                              log_permissions, 2);
        producers &= !has_any(policy, domain, "openwave_runtime_t", "file",
                              log_permissions, 2);
    }
    check(producers, "appd wallpaperagentd and Ocean write only to dedicated QA evidence type");

    const char *manage_permissions[] = {"create", "write", "append", "unlink", "rename"};
    int manager = has(policy, "openwave_system_t", "openwave_qa_evidence_t", "dir", "add_name") &&
        has(policy, "openwave_system_t", "openwave_qa_evidence_t", "file", "create") &&
        has(policy, "openwave_system_t", "openwave_qa_evidence_t", "file", "read") &&
        has(policy, "openwave_system_t", "openwave_private_t", "dir", "search") &&
        !has_any(policy, "openwave_system_t", "openwave_private_t", "file", manage_permissions, 5) &&
        !has_any(policy, "openwave_system_t", "openwave_runtime_t", "file", manage_permissions, 5);
    check(manager, "system supervisor manages QA evidence without private or runtime file writes");

    int ocean_log_transition = named_directory_transition(policy, "openwave_system_t",
        "openwave_run_t", "openwave-logs", "openwave_qa_evidence_t") &&
        !has(policy, "openwave_system_t", "openwave_runtime_t", "dir", "relabelfrom") &&
        !has(policy, "openwave_system_t", "openwave_qa_evidence_t", "dir", "relabelto");
    check(ocean_log_transition, "OpenRC creates only the named Ocean log directory with the evidence type and has no general directory relabel access");

    int transitions = type_transition(policy, "openwave_system_t", "openwave_appd_exec_t", "openwave_appd_t") &&
        type_transition(policy, "openwave_system_t", "openwave_wallpaper_exec_t", "openwave_wallpaper_t") &&
        type_transition(policy, "openwave_system_t", "openwave_shell_exec_t", "openwave_shell_t");
    check(transitions, "appd wallpaperagentd and Ocean retain dedicated process transitions");

    free(image);
    sepol_policydb_free(wrapped);
    sepol_handle_destroy(handle);
    printf("QA_POLICY_RESULT failures=%d\n", failures);
    return failures ? 1 : 0;
}
