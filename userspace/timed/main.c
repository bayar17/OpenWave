#include <OpenWave/OpenWaveXml.h>
#include <gio/gio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BUS_NAME "org.openwave.Time"
#define OBJECT_PATH "/org/openwave/Time"
#define INTERFACE "org.openwave.Time1"

typedef struct {
    GDBusConnection *bus;
    GMainLoop *loop;
    GPtrArray *zones;
    char *timezone;
    char *root;
} Service;

static char *root_path(Service *service, const char *path)
{
    return g_build_filename(service->root, path, NULL);
}

static gint compare_names(gconstpointer left, gconstpointer right)
{
    return g_strcmp0(*(char *const *) left, *(char *const *) right);
}

static gboolean zone_allowed(Service *service, const char *name)
{
    if (!name || !*name || strchr(name, '\\') || name[0] == '/' ||
        strstr(name, "..") || strchr(name, '\n'))
        return FALSE;
    for (guint i = 0; i < service->zones->len; ++i)
        if (g_strcmp0(name, g_ptr_array_index(service->zones, i)) == 0)
            return TRUE;
    return FALSE;
}

static gboolean load_zones(Service *service)
{
    char *path = root_path(service, "usr/share/zoneinfo/zone.tab");
    char *contents = NULL;
    if (!g_file_get_contents(path, &contents, NULL, NULL)) {
        g_printerr("timed: cannot read %s\n", path);
        g_free(path);
        return FALSE;
    }
    g_free(path);
    service->zones = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(service->zones, g_strdup("UTC"));
    char **lines = g_strsplit(contents, "\n", -1);
    for (char **line = lines; *line; ++line) {
        if (!**line || **line == '#')
            continue;
        char **fields = g_strsplit(*line, "\t", 4);
        if (fields[2] && *fields[2]) {
            path = g_build_filename(service->root, "usr/share/zoneinfo", fields[2], NULL);
            if (g_file_test(path, G_FILE_TEST_IS_REGULAR))
                g_ptr_array_add(service->zones, g_strdup(fields[2]));
            g_free(path);
        }
        g_strfreev(fields);
    }
    g_strfreev(lines);
    g_free(contents);
    g_ptr_array_sort(service->zones, compare_names);
    return TRUE;
}

static char *read_timezone(Service *service)
{
    char *path = root_path(service, "etc/localtime");
    char *link = g_file_read_link(path, NULL);
    g_free(path);
    const char *prefix = "../usr/share/zoneinfo/";
    if (link && g_str_has_prefix(link, prefix) && zone_allowed(service, link + strlen(prefix))) {
        char *result = g_strdup(link + strlen(prefix));
        g_free(link);
        return result;
    }
    g_free(link);
    path = root_path(service, "etc/timezone");
    char *contents = NULL;
    if (g_file_get_contents(path, &contents, NULL, NULL)) {
        g_strstrip(contents);
        if (zone_allowed(service, contents)) {
            g_free(path);
            return contents;
        }
        g_free(contents);
    }
    g_free(path);
    return g_strdup("UTC");
}

static gboolean set_timezone(Service *service, const char *name, GError **error)
{
    if (!zone_allowed(service, name)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Unknown timezone");
        return FALSE;
    }
    char *zone = g_build_filename(service->root, "usr/share/zoneinfo", name, NULL);
    if (!g_file_test(zone, G_FILE_TEST_IS_REGULAR)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Timezone data missing");
        g_free(zone);
        return FALSE;
    }
    g_free(zone);
    char *directory = root_path(service, "etc");
    char *localtime = g_build_filename(directory, "localtime", NULL);
    char *temporary = g_strdup_printf("%s/.localtime.%ld", directory, (long) getpid());
    char *target = g_strdup_printf("../usr/share/zoneinfo/%s", name);
    unlink(temporary);
    if (symlink(target, temporary) != 0 || rename(temporary, localtime) != 0) {
        int saved_errno = errno;
        unlink(temporary);
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved_errno),
                    "Could not update localtime: %s", g_strerror(saved_errno));
        g_free(directory); g_free(localtime); g_free(temporary); g_free(target);
        return FALSE;
    }
    char *marker = g_build_filename(directory, "timezone", NULL);
    char *value = g_strconcat(name, "\n", NULL);
    GError *marker_error = NULL;
    if (!g_file_set_contents(marker, value, -1, &marker_error)) {
        g_warning("Timezone changed, but /etc/timezone could not be updated: %s",
                  marker_error->message);
        g_clear_error(&marker_error);
    }
    int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) { fsync(fd); close(fd); }
    g_free(service->timezone);
    service->timezone = g_strdup(name);
    g_free(directory); g_free(localtime); g_free(temporary); g_free(target);
    g_free(marker); g_free(value);
    /* The localtime symlink is authoritative even if the marker write failed. */
    return TRUE;
}

