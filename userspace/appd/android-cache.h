#pragma once
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

gboolean appd_android_cache_save(const char *root, const guchar *data, gsize length);
gboolean appd_android_cache_load(const char *root, gsize limit, guchar **data, gsize *length);

#ifdef __cplusplus
}
#endif
