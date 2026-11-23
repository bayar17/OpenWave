#ifndef APPD_PREFERENCES_H
#define APPD_PREFERENCES_H

#include <gio/gio.h>
#include <glib.h>
#include <sys/types.h>

G_BEGIN_DECLS

#define APPD_PREFERENCES_MAX_BYTES (256 * 1024)
#define APPD_PREFERENCES_MAX_LIST 128

typedef enum {
    APPD_PREFERENCE_BOOL,
    APPD_PREFERENCE_STRING,
    APPD_PREFERENCE_LIST,
    APPD_PREFERENCE_RESET
} AppdPreferenceKind;

typedef struct {
    AppdPreferenceKind kind;
    const char *key;
    gboolean boolean;
    const char *string;
    const char *const *list;
} AppdPreferenceChange;

gboolean appd_preference_valid_id(const char *id);
gboolean appd_preference_allowed(AppdPreferenceKind kind, const char *key);
char *appd_preference_encode_string(const char *value);
gboolean appd_preferences_apply(const char *path, uid_t owner, gid_t group, const AppdPreferenceChange *change, GError **error);
char *appd_preferences_public(GKeyFile *private_file, gsize *length);

G_END_DECLS
#endif