static void method_call(GDBusConnection *connection, const char *sender,
                        const char *object_path, const char *interface_name,
                        const char *method, GVariant *parameters,
                        GDBusMethodInvocation *invocation, gpointer data)
{
    (void) sender; (void) object_path; (void) interface_name;
    Service *service = data;
    if (g_str_equal(method, "GetTimezone")) {
        g_dbus_method_invocation_return_value(invocation,
                                              g_variant_new("(s)", service->timezone));
    } else if (g_str_equal(method, "ListTimezones")) {
        GVariantBuilder list;
        g_variant_builder_init(&list, G_VARIANT_TYPE("as"));
        for (guint i = 0; i < service->zones->len; ++i)
            g_variant_builder_add(&list, "s", (char *) g_ptr_array_index(service->zones, i));
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(as)", &list));
    } else if (g_str_equal(method, "SetTimezone")) {
        const char *name;
        g_variant_get(parameters, "(&s)", &name);
        GError *error = NULL;
        if (!set_timezone(service, name, &error)) {
            g_dbus_method_invocation_return_dbus_error(invocation, BUS_NAME ".OperationFailed",
                                                        error->message);
            g_clear_error(&error);
            return;
        }
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(s)", service->timezone));
        g_dbus_connection_emit_signal(connection, NULL, OBJECT_PATH, INTERFACE,
                                      "TimezoneChanged", g_variant_new("(s)", service->timezone), NULL);
    }
}

static const GDBusInterfaceVTable vtable = { .method_call = method_call };

static void bus_closed(GDBusConnection *bus, gboolean vanished, GError *error, gpointer data)
{
    (void) bus; (void) vanished; (void) error;
    g_main_loop_quit(((Service *) data)->loop);
}

int main(int argc, char **argv)
{
    /* --root is for isolated host tests; init starts the daemon without it. */
    if (argc != 1 && (argc != 3 || strcmp(argv[1], "--root") != 0)) {
        g_printerr("usage: timed [--root TEST_ROOT]\n");
        return 2;
    }
    Service service = { .root = argc == 3 ? argv[2] : "/" };
    if (!load_zones(&service)) return 1;
    service.timezone = read_timezone(&service);
    GError *error = NULL;
    service.bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!service.bus) {
        g_printerr("timed: system bus unavailable: %s\n", error->message);
        return 1;
    }
    GDBusNodeInfo *node = openwave_xml_load_dbus_interface(INTERFACE, &error);
    if (!node) {
        g_printerr("timed: invalid interface: %s\n", error->message);
        return 1;
    }
    if (!g_dbus_connection_register_object(service.bus, OBJECT_PATH, node->interfaces[0],
                                           &vtable, &service, NULL, &error)) {
        g_printerr("timed: registration failed: %s\n", error->message);
        return 1;
    }
    service.loop = g_main_loop_new(NULL, FALSE);
    g_signal_connect(service.bus, "closed", G_CALLBACK(bus_closed), &service);
    g_bus_own_name_on_connection(service.bus, BUS_NAME, G_BUS_NAME_OWNER_FLAGS_NONE,
                                 NULL, NULL, NULL, NULL);
    g_main_loop_run(service.loop);
    return 1;
}
