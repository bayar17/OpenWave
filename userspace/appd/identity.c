#define _GNU_SOURCE
#include "appd.h"
#include <OpenWave/OpenWaveIdentity.h>
#include <stdio.h>
#include <string.h>

gboolean appd_trusted_app(App *app)
{
    return (!strcmp(app->id, "com.openwave.settings") && !strcmp(app->argv[0], "/System/Applications/com.openwave.settings/bin/ocean-settings"))
        || (!strcmp(app->id, "com.openwave.files") && !strcmp(app->argv[0], "/System/Applications/com.openwave.files/bin/ocean-files"));
}

Instance *appd_caller(Manager *m, guint uid, guint pid)
{
    if (pid < 2) return NULL;
    char *path = g_strdup_printf("/proc/%u/cgroup", pid), *contents = NULL;
    g_file_get_contents(path, &contents, NULL, NULL); g_free(path);
    Instance *found = NULL;
    for (guint i = 0; i < m->instances->len; ++i) {
        Instance *instance = g_ptr_array_index(m->instances, i);
        if (instance->stopping || instance->uid != uid || !instance->start_time
            || openwave_process_start(instance->pid) != instance->start_time) continue;
        if (pid == (guint)instance->pid) { found = instance; break; }
        if (contents && instance->cgroup) {
            const char *relative = instance->cgroup + strlen("/sys/fs/cgroup");
            char *line = g_strdup_printf("0::%s\n", relative);
            if (!strcmp(contents, line)) found = instance;
            g_free(line);
            if (found) break;
        }
    }
    g_free(contents); return found;
}

GVariant *appd_identity(Manager *m, Instance *instance, guint pid)
{
    GVariantBuilder fields; g_variant_builder_init(&fields, G_VARIANT_TYPE("a{sv}"));
#define STR(k,v) g_variant_builder_add(&fields, "{sv}", k, g_variant_new_string(v))
#define UINT(k,v) g_variant_builder_add(&fields, "{sv}", k, g_variant_new_uint32(v))
    STR("appId", instance->app->id); STR("name", instance->app->name);
    STR("manifestDigest", instance->app->digest);
    STR("userName", m->user); STR("userRoot", m->home); STR("session", m->session);
    STR("instance", instance->cgroup ? instance->cgroup : "");
    UINT("userId", m->uid); UINT("processUserId", instance->uid);
    UINT("pid", pid); UINT("leaderPid", instance->pid);
    g_variant_builder_add(&fields, "{sv}", "startTime", g_variant_new_uint64(openwave_process_start(pid)));
    g_variant_builder_add(&fields, "{sv}", "trusted", g_variant_new_boolean(appd_trusted_app(instance->app)));
    g_variant_builder_add(&fields, "{sv}", "administrator", g_variant_new_boolean(
        !strcmp(instance->app->id, "com.openwave.settings") && appd_trusted_app(instance->app) && pid == (guint)instance->pid));
#undef STR
#undef UINT
    return g_variant_builder_end(&fields);
}
