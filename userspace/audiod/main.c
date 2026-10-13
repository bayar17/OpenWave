#include <OpenWave/OpenWaveXml.h>
#include <gio/gio.h>
#include <pipewire/keys.h>
#include <wp/wp.h>

#define BUS_NAME "org.openwave.Audio"
#define OBJECT_PATH "/org/openwave/Audio"
#define INTERFACE "org.openwave.Audio1"

typedef struct {
    GMainLoop *loop;
    GDBusConnection *bus;
    GDBusNodeInfo *node;
    WpCore *core;
    WpObjectManager *manager;
    WpPlugin *defaults;
    WpPlugin *mixer;
    guint registration;
    guint bus_owner;
    guint pending_plugins;
    gboolean ready;
} AudioService;

static void emit_changed(AudioService *service)
{
    if (service->bus && service->registration)
        g_dbus_connection_emit_signal(service->bus, NULL, OBJECT_PATH,
                                      INTERFACE, "StateChanged", NULL, NULL);
}

static void on_nodes_changed(WpObjectManager *manager, WpObject *object,
                             AudioService *service)
{
    (void) manager;
    (void) object;
    emit_changed(service);
}

static void on_default_changed(WpPlugin *plugin, AudioService *service)
{
    (void) plugin;
    emit_changed(service);
}

static void on_mixer_changed(WpPlugin *plugin, guint id, AudioService *service)
{
    (void) plugin;
    (void) id;
    emit_changed(service);
}

static guint32 default_sink_id(AudioService *service)
{
    guint32 id = 0;
    if (service->ready && service->defaults)
        g_signal_emit_by_name(service->defaults, "get-default-node",
                              "Audio/Sink", &id);
    return id == G_MAXUINT32 ? 0 : id;
}

static WpNode *find_sink(AudioService *service, guint32 id)
{
    if (!service->ready || !id || !service->manager)
        return NULL;
    WpNode *node = wp_object_manager_lookup(service->manager, WP_TYPE_NODE,
        WP_CONSTRAINT_TYPE_G_PROPERTY, "bound-id", "=u", id, NULL);
    if (!node)
        return NULL;
    const char *media_class = wp_pipewire_object_get_property(
        WP_PIPEWIRE_OBJECT(node), PW_KEY_MEDIA_CLASS);
    if (!media_class || !g_str_has_prefix(media_class, "Audio/Sink")) {
        g_object_unref(node);
        return NULL;
    }
    return node;
}

static const char *node_label(WpNode *node)
{
    const char *label = wp_pipewire_object_get_property(
        WP_PIPEWIRE_OBJECT(node), PW_KEY_NODE_DESCRIPTION);
    if (!label || !*label)
        label = wp_pipewire_object_get_property(WP_PIPEWIRE_OBJECT(node),
                                                PW_KEY_NODE_NICK);
    if (!label || !*label)
        label = wp_pipewire_object_get_property(WP_PIPEWIRE_OBJECT(node),
                                                PW_KEY_NODE_NAME);
    return label ? label : "Audio output";
}

static GVariant *state_variant(AudioService *service)
{
    GVariantBuilder state;
    g_variant_builder_init(&state, G_VARIANT_TYPE_VARDICT);
    g_variant_builder_add(&state, "{sv}", "Available",
                          g_variant_new_boolean(service->ready));
    guint32 id = default_sink_id(service);
    g_variant_builder_add(&state, "{sv}", "DefaultSinkId",
                          g_variant_new_uint32(id));
    WpNode *node = find_sink(service, id);
    g_variant_builder_add(&state, "{sv}", "DefaultSink",
                          g_variant_new_string(node ? node_label(node) : ""));
    g_variant_builder_add(&state, "{sv}", "HasOutput",
                          g_variant_new_boolean(node != NULL));
    gdouble volume = 0.0;
    gboolean muted = FALSE;
    if (node && service->mixer) {
        GVariant *value = NULL;
        g_signal_emit_by_name(service->mixer, "get-volume", id, &value);
        if (value) {
            g_variant_lookup(value, "volume", "d", &volume);
            g_variant_lookup(value, "mute", "b", &muted);
            g_variant_unref(value);
        }
    }
    g_variant_builder_add(&state, "{sv}", "Volume",
                          g_variant_new_uint32((guint) (CLAMP(volume, 0.0, 1.0) * 100.0 + 0.5)));
    g_variant_builder_add(&state, "{sv}", "Muted",
                          g_variant_new_boolean(muted));
    g_clear_object(&node);
    return g_variant_new("(a{sv})", &state);
}

