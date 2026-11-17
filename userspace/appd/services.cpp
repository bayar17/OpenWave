#include "appd.h"
#include <OpenWave/OpenWaveIdentity.h>
#include <OpenWave/OpenWaveXml.h>
#include <cstring>

namespace {
constexpr auto services = "org.openwave.ApplicationServices1";
void dispatch(GDBusConnection *, const char *sender, const char *, const char *, const char *,
    GVariant *args, GDBusMethodInvocation *call, void *data)
{
    auto *m = static_cast<Manager *>(data);
    guint uid = 0, pid = 0;
    if (!appd_credentials(m, sender, &uid, &pid)
        || !openwave_supervised_process(uid, pid, "/run/openwave/permissiond.pid")) {
        g_dbus_method_invocation_return_dbus_error(call, "org.openwave.Applications.Error.Unauthorized", "Only supervised permissiond may subscribe");
        return;
    }
    const char *correlation;
    g_variant_get(args, "(&s)", &correlation);
    if (!g_uuid_string_is_valid(correlation)) {
        g_dbus_method_invocation_return_dbus_error(call, "org.openwave.Applications.Error.InvalidRequest", "A UUID correlation is required");
        return;
    }
    g_free(m->permission_peer); m->permission_peer = g_strdup(sender);
    g_free(m->permission_correlation); m->permission_correlation = g_strdup(correlation);
    GVariantBuilder apps, instances;
    g_variant_builder_init(&apps, G_VARIANT_TYPE("aa{sv}"));
    g_variant_builder_init(&instances, G_VARIANT_TYPE("aa{sv}"));
    GHashTableIter iter; gpointer value; g_hash_table_iter_init(&iter, m->apps);
    while (g_hash_table_iter_next(&iter, nullptr, &value))
        g_variant_builder_add_value(&apps, appd_info(m, static_cast<App *>(value), nullptr));
    for (guint i = 0; i < m->instances->len; ++i) {
        auto *instance = static_cast<Instance *>(g_ptr_array_index(m->instances, i));
        g_variant_builder_add_value(&instances, appd_identity(m, instance, instance->pid));
    }
    g_dbus_method_invocation_return_value(call, g_variant_new("(tus@aa{sv}@aa{sv})",
        m->services_sequence, m->uid, m->session, g_variant_builder_end(&apps), g_variant_builder_end(&instances)));
}
}
extern "C" gboolean appd_register_services(Manager *m, GError **error)
{
    auto *node = openwave_xml_load_dbus_interface(services, error);
    if (!node) return FALSE;
    static const GDBusInterfaceVTable vtable = {dispatch, nullptr, nullptr, {nullptr}};
    m->services_registration = g_dbus_connection_register_object(m->bus, APPD_PATH, node->interfaces[0], &vtable, m, nullptr, error);
    g_dbus_node_info_unref(node);
    return m->services_registration != 0;
}
extern "C" void appd_services_event(Manager *m, const char *event, App *app, Instance *instance)
{
    ++m->services_sequence;
    if (!m->permission_peer) return;
    auto *identity = instance ? appd_identity(m, instance, instance->pid) : appd_info(m, app, nullptr);
    g_dbus_connection_emit_signal(m->bus, m->permission_peer, APPD_PATH, services, "LifecycleEvent",
        g_variant_new("(sts@a{sv})", m->permission_correlation, m->services_sequence, event, identity), nullptr);
}
