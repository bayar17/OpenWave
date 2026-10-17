#pragma once
#include <gio/gio.h>
G_BEGIN_DECLS
guint64 openwave_process_start(guint pid);
gboolean openwave_supervised_process(guint uid, guint pid, const char *pidfile);
char *openwave_service_owner(GDBusConnection *bus, const char *name, const char *pidfile, GError **error);
GVariant *openwave_resolve_caller(GDBusConnection *bus, const char *sender, GError **error);
G_END_DECLS
