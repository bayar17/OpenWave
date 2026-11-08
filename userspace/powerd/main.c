#define _GNU_SOURCE
#include <OpenWave/OpenWaveIdentity.h>
#include <OpenWave/OpenWaveXml.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef POWER_FIFO_PATH
#define POWER_FIFO_PATH "/run/openrc/init.ctl"
#endif
#ifndef POWER_OWNER_UID
#define POWER_OWNER_UID 0
#endif
#define BUS "org.openwave.Power"
#define IFACE "org.openwave.Power1"
#define PATH "/org/openwave/Power"

static gboolean pending;

/* Use OpenRC's command channel, so PID 1 stops services and unmounts filesystems.
 * No command execution, shell arguments, reboot syscall or signal privileges. */
static int open_init_fifo(void)
{
    int fd = open(POWER_FIFO_PATH, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISFIFO(st.st_mode) || st.st_uid != POWER_OWNER_UID
        || (st.st_mode & 0077) || st.st_nlink != 1) {
        close(fd); errno = EPERM; return -1;
    }
    return fd;
}

static gboolean trusted_client(GDBusConnection *bus, const char *sender, guint *pid)
{
    GVariant *reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionCredentials",
        g_variant_new("(s)", sender), G_VARIANT_TYPE("(a{sv})"), 0, 2000, NULL, NULL);
    if (!reply) return FALSE;
    GVariant *fields = g_variant_get_child_value(reply, 0);
    guint uid = 0;
    struct passwd *ocean = getpwnam("ocean");
    gboolean okay = ocean && g_variant_lookup(fields, "UnixUserID", "u", &uid)
        && g_variant_lookup(fields, "ProcessID", "u", pid)
        && openwave_supervised_process_uid(uid, *pid, "/run/openwave/ocean.pid", ocean->pw_uid);
    if (!okay && ocean && uid == ocean->pw_uid) {
        GVariant *identity = openwave_resolve_caller(bus, sender, NULL);
        const char *id = NULL;
        guint leader = 0, user = 0;
        gboolean administrator = FALSE;
        okay = identity && g_variant_lookup(identity, "appId", "&s", &id)
            && !strcmp(id, "com.openwave.settings")
            && g_variant_lookup(identity, "leaderPid", "u", &leader) && leader == *pid
            && g_variant_lookup(identity, "userId", "u", &user) && user == ocean->pw_uid
            && g_variant_lookup(identity, "administrator", "b", &administrator) && administrator;
        if (identity) g_variant_unref(identity);
    }
    g_variant_unref(fields); g_variant_unref(reply);
    return okay;
}

static void method(GDBusConnection *bus, const char *sender, const char *path,
    const char *interface, const char *name, GVariant *args,
    GDBusMethodInvocation *call, void *data)
{
    (void)path; (void)interface; (void)args; (void)data;
    guint pid = 0;
    if (!trusted_client(bus, sender, &pid)) {
        g_dbus_method_invocation_return_dbus_error(call, BUS ".Unauthorized",
            "Only the supervised Ocean shell or managed Settings leader may request system power actions."); return;
    }
    const char *command = !strcmp(name, "PowerOff") ? "poweroff"
        : !strcmp(name, "Reboot") ? "reboot" : NULL;
    if (!command || pending) {
        g_dbus_method_invocation_return_dbus_error(call, BUS ".Unavailable",
            "A power action is already pending or unsupported."); return;
    }
    int fd = open_init_fifo();
    if (fd < 0) {
        g_dbus_method_invocation_return_dbus_error(call, BUS ".Unavailable",
            "OpenRC is not ready to receive a power action. Please try again."); return;
    }
    g_print("POWER_ACTION submitting action=%s ocean_pid=%u\n", command, pid);
    fflush(stdout);
    /* One atomic FIFO write, also below PIPE_BUF. PID 1 accepts exact words. */
    ssize_t count;
    do { count = write(fd, command, strlen(command)); } while (count < 0 && errno == EINTR);
    close(fd);
    if (count != (ssize_t)strlen(command)) {
        g_dbus_method_invocation_return_dbus_error(call, BUS ".Unavailable",
            "OpenRC did not accept the power request."); return;
    }
    pending = TRUE;
    g_print("POWER_ACTION accepted action=%s ocean_pid=%u\n", command, pid);
    fflush(stdout);
    g_dbus_method_invocation_return_value(call, NULL);
    g_dbus_connection_flush_sync(bus, NULL, NULL);
}

static gboolean stop(void *data) { g_main_loop_quit(data); return G_SOURCE_REMOVE; }

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    GDBusNodeInfo *node = bus ? openwave_xml_load_dbus_interface(IFACE, &error) : NULL;
    static const GDBusInterfaceVTable table = {method, NULL, NULL, {NULL}};
    if (!node || !g_dbus_connection_register_object(bus, PATH, node->interfaces[0], &table, NULL, NULL, &error)) goto failed;
    g_dbus_node_info_unref(node);
    GVariant *reply = g_dbus_connection_call_sync(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName", g_variant_new("(su)", BUS, 4u),
        G_VARIANT_TYPE("(u)"), 0, 2000, NULL, &error);
    guint result = 0;
    if (reply) { g_variant_get(reply, "(u)", &result); g_variant_unref(reply); }
    if (result != 1) goto failed;
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_unix_signal_add(SIGTERM, stop, loop);
    g_unix_signal_add(SIGINT, stop, loop);
    g_print("OpenWave power service ready\n");
    g_main_loop_run(loop);
    g_main_loop_unref(loop); g_object_unref(bus);
    return 0;
failed:
    g_printerr("powerd: %s\n", error ? error->message : "service name is already owned");
    g_clear_error(&error); return 1;
}