static GVariant *devices_variant(AudioService *service)
{
    GVariantBuilder devices;
    g_variant_builder_init(&devices, G_VARIANT_TYPE("aa{sv}"));
    if (service->ready) {
        WpIterator *iterator = wp_object_manager_new_filtered_iterator(
            service->manager, WP_TYPE_NODE, NULL);
        GValue value = G_VALUE_INIT;
        guint32 default_id = default_sink_id(service);
        while (wp_iterator_next(iterator, &value)) {
            WpNode *node = g_value_get_object(&value);
            const char *media_class = wp_pipewire_object_get_property(
                WP_PIPEWIRE_OBJECT(node), PW_KEY_MEDIA_CLASS);
            if (media_class && (g_str_has_prefix(media_class, "Audio/Sink") ||
                                g_str_has_prefix(media_class, "Audio/Source"))) {
                guint32 id = wp_proxy_get_bound_id(WP_PROXY(node));
                GVariantBuilder entry;
                g_variant_builder_init(&entry, G_VARIANT_TYPE_VARDICT);
                g_variant_builder_add(&entry, "{sv}", "Id", g_variant_new_uint32(id));
                g_variant_builder_add(&entry, "{sv}", "Name",
                                      g_variant_new_string(node_label(node)));
                g_variant_builder_add(&entry, "{sv}", "MediaClass",
                                      g_variant_new_string(media_class));
                g_variant_builder_add(&entry, "{sv}", "Default",
                                      g_variant_new_boolean(id == default_id));
                g_variant_builder_add_value(&devices, g_variant_builder_end(&entry));
            }
            g_value_unset(&value);
        }
        wp_iterator_unref(iterator);
    }
    return g_variant_new("(aa{sv})", &devices);
}

static void method_call(GDBusConnection *connection, const char *sender,
                        const char *object_path, const char *interface_name,
                        const char *method, GVariant *parameters,
                        GDBusMethodInvocation *invocation, gpointer data)
{
    (void) connection;
    (void) sender;
    (void) object_path;
    (void) interface_name;
    AudioService *service = data;
    if (g_str_equal(method, "GetState")) {
        g_dbus_method_invocation_return_value(invocation, state_variant(service));
        return;
    }
    if (g_str_equal(method, "ListDevices")) {
        g_dbus_method_invocation_return_value(invocation, devices_variant(service));
        return;
    }
    if (!service->ready || !service->defaults || !service->mixer) {
        g_dbus_method_invocation_return_dbus_error(invocation,
            BUS_NAME ".Unavailable", "Audio service is not ready");
        return;
    }

    guint32 id = default_sink_id(service);
    if (g_str_equal(method, "SetDefaultSink"))
        g_variant_get(parameters, "(u)", &id);
    WpNode *node = find_sink(service, id);
    if (!node) {
        g_dbus_method_invocation_return_dbus_error(invocation,
            BUS_NAME ".NoOutput", "No matching audio output is available");
        return;
    }
    gboolean success = FALSE;
    if (g_str_equal(method, "SetDefaultSink")) {
        const char *name = wp_pipewire_object_get_property(
            WP_PIPEWIRE_OBJECT(node), PW_KEY_NODE_NAME);
        if (name)
            g_signal_emit_by_name(service->defaults,
                "set-default-configured-node-name", "Audio/Sink", name, &success);
    } else {
        GVariantBuilder change;
        g_variant_builder_init(&change, G_VARIANT_TYPE_VARDICT);
        if (g_str_equal(method, "SetVolume")) {
            guint volume;
            g_variant_get(parameters, "(u)", &volume);
            if (volume > 100) {
                g_object_unref(node);
                g_dbus_method_invocation_return_dbus_error(invocation,
                    BUS_NAME ".InvalidArgument", "Volume must be between 0 and 100");
                return;
            }
            g_variant_builder_add(&change, "{sv}", "volume",
                                  g_variant_new_double(volume / 100.0));
        } else if (g_str_equal(method, "SetMuted")) {
            gboolean muted;
            g_variant_get(parameters, "(b)", &muted);
            g_variant_builder_add(&change, "{sv}", "mute",
                                  g_variant_new_boolean(muted));
        } else {
            g_object_unref(node);
            g_dbus_method_invocation_return_dbus_error(invocation,
                BUS_NAME ".InvalidArgument", "Unknown audio operation");
            return;
        }
        GVariant *value = g_variant_builder_end(&change);
        g_signal_emit_by_name(service->mixer, "set-volume", id, value, &success);
    }
    g_object_unref(node);
    if (!success) {
        g_dbus_method_invocation_return_dbus_error(invocation,
            BUS_NAME ".OperationFailed", "Audio operation failed");
        return;
    }
    g_dbus_method_invocation_return_value(invocation, NULL);
    emit_changed(service);
}

