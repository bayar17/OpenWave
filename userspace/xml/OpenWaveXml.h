#ifndef OPENWAVE_XML_H
#define OPENWAVE_XML_H

#ifdef signals
#pragma push_macro("signals")
#undef signals
#define OPENWAVE_XML_SIGNALS_PUSHED
#endif

#include <gio/gio.h>

#ifdef OPENWAVE_XML_SIGNALS_PUSHED
#pragma pop_macro("signals")
#undef OPENWAVE_XML_SIGNALS_PUSHED
#endif

G_BEGIN_DECLS

char *openwave_xml_load_file(const char *filename, GError **error);
GDBusNodeInfo *openwave_xml_load_dbus_file(const char *filename,
                                         const char *interface_name,
                                         GError **error);
GDBusNodeInfo *openwave_xml_load_dbus_interface(const char *interface_name,
                                              GError **error);

G_END_DECLS

#endif
