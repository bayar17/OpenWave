#define _GNU_SOURCE
#include "OpenWaveIdentity.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <selinux/selinux.h>
#include <unistd.h>

#ifndef OPENWAVE_PROC_DIR
#define OPENWAVE_PROC_DIR "/proc"
#endif
#ifndef OPENWAVE_OWNER_UID
#define OPENWAVE_OWNER_UID 0
#endif

guint64 openwave_process_start(guint pid)
{
    char *path = g_strdup_printf(OPENWAVE_PROC_DIR "/%u/stat", pid), *text = NULL;
    g_file_get_contents(path, &text, NULL, NULL); g_free(path);
    if (!text) return 0;
    char *end = strrchr(text, ')'); guint64 ticks = 0;
    if (end && end[1] == ' ') {
        char **fields = g_strsplit(end + 2, " ", -1);
        if (g_strv_length(fields) > 19) ticks = g_ascii_strtoull(fields[19], NULL, 10);
        g_strfreev(fields);
    }
    g_free(text); return ticks;
}

static gboolean supervisor_record_label(int fd)
{
    int enabled = is_selinux_enabled();
    if (enabled == 0) return TRUE; /* Non-SELinux host development. */
    if (enabled < 0) return FALSE;
    char *context = NULL;
    gboolean okay = fgetfilecon_raw(fd, &context) >= 0
        && !strcmp(context, "system_u:object_r:openwave_service_run_t:s0");
    freecon(context);
    return okay;
}

gboolean openwave_supervised_process_uid(guint uid, guint pid, const char *pidfile, guint expected_uid)
{
    if (uid != expected_uid || pid < 2) return FALSE;
    char *identity = g_strconcat(pidfile, ".identity", NULL);
    int fd = open(identity, O_RDONLY | O_CLOEXEC | O_NOFOLLOW); g_free(identity);
    struct stat st; char text[128] = {0}, extra;
    unsigned expected = 0, recorded_uid = 0; unsigned long long ticks = 0;
    gboolean okay = fd >= 0 && !fstat(fd, &st) && S_ISREG(st.st_mode)
        && st.st_uid == OPENWAVE_OWNER_UID && !(st.st_mode & 0022)
        && supervisor_record_label(fd)
        && st.st_size > 0 && st.st_size < (off_t)sizeof(text)
        && read(fd, text, sizeof(text) - 1) == st.st_size
        && sscanf(text, "%u %u %llu %c", &expected, &recorded_uid, &ticks, &extra) == 3
        && expected == pid && recorded_uid == expected_uid
        && ticks && ticks == openwave_process_start(pid);
    if (fd >= 0) close(fd);
    return okay;
}

gboolean openwave_supervised_process(guint uid, guint pid, const char *pidfile)
{
    return openwave_supervised_process_uid(uid, pid, pidfile, 0);
}

char *openwave_service_owner(GDBusConnection *bus, const char *name, const char *pidfile, GError **error)
{
    GVariant *reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetNameOwner", g_variant_new("(s)", name), G_VARIANT_TYPE("(s)"), 0, 1000, NULL, error);
    if (!reply) return NULL;
    char *owner; g_variant_get(reply, "(s)", &owner); g_variant_unref(reply);
    reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetConnectionCredentials", g_variant_new("(s)", owner),
        G_VARIANT_TYPE("(a{sv})"), 0, 1000, NULL, error);
    guint uid = 1, pid = 0; gboolean okay = FALSE;
    if (reply) {
        GVariant *info = g_variant_get_child_value(reply, 0);
        okay = g_variant_lookup(info, "UnixUserID", "u", &uid)
            && g_variant_lookup(info, "ProcessID", "u", &pid)
            && openwave_supervised_process(uid, pid, pidfile);
        g_variant_unref(info); g_variant_unref(reply);
    }
    if (!okay) {
        if (error && !*error) g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "The supervised system service could not be authenticated");
        g_free(owner); return NULL;
    }
    return owner;
}

GVariant *openwave_resolve_caller(GDBusConnection *bus, const char *sender, GError **error)
{
    if (!sender || sender[0] != ':') {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "A unique system bus connection is required"); return NULL;
    }
    char *owner = openwave_service_owner(bus, "org.openwave.Applications", "/run/openwave/appd.pid", error);
    if (!owner) return NULL;
    GVariant *reply = g_dbus_connection_call_sync(bus, owner, "/org/openwave/Applications", "org.openwave.Applications1",
        "ResolveCaller", g_variant_new("(s)", sender), G_VARIANT_TYPE("(a{sv})"), 0, 1500, NULL, error);
    g_free(owner);
    if (!reply) return NULL;
    GVariant *identity = g_variant_get_child_value(reply, 0); g_variant_unref(reply); return identity;
}