static const GDBusInterfaceVTable interface_vtable = {
    .method_call = method_call,
};

static void on_manager_installed(WpObjectManager *manager, AudioService *service)
{
    (void) manager;
    service->ready = TRUE;
    emit_changed(service);
}

static void on_plugin_loaded(WpCore *core, GAsyncResult *result, AudioService *service)
{
    GError *error = NULL;
    if (!wp_core_load_component_finish(core, result, &error)) {
        g_warning("Audio API module could not load: %s", error->message);
        g_clear_error(&error);
        g_main_loop_quit(service->loop);
        return;
    }
    if (--service->pending_plugins != 0)
        return;
    service->defaults = wp_plugin_find(core, "default-nodes-api");
    service->mixer = wp_plugin_find(core, "mixer-api");
    if (!service->defaults || !service->mixer) {
        g_warning("Audio API modules are unavailable");
        g_main_loop_quit(service->loop);
        return;
    }
    g_object_set(service->mixer, "scale", 1, NULL);
    g_signal_connect(service->defaults, "changed", G_CALLBACK(on_default_changed), service);
    g_signal_connect(service->mixer, "changed", G_CALLBACK(on_mixer_changed), service);
    wp_core_install_object_manager(service->core, service->manager);
}

static void on_disconnected(WpCore *core, AudioService *service)
{
    (void) core;
    service->ready = FALSE;
    emit_changed(service);
    g_main_loop_quit(service->loop);
}

int main(void)
{
    AudioService service = {0};
    GError *error = NULL;
    wp_init(WP_INIT_ALL);
    service.loop = g_main_loop_new(NULL, FALSE);
    service.node = openwave_xml_load_dbus_interface(INTERFACE, &error);
    if (!service.node) {
        g_printerr("audiod D-Bus interface: %s\n", error->message);
        return 1;
    }
    service.bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (!service.bus) {
        g_printerr("audiod system bus: %s\n", error->message);
        return 1;
    }
    service.registration = g_dbus_connection_register_object(service.bus,
        OBJECT_PATH, service.node->interfaces[0], &interface_vtable,
        &service, NULL, &error);
    if (!service.registration) {
        g_printerr("audiod registration: %s\n", error->message);
        return 1;
    }
    service.bus_owner = g_bus_own_name_on_connection(service.bus, BUS_NAME,
        G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);
    service.core = wp_core_new(NULL, NULL, NULL);
    service.manager = wp_object_manager_new();
    wp_object_manager_add_interest(service.manager, WP_TYPE_NODE, NULL);
    wp_object_manager_request_object_features(service.manager, WP_TYPE_NODE,
        WP_PIPEWIRE_OBJECT_FEATURES_MINIMAL);
    g_signal_connect(service.manager, "installed", G_CALLBACK(on_manager_installed), &service);
    g_signal_connect(service.manager, "object-added", G_CALLBACK(on_nodes_changed), &service);
    g_signal_connect(service.manager, "object-removed", G_CALLBACK(on_nodes_changed), &service);
    g_signal_connect(service.core, "disconnected", G_CALLBACK(on_disconnected), &service);
    service.pending_plugins = 2;
    wp_core_load_component(service.core, "libwireplumber-module-default-nodes-api",
        "module", NULL, NULL, NULL, (GAsyncReadyCallback) on_plugin_loaded, &service);
    wp_core_load_component(service.core, "libwireplumber-module-mixer-api",
        "module", NULL, NULL, NULL, (GAsyncReadyCallback) on_plugin_loaded, &service);
    if (!wp_core_connect(service.core)) {
        g_printerr("audiod could not connect to PipeWire\n");
        return 1;
    }
    g_main_loop_run(service.loop);
    g_bus_unown_name(service.bus_owner);
    g_dbus_connection_unregister_object(service.bus, service.registration);
    g_clear_object(&service.defaults);
    g_clear_object(&service.mixer);
    g_clear_object(&service.manager);
    g_clear_object(&service.core);
    g_clear_object(&service.bus);
    g_dbus_node_info_unref(service.node);
    g_main_loop_unref(service.loop);
    return 1;
}
