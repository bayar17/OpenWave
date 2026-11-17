#pragma once
#include <glib.h>
#include <sys/types.h>

gboolean appd_user_folders(const char *home, const char *data, uid_t uid, GError **error);
