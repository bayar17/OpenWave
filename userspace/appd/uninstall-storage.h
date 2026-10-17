#ifndef OPENWAVE_UNINSTALL_STORAGE_H
#define OPENWAVE_UNINSTALL_STORAGE_H
#include <glib.h>
G_BEGIN_DECLS
gboolean appd_uninstall_storage(const char *bundles, const char *data, const char *id, GError **error);
G_END_DECLS
#endif
