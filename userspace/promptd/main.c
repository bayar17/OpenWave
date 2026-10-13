#include <OpenWave/OpenWaveXml.h>
#include <gio/gio.h>
#include <glib-unix.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUS "org.openwave.Prompt"
#define PATH "/org/openwave/Prompt"
#define IFACE "org.openwave.Prompt1"
#define ERROR_PREFIX "org.openwave.Prompt.Error."
#define MAX_REQUESTS 64
#define MAX_PENDING 32
#define PER_PROCESS 4
#define PER_MINUTE 12

typedef struct {
    char *id, *sender, *type, *application, *result, *text;
    GVariant *options;
    GRegex *validation;
    guint pid, minimum, maximum;
    gint64 deadline, finished_at;
} Request;
typedef struct { gint64 since; guint count; } Rate;
typedef struct {
    GDBusConnection *bus;
    GMainLoop *loop;
    GQueue *requests;
    GHashTable *rates;
    char *renderer;
    char *active;
    const char *renderer_executable, *renderer_pidfile;
} Service;

static void request_free(Request *r)
{
    g_free(r->id); g_free(r->sender); g_free(r->type); g_free(r->application);
    g_free(r->result); g_free(r->text);
    g_variant_unref(r->options);
    if (r->validation) g_regex_unref(r->validation);
    g_free(r);
}
static void fail(GDBusMethodInvocation *call, const char *kind, const char *message)
{
    char *name = g_strconcat(ERROR_PREFIX, kind, NULL);
    g_dbus_method_invocation_return_dbus_error(call, name, message);
    g_free(name);
}
static gboolean credentials(Service *s, const char *sender, guint *uid, guint *pid)
{
    GVariant *reply = g_dbus_connection_call_sync(s->bus, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "GetConnectionCredentials",
        g_variant_new("(s)", sender), G_VARIANT_TYPE("(a{sv})"),
        G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
    if (!reply) return FALSE;
    GVariant *values = g_variant_get_child_value(reply, 0);
    gboolean okay = g_variant_lookup(values, "UnixUserID", "u", uid)
        && g_variant_lookup(values, "ProcessID", "u", pid);
    g_variant_unref(values); g_variant_unref(reply);
    return okay;
}
static char *executable(guint pid)
{
    char *path = g_strdup_printf("/proc/%u/exe", pid);
    char *result = g_file_read_link(path, NULL);
    g_free(path);
    return result;
}
static gboolean trusted_renderer(Service *s, const char *sender)
{
    guint uid = 0, pid = 0;
    if (!credentials(s, sender, &uid, &pid) || uid != getuid()) return FALSE;
    char *expected = NULL;
    if (!g_file_get_contents(s->renderer_pidfile, &expected, NULL, NULL)) return FALSE;
    char *end = NULL;
    guint64 registered_pid = g_ascii_strtoull(expected, &end, 10);
    gboolean okay = end != expected && registered_pid == pid;
    g_free(expected);
    char *exe = executable(pid);
    okay = okay && exe && g_str_equal(exe, s->renderer_executable);
    g_free(exe);
    return okay;
}
static char *application_id(const char *exe)
{
    GDir *dir = g_dir_open("/usr/share/applications", 0, NULL);
    if (!dir) return g_strdup("");
    const char *name;
    char *result = NULL;
    while (!result && (name = g_dir_read_name(dir))) {
        if (!g_str_has_suffix(name, ".desktop")) continue;
        char *path = g_build_filename("/usr/share/applications", name, NULL);
        GKeyFile *entry = g_key_file_new();
        if (g_key_file_load_from_file(entry, path, G_KEY_FILE_NONE, NULL)) {
            char *command = g_key_file_get_string(entry, "Desktop Entry", "Exec", NULL);
            if (command && g_str_equal(command, exe))
                result = g_key_file_get_string(entry, "Desktop Entry", "X-OpenWave-AppId", NULL);
            g_free(command);
        }
        g_key_file_unref(entry); g_free(path);
    }
    g_dir_close(dir);
    return result ? result : g_strdup("");
}
static Request *find(Service *s, const char *id)
{
    for (GList *n = s->requests->head; n; n = n->next) {
        Request *r = n->data;
        if (g_str_equal(r->id, id)) return r;
    }
    return NULL;
}
static void queue_changed(Service *s)
{
    if (s->renderer) g_dbus_connection_emit_signal(s->bus, s->renderer, PATH,
        IFACE, "QueueChanged", NULL, NULL);
}
static void finish(Service *s, Request *r, const char *result, const char *text)
{
    if (r->result) return;
    r->result = g_strdup(result);
    r->text = g_strdup(text ? text : "");
    r->finished_at = g_get_monotonic_time();
    if (s->active && g_str_equal(s->active, r->id)) g_clear_pointer(&s->active, g_free);
    g_dbus_connection_emit_signal(s->bus, r->sender, PATH, IFACE, "Response",
        g_variant_new("(ss)", r->id, result), NULL);
    queue_changed(s);
}
static void remove_request(Service *s, Request *r)
{
    if (s->active && g_str_equal(s->active, r->id)) g_clear_pointer(&s->active, g_free);
    g_queue_remove(s->requests, r);
    request_free(r);
    queue_changed(s);
}
static gboolean valid_string(GVariant *value, guint max, gboolean multiline)
{
    if (!g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)) return FALSE;
    const char *text = g_variant_get_string(value, NULL);
    if (g_utf8_strlen(text, -1) > max) return FALSE;
    for (const char *p = text; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_iscntrl(c) && !(multiline && (c == '\n' || c == '\t'))) return FALSE;
    }
    return TRUE;
}
static gboolean field_id(const char *id)
{
    if (!id || !g_ascii_isalpha(*id) || strlen(id) > 32) return FALSE;
    for (const char *p = id; *p; ++p)
        if (!g_ascii_isalnum(*p) && *p != '_') return FALSE;
    return TRUE;
}
static gboolean validate_form(GVariant *options)
{
    GVariant *fields = g_variant_lookup_value(options, "fields", G_VARIANT_TYPE("a{sv}"));
    const char *order = NULL;
    if (!fields || !g_variant_lookup(options, "fieldOrder", "&s", &order)) {
        if (fields) g_variant_unref(fields);
        return FALSE;
    }
    char **keys = g_strsplit(order, ",", -1);
    guint count = g_strv_length(keys);
    gboolean okay = count > 0 && count <= 6 && g_variant_n_children(fields) == count;
    for (guint n = 0; okay && n < count; ++n) {
        okay = field_id(keys[n]);
        for (guint k = 0; k < n; ++k) okay &= !g_str_equal(keys[n], keys[k]);
        GVariant *field = g_variant_lookup_value(fields, keys[n], G_VARIANT_TYPE("a{sv}"));
        if (!field) { okay = FALSE; break; }
        GVariantIter iter; const char *key; GVariant *value;
        GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
        g_variant_iter_init(&iter, field);
        while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
            if (g_hash_table_contains(seen, key)) okay = FALSE;
            g_hash_table_add(seen, (gpointer)key);
            if (g_str_equal(key, "label") || g_str_equal(key, "placeholder")) okay &= valid_string(value, 128, FALSE);
            else if (g_str_equal(key, "initialText")) okay &= valid_string(value, 512, FALSE);
            else if (g_str_equal(key, "minLength") || g_str_equal(key, "maxLength") || g_str_equal(key, "maxBytes"))
                okay &= g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32);
            else if (g_str_equal(key, "secret")) okay &= g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN);
            else if (g_str_equal(key, "activeWhenField") || g_str_equal(key, "activeWhenValue"))
                okay &= valid_string(value, 128, FALSE);
            else if (g_str_equal(key, "choices")) {
                if (!g_variant_is_of_type(value, G_VARIANT_TYPE("a{sv}")) ||
                    g_variant_n_children(value) < 1 || g_variant_n_children(value) > 8) okay = FALSE;
                else {
                    GVariantIter choices; const char *choice; GVariant *label;
                    GHashTable *choice_keys = g_hash_table_new(g_str_hash, g_str_equal);
                    g_variant_iter_init(&choices, value);
                    while (g_variant_iter_next(&choices, "{&sv}", &choice, &label)) {
                        okay &= field_id(choice) && !g_hash_table_contains(choice_keys, choice)
                            && valid_string(label, 128, FALSE);
                        g_hash_table_add(choice_keys, (gpointer)choice); g_variant_unref(label);
                    }
                    g_hash_table_unref(choice_keys);
                }
            } else okay = FALSE;
            g_variant_unref(value);
        }
        g_hash_table_unref(seen);
        guint minimum = 0, maximum = 256, bytes = 2048;
        const char *initial = "", *dependency = NULL, *condition = NULL;
        g_variant_lookup(field, "minLength", "u", &minimum);
        g_variant_lookup(field, "maxLength", "u", &maximum);
        g_variant_lookup(field, "maxBytes", "u", &bytes);
        g_variant_lookup(field, "initialText", "&s", &initial);
        g_variant_lookup(field, "activeWhenField", "&s", &dependency);
        g_variant_lookup(field, "activeWhenValue", "&s", &condition);
        okay &= maximum >= 1 && maximum <= 512 && minimum <= maximum && bytes >= 1 && bytes <= 2048
            && g_utf8_strlen(initial, -1) <= maximum && strlen(initial) <= bytes;
        if (dependency || condition) {
            GVariant *other = dependency ? g_variant_lookup_value(fields, dependency, G_VARIANT_TYPE("a{sv}")) : NULL;
            okay &= other && condition && !g_str_equal(dependency, keys[n]);
            if (other) g_variant_unref(other);
        }
        GVariant *choices = g_variant_lookup_value(field, "choices", G_VARIANT_TYPE("a{sv}"));
        if (choices) {
            GVariant *selected = g_variant_lookup_value(choices, initial, G_VARIANT_TYPE_STRING);
            okay &= selected != NULL;
            if (selected) g_variant_unref(selected);
            g_variant_unref(choices);
        }
        g_variant_unref(field);
    }
    g_strfreev(keys); g_variant_unref(fields);
    return okay;
}
static gboolean validate_form_response(GVariant *options, const char *text)
{
    if (strlen(text) > 16384) return FALSE;
    GVariant *values = g_variant_parse(G_VARIANT_TYPE("a{ss}"), text, NULL, NULL, NULL);
    if (!values) return FALSE;
    GVariant *fields = g_variant_lookup_value(options, "fields", G_VARIANT_TYPE("a{sv}"));
    gboolean okay = fields && g_variant_n_children(fields) == g_variant_n_children(values);
    GVariantIter iter; const char *key; GVariant *field;
    if (fields) g_variant_iter_init(&iter, fields);
    while (okay && g_variant_iter_next(&iter, "{&sv}", &key, &field)) {
        const char *value = NULL, *dependency = NULL, *condition = NULL, *other = NULL;
        okay = g_variant_lookup(values, key, "&s", &value);
        guint minimum = 0, maximum = 256, bytes = 2048;
        g_variant_lookup(field, "minLength", "u", &minimum);
        g_variant_lookup(field, "maxLength", "u", &maximum);
        g_variant_lookup(field, "maxBytes", "u", &bytes);
        g_variant_lookup(field, "activeWhenField", "&s", &dependency);
        g_variant_lookup(field, "activeWhenValue", "&s", &condition);
        if (dependency) {
            okay &= g_variant_lookup(values, dependency, "&s", &other);
            if (other && g_strcmp0(other, condition)) {
                okay &= value && !*value;
                g_variant_unref(field); continue;
            }
        }
        if (value) {
            GVariant *string = g_variant_ref_sink(g_variant_new_string(value));
            okay &= valid_string(string, maximum, FALSE) && g_utf8_strlen(value, -1) >= minimum && strlen(value) <= bytes;
            g_variant_unref(string);
            GVariant *choices = g_variant_lookup_value(field, "choices", G_VARIANT_TYPE("a{sv}"));
            if (choices) {
                GVariant *selected = g_variant_lookup_value(choices, value, G_VARIANT_TYPE_STRING);
                okay &= selected != NULL;
                if (selected) g_variant_unref(selected);
                g_variant_unref(choices);
            }
        }
        g_variant_unref(field);
    }
    if (fields) g_variant_unref(fields);
    g_variant_unref(values); return okay;
}
static gboolean validate_selection(GVariant *options)
{
    GVariant *choices = g_variant_lookup_value(options, "choices", G_VARIANT_TYPE("a{sv}"));
    if (!choices) return FALSE;
    gboolean okay = g_variant_n_children(choices) >= 1 && g_variant_n_children(choices) <= 1024;
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    GVariantIter iter; const char *key; GVariant *label;
    g_variant_iter_init(&iter, choices);
    while (g_variant_iter_next(&iter, "{&sv}", &key, &label)) {
        GVariant *name = g_variant_ref_sink(g_variant_new_string(key));
        okay &= *key && valid_string(name, 128, FALSE) && valid_string(label, 128, FALSE)
            && !g_hash_table_contains(seen, key);
        g_hash_table_add(seen, (gpointer)key);
        g_variant_unref(name); g_variant_unref(label);
    }
    const char *initial = "";
    g_variant_lookup(options, "initialText", "&s", &initial);
    if (*initial) {
        GVariant *selected = g_variant_lookup_value(choices, initial, G_VARIANT_TYPE_STRING);
        okay &= selected != NULL;
        if (selected) g_variant_unref(selected);
    }
    g_hash_table_unref(seen); g_variant_unref(choices);
    return okay;
}
static gboolean selection_contains(GVariant *options, const char *value)
{
    GVariant *choices = g_variant_lookup_value(options, "choices", G_VARIANT_TYPE("a{sv}"));
    GVariant *selected = choices ? g_variant_lookup_value(choices, value, G_VARIANT_TYPE_STRING) : NULL;
    gboolean okay = selected != NULL;
    if (selected) g_variant_unref(selected);
    if (choices) g_variant_unref(choices);
    return okay;
}
static gboolean validate_options(const char *type, GVariant *options)
{
    GVariantIter iter;
    const char *key;
    GVariant *value;
    gboolean okay = TRUE;
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    g_variant_iter_init(&iter, options);
    while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
        if (g_hash_table_contains(seen, key)) okay = FALSE;
        g_hash_table_add(seen, (gpointer) key);
        if (g_str_equal(key, "title")) okay &= valid_string(value, 160, FALSE);
        else if (g_str_equal(key, "message")) okay &= valid_string(value, 2048, TRUE);
        else if (g_str_equal(key, "acceptLabel") || g_str_equal(key, "cancelLabel"))
            okay &= valid_string(value, 48, FALSE) && g_variant_is_of_type(value, G_VARIANT_TYPE_STRING)
                && *g_variant_get_string(value, NULL);
        else if (g_str_equal(key, "placeholder") || g_str_equal(key, "initialText"))
            okay &= (g_str_equal(type, "text_input") || g_str_equal(type, "selection")) && valid_string(value, 512, FALSE);
        else if (g_str_equal(key, "choices"))
            okay &= g_str_equal(type, "selection") && g_variant_is_of_type(value, G_VARIANT_TYPE("a{sv}"));
        else if (g_str_equal(key, "validationPattern"))
            okay &= g_str_equal(type, "text_input") && valid_string(value, 128, FALSE);
        else if (g_str_equal(key, "minLength") || g_str_equal(key, "maxLength"))
            okay &= g_str_equal(type, "text_input") && g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32);
        else if (g_str_equal(key, "secret") || g_str_equal(key, "allowPasswordReveal"))
            okay &= g_str_equal(type, "text_input") && g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN);
        else if (g_str_equal(key, "timeoutMs")) okay &= g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32);
        else if (g_str_equal(key, "fields")) okay &= g_str_equal(type, "form") && g_variant_is_of_type(value, G_VARIANT_TYPE("a{sv}"));
        else if (g_str_equal(key, "fieldOrder")) okay &= g_str_equal(type, "form") && valid_string(value, 200, FALSE);
        else okay = FALSE;
        g_variant_unref(value);
    }
    g_hash_table_unref(seen);
    const char *title = NULL;
    gboolean secret = FALSE, reveal = FALSE;
    g_variant_lookup(options, "secret", "b", &secret);
    g_variant_lookup(options, "allowPasswordReveal", "b", &reveal);
    return okay && g_variant_lookup(options, "title", "&s", &title) && *title
        && (!reveal || secret)
        && (!g_str_equal(type, "form") || validate_form(options))
        && (!g_str_equal(type, "selection") || validate_selection(options));
}
static void request(Service *s, const char *sender, GVariant *parameters, GDBusMethodInvocation *call)
{
    const char *type;
    GVariant *options;
    g_variant_get(parameters, "(&s@a{sv})", &type, &options);
    if (!(g_str_equal(type, "message") || g_str_equal(type, "confirmation")
        || g_str_equal(type, "text_input") || g_str_equal(type, "form") || g_str_equal(type, "selection"))
        || g_variant_get_size(options) > (g_str_equal(type, "selection") ? 131072 : 16384)
        || !validate_options(type, options)) {
        fail(call, "InvalidRequest", "Unsupported prompt type or options.");
        g_variant_unref(options); return;
    }
    guint timeout = 120000, minimum = 0, maximum = 256;
    g_variant_lookup(options, "timeoutMs", "u", &timeout);
    g_variant_lookup(options, "minLength", "u", &minimum);
    g_variant_lookup(options, "maxLength", "u", &maximum);
    if (timeout < 1000 || timeout > 300000 || minimum > maximum || maximum < 1 || maximum > 512) {
        fail(call, "InvalidRequest", "Invalid timeout or input length limits.");
        g_variant_unref(options); return;
    }
    const char *pattern = NULL, *initial = "";
    g_variant_lookup(options, "validationPattern", "&s", &pattern);
    g_variant_lookup(options, "initialText", "&s", &initial);
    GRegex *regex = NULL;
    if (pattern) {
        char *anchored = g_strdup_printf("\\A(?:%s)\\z", pattern);
        regex = g_regex_new(anchored, 0, 0, NULL);
        g_free(anchored);
    }
    if ((pattern && !regex) || g_utf8_strlen(initial, -1) > maximum) {
        fail(call, "InvalidRequest", "Invalid input pattern or initial text.");
        if (regex) g_regex_unref(regex);
        g_variant_unref(options); return;
    }
    if (!s->renderer || g_queue_get_length(s->requests) >= MAX_REQUESTS) {
        fail(call, s->renderer ? "Busy" : "RendererUnavailable", "Prompt renderer unavailable or request limit reached.");
        if (regex) g_regex_unref(regex);
        g_variant_unref(options); return;
    }
    guint uid = 0, pid = 0;
    char *exe = NULL;
    if (!credentials(s, sender, &uid, &pid) || uid != getuid() || !(exe = executable(pid))) {
        fail(call, "Unauthorized", "Could not verify the requesting process in this session.");
        if (regex) g_regex_unref(regex);
        g_variant_unref(options); return;
    }
    guint pending = 0, owned = 0;
    for (GList *n = s->requests->head; n; n = n->next) {
        Request *r = n->data;
        pending += r->result == NULL;
        owned += r->pid == pid;
    }
    gint64 now = g_get_monotonic_time();
    Rate *rate = g_hash_table_lookup(s->rates, GUINT_TO_POINTER(pid));
    if (!rate) { rate = g_new0(Rate, 1); g_hash_table_insert(s->rates, GUINT_TO_POINTER(pid), rate); }
    if (now - rate->since > 60 * G_USEC_PER_SEC) { rate->since = now; rate->count = 0; }
    if (pending >= MAX_PENDING || owned >= PER_PROCESS || rate->count >= PER_MINUTE) {
        fail(call, "Busy", "Too many outstanding or frequent prompts.");
        g_free(exe); if (regex) g_regex_unref(regex);
        g_variant_unref(options); return;
    }
    rate->count++;
    Request *r = g_new0(Request, 1);
    r->id = g_uuid_string_random(); r->sender = g_strdup(sender); r->type = g_strdup(type);
    r->options = options; r->pid = pid; r->application = application_id(exe);
    r->minimum = minimum; r->maximum = maximum; r->validation = regex;
    r->deadline = now + (gint64) timeout * 1000;
    g_free(exe);
    g_queue_push_tail(s->requests, r);
    g_dbus_method_invocation_return_value(call, g_variant_new("(s)", r->id));
    queue_changed(s);
}
static void method_call(GDBusConnection *bus, const char *sender, const char *object,
    const char *interface, const char *method, GVariant *parameters, GDBusMethodInvocation *call, gpointer data)
{
    (void) bus; (void) object; (void) interface;
    Service *s = data;
    if (g_str_equal(method, "Request")) { request(s, sender, parameters, call); return; }
    if (g_str_equal(method, "RegisterRenderer")) {
        if (!trusted_renderer(s, sender)) { fail(call, "Unauthorized", "Only the supervised OpenWave Shell may render prompts."); return; }
        if (s->renderer && !g_str_equal(sender, s->renderer)) { fail(call, "Busy", "A renderer is already registered."); return; }
        if (!s->renderer) s->renderer = g_strdup(sender);
        g_dbus_method_invocation_return_value(call, NULL); return;
    }
    if (g_str_equal(method, "Next")) {
        if (!s->renderer || !g_str_equal(sender, s->renderer)) { fail(call, "Unauthorized", "Only the registered renderer may read prompts."); return; }
        Request *next = s->active ? find(s, s->active) : NULL;
        for (GList *n = s->requests->head; !next && n; n = n->next) {
            Request *r = n->data;
            if (!r->result) next = r;
        }
        if (next && !s->active) s->active = g_strdup(next->id);
        GVariantBuilder fields;
        g_variant_builder_init(&fields, G_VARIANT_TYPE("a{sv}"));
        if (next) {
            GVariantIter iter; const char *key; GVariant *value;
            g_variant_iter_init(&iter, next->options);
            while (g_variant_iter_next(&iter, "{&sv}", &key, &value)) {
                if (!g_str_equal(key, "minLength") && !g_str_equal(key, "maxLength"))
                    g_variant_builder_add(&fields, "{sv}", key, value);
                g_variant_unref(value);
            }
            g_variant_builder_add(&fields, "{sv}", "applicationId", g_variant_new_string(next->application));
            g_variant_builder_add(&fields, "{sv}", "ownerPid", g_variant_new_uint32(next->pid));
            g_variant_builder_add(&fields, "{sv}", "minLength", g_variant_new_uint32(next->minimum));
            g_variant_builder_add(&fields, "{sv}", "maxLength", g_variant_new_uint32(next->maximum));
        }
        g_dbus_method_invocation_return_value(call, g_variant_new("(ss@a{sv})",
            next ? next->id : "", next ? next->type : "", g_variant_builder_end(&fields))); return;
    }
    const char *id;
    g_variant_get_child(parameters, 0, "&s", &id);
    Request *r = find(s, id);
    if (!r) { fail(call, "UnknownRequest", "No such prompt request."); return; }
    if (g_str_equal(method, "Respond")) {
        if (!s->renderer || !g_str_equal(sender, s->renderer)) { fail(call, "Unauthorized", "Only the registered renderer may respond."); return; }
        const char *result, *text;
        g_variant_get(parameters, "(&s&s&s)", &id, &result, &text);
        if (r->result || !s->active || !g_str_equal(s->active, id)
            || !(g_str_equal(result, "accepted") || g_str_equal(result, "cancelled"))) {
            fail(call, "InvalidResponse", "This prompt is not active or the result is invalid."); return;
        }
        gboolean accepted = g_str_equal(result, "accepted");
        gboolean input = g_str_equal(r->type, "text_input");
        gboolean form = g_str_equal(r->type, "form");
        gboolean selection = g_str_equal(r->type, "selection");
        glong length = g_utf8_strlen(text, -1);
        gboolean plain = TRUE;
        for (const char *p = text; *p; p = g_utf8_next_char(p))
            if (g_unichar_iscntrl(g_utf8_get_char(p))) plain = FALSE;
        if (((!accepted || (!input && !form && !selection)) && *text)
            || (accepted && selection && !selection_contains(r->options, text))
            || (accepted && form && !validate_form_response(r->options, text)) || (accepted && input &&
                (!plain || length < r->minimum || length > r->maximum
                 || (r->validation && !g_regex_match(r->validation, text, 0, NULL))))) {
            fail(call, "InvalidResponse", "Input does not match the prompt's validation rules."); return;
        }
        finish(s, r, result, text);
        g_dbus_method_invocation_return_value(call, NULL); return;
    }
    if (!g_str_equal(sender, r->sender)) { fail(call, "Unauthorized", "Only the requesting connection may access this result."); return; }
    if (g_str_equal(method, "Cancel")) { finish(s, r, "cancelled", ""); g_dbus_method_invocation_return_value(call, NULL); return; }
    if (g_str_equal(method, "GetResult")) {
        g_dbus_method_invocation_return_value(call, g_variant_new("(bss)", r->result != NULL,
            r->result ? r->result : "", r->text ? r->text : "")); return;
    }
    if (g_str_equal(method, "Acknowledge")) {
        if (!r->result) { fail(call, "Pending", "The prompt has not finished."); return; }
        remove_request(s, r); g_dbus_method_invocation_return_value(call, NULL); return;
    }
    fail(call, "InvalidRequest", "Unknown prompt method.");
}
static void owner_changed(GDBusConnection *bus, const char *sender, const char *path,
    const char *iface, const char *signal, GVariant *parameters, gpointer data)
{
    (void) bus; (void) sender; (void) path; (void) iface; (void) signal;
    Service *s = data;
    const char *name, *old, *new_owner;
    g_variant_get(parameters, "(&s&s&s)", &name, &old, &new_owner);
    if (*new_owner || !*old) return;
    if (s->renderer && g_str_equal(name, s->renderer)) {
        g_clear_pointer(&s->renderer, g_free);
        for (GList *n = s->requests->head; n; n = n->next)
            finish(s, n->data, "renderer_lost", "");
    }
    for (GList *n = s->requests->head; n;) {
        GList *next = n->next; Request *r = n->data;
        if (g_str_equal(name, r->sender)) remove_request(s, r);
        n = next;
    }
}
static gboolean expire(gpointer data)
{
    Service *s = data;
    gint64 now = g_get_monotonic_time();
    for (GList *n = s->requests->head; n;) {
        GList *next = n->next; Request *r = n->data;
        if (!r->result && now >= r->deadline) finish(s, r, "timed_out", "");
        else if (r->result && now - r->finished_at >= 60 * G_USEC_PER_SEC) remove_request(s, r);
        n = next;
    }
    GHashTableIter iter; gpointer value;
    g_hash_table_iter_init(&iter, s->rates);
    while (g_hash_table_iter_next(&iter, NULL, &value))
        if (now - ((Rate *) value)->since >= 60 * G_USEC_PER_SEC) g_hash_table_iter_remove(&iter);
    return G_SOURCE_CONTINUE;
}
static gboolean stop(gpointer data) { g_main_loop_quit(((Service *) data)->loop); return G_SOURCE_REMOVE; }
static void closed(GDBusConnection *bus, gboolean vanished, GError *error, gpointer data)
{ (void) bus; (void) vanished; (void) error; stop(data); }
static void name_lost(GDBusConnection *bus, const char *name, gpointer data)
{ (void) bus; (void) name; stop(data); }
static const GDBusInterfaceVTable vtable = { .method_call = method_call };

