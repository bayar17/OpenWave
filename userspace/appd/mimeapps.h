#ifndef APPD_MIMEAPPS_H
#define APPD_MIMEAPPS_H

#include "desktop.h"

G_BEGIN_DECLS

#define MIMEAPPS_MAX_BYTES (256 * 1024)

typedef struct MimeApps MimeApps;
typedef gboolean (*MimeAppsUsable)(const char *desktop_id, gpointer data);

MimeApps *mimeapps_load(const DesktopPaths *paths);
void mimeapps_free(MimeApps *apps);
const char *mimeapps_digest(const MimeApps *apps);
char **mimeapps_default_ids(const MimeApps *apps, const char *mime);
char *mimeapps_default(const MimeApps *apps, const char *mime, MimeAppsUsable usable, gpointer data);
GPtrArray *mimeapps_associated(const MimeApps *apps, const char *mime, GPtrArray *entries);
GPtrArray *mimeapps_known_types(const MimeApps *apps);
char *mimeapps_user_path(const DesktopPaths *paths);

gboolean mimeapps_set_default(const DesktopPaths *paths, const char *mime, const char *desktop_id, GError **error);
gboolean mimeapps_clear_default(const DesktopPaths *paths, const char *mime, GError **error);
gboolean mimeapps_clear_application(const DesktopPaths *paths, const char *desktop_id, GError **error);

G_END_DECLS
#endif