int main(int argc, char **argv)
{
    Service s = { .requests = g_queue_new(),
        .rates = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free),
        .renderer_executable = "/usr/bin/ocean-launcher",
        .renderer_pidfile = "/run/openwave/ocean.pid" };
#ifdef PROMPTD_TESTING
    if (argc == 3) { s.renderer_executable = argv[1]; s.renderer_pidfile = argv[2]; }
    else if (argc != 1) return 2;
#else
    (void) argv;
    if (argc != 1) return 2;
#endif
    GError *error = NULL;
    s.bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!s.bus) { g_printerr("promptd: session bus unavailable: %s\n", error->message); g_clear_error(&error); return 1; }
    s.loop = g_main_loop_new(NULL, FALSE);
    GDBusNodeInfo *node = openwave_xml_load_dbus_interface(IFACE, &error);
    if (!node || !g_dbus_connection_register_object(s.bus, PATH, node->interfaces[0], &vtable, &s, NULL, &error)) {
        g_printerr("promptd: object registration failed: %s\n", error->message); g_clear_error(&error); return 1;
    }
    g_signal_connect(s.bus, "closed", G_CALLBACK(closed), &s);
    g_dbus_connection_signal_subscribe(s.bus, "org.freedesktop.DBus", "org.freedesktop.DBus",
        "NameOwnerChanged", "/org/freedesktop/DBus", NULL, G_DBUS_SIGNAL_FLAGS_NONE, owner_changed, &s, NULL);
    guint name = g_bus_own_name_on_connection(s.bus, BUS, G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE,
        NULL, name_lost, &s, NULL);
    g_timeout_add(250, expire, &s);
    g_unix_signal_add(SIGTERM, stop, &s); g_unix_signal_add(SIGINT, stop, &s);
    g_main_loop_run(s.loop);
    for (GList *n = s.requests->head; n; n = n->next) finish(&s, n->data, "service_lost", "");
    g_bus_unown_name(name);
    g_queue_free_full(s.requests, (GDestroyNotify) request_free);
    g_hash_table_unref(s.rates); g_free(s.renderer); g_free(s.active);
    g_dbus_node_info_unref(node); g_main_loop_unref(s.loop); g_object_unref(s.bus);
    return 0;
}
