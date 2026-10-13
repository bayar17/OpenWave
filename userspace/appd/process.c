#define _GNU_SOURCE
#include "appd.h"
#include <glib-unix.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/vfs.h>
#include <sys/signalfd.h>
#include <linux/magic.h>
#include <unistd.h>

Instance *appd_instance(Manager *m, App *app)
{
    for (guint i = m->instances->len; i > 0; --i) {
        Instance *instance = g_ptr_array_index(m->instances, i - 1);
        if (instance->app == app) return instance;
    }
    return NULL;
}

void appd_emit(Manager *m, const char *signal, GVariant *parameters)
{
    if (m->bus) g_dbus_connection_emit_signal(m->bus, NULL, APPD_PATH, APPD_INTERFACE, signal, parameters, NULL);
}

void appd_state(Manager *m, App *app, const char *state, const char *error)
{
    g_free(app->state); app->state = g_strdup(state);
    g_free(app->error); app->error = g_strdup(error ? error : "");
    Instance *instance = appd_instance(m, app);
    appd_emit(m, "ApplicationStateChanged", g_variant_new("(ssus)", app->id, state,
        instance ? instance->pid : 0, app->error));
}

gboolean appd_arguments(App *app, char **arguments, GError **error)
{
    guint count = g_strv_length(arguments);
    if ((!app->paths && count) || count > 16) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "This application does not accept these arguments");
        return FALSE;
    }
    for (guint i = 0; i < count; ++i)
        if (strlen(arguments[i]) > 4096 || arguments[i][0] != '/' || strchr(arguments[i], '\n')) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Application arguments must be absolute file paths");
            return FALSE;
        }
    return TRUE;
}

static guint64 start_time(GPid pid)
{
    char *path = g_strdup_printf("/proc/%d/stat", pid), *contents = NULL;
    g_file_get_contents(path, &contents, NULL, NULL); g_free(path);
    if (!contents) return 0;
    char *end = strrchr(contents, ')'); guint64 result = 0;
    if (end && end[1] == ' ') {
        char **fields = g_strsplit(end + 2, " ", -1);
        if (g_strv_length(fields) > 19) result = g_ascii_strtoull(fields[19], NULL, 10);
        g_strfreev(fields);
    }
    g_free(contents); return result;
}

static void journal(Instance *instance, gboolean remove)
{
    char *path = g_strdup_printf("%s/%d.ini", instance->manager->state_dir, instance->pid);
    if (remove) unlink(path);
    else {
        GKeyFile *file = g_key_file_new();
        g_key_file_set_string(file, "Process", "AppId", instance->app->id);
        g_key_file_set_string(file, "Process", "Executable", instance->app->argv[0]);
        g_key_file_set_uint64(file, "Process", "Pid", instance->pid);
        g_key_file_set_uint64(file, "Process", "StartTime", instance->start_time);
        g_key_file_save_to_file(file, path, NULL);
        g_key_file_unref(file);
    }
    g_free(path);
}

static void kill_cgroup(const char *directory)
{
    if (!directory) return;
    char *path = g_build_filename(directory, "cgroup.kill", NULL);
    int fd = open(path, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd >= 0) { if (write(fd, "1", 1) < 0) g_warning("appd: cgroup cleanup failed: %s", g_strerror(errno)); close(fd); }
    g_free(path); rmdir(directory);
}

static void remove_preview(GPid pid)
{
    const char *directory = "/tmp/openwave/process/preview";
    GDir *dir = g_dir_open(directory, 0, NULL);
    if (!dir) return;
    char *prefix = g_strdup_printf("%d-", pid);
    const char *name;
    while ((name = g_dir_read_name(dir))) {
        if (!g_str_has_prefix(name, prefix) || !g_str_has_suffix(name, ".jpg")) continue;
        char *path = g_build_filename(directory, name, NULL); unlink(path); g_free(path);
    }
    g_free(prefix); g_dir_close(dir);
}

static void exited(GPid pid, gint status, gpointer data)
{
    Instance *instance = data; Manager *m = instance->manager; App *app = instance->app;
    gboolean expected = instance->stopping || (WIFEXITED(status) && WEXITSTATUS(status) == 0);
    if (instance->deadline) g_source_remove(instance->deadline);
    /* The group belongs exclusively to this launch, including WPE helpers. */
    kill(-pid, SIGKILL);
    kill_cgroup(instance->cgroup);
    journal(instance, TRUE);
    remove_preview(pid);
    char *socket = g_strdup_printf("%s/openwave-app-%d.socket", m->runtime, pid);
    unlink(socket); g_free(socket);
    g_spawn_close_pid(pid);
    g_ptr_array_remove_fast(m->instances, instance);
    g_free(instance->cgroup); g_free(instance);
    if (expected) appd_emit(m, "ApplicationStopped", g_variant_new("(si)", app->id, status));
    else appd_emit(m, "ApplicationCrashed", g_variant_new("(si)", app->id, status));
    appd_state(m, app, appd_instance(m, app) ? "Running" : expected ? "Stopped" : "Crashed",
        expected ? "" : "Application exited unexpectedly");
    appd_registry_scan(m);
    if (m->quitting && !m->instances->len) g_main_loop_quit(m->loop);
}

static gboolean child_events(gint fd, GIOCondition condition, gpointer data)
{
    (void)condition;
    Manager *m = data;
    struct signalfd_siginfo event;
    while (read(fd, &event, sizeof(event)) == sizeof(event)) {}
    int status;
    GPid pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (guint i = 0; i < m->instances->len; ++i) {
            Instance *instance = g_ptr_array_index(m->instances, i);
            if (instance->pid == pid) { exited(pid, status, instance); break; }
        }
    }
    return G_SOURCE_CONTINUE;
}

gboolean appd_watch_children(Manager *m, GError **error)
{
    sigset_t mask;
    sigemptyset(&mask); sigaddset(&mask, SIGCHLD);
    if (sigprocmask(SIG_BLOCK, &mask, NULL)) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot block child signals: %s", g_strerror(errno)); return FALSE;
    }
    int fd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot monitor child signals: %s", g_strerror(errno)); return FALSE;
    }
    g_unix_fd_add(fd, G_IO_IN, child_events, m);
    return TRUE;
}

static gboolean force_exit(gpointer data)
{
    Instance *instance = data; instance->deadline = 0;
    kill(-instance->pid, SIGKILL);
    kill_cgroup(instance->cgroup);
    return G_SOURCE_REMOVE;
}

typedef struct {
    Manager *manager;
    GPid pid;
    char **arguments;
    gboolean quit;
    guint retries;
    GCancellable *cancel;
    guint timeout;
    GSocketConnection *connection;
    char *message;
    char reply[16385];
    gsize reply_size;
} Command;

static Instance *command_instance(Command *c)
{
    for (guint i = 0; i < c->manager->instances->len; ++i) {
        Instance *instance = g_ptr_array_index(c->manager->instances, i);
        if (instance->pid == c->pid) return instance;
    }
    return NULL;
}

static void send_command(Command *command);
static gboolean retry_command(gpointer data)
{
    send_command(data); return G_SOURCE_REMOVE;
}
static void command_done(Command *c, gboolean success)
{
    if (c->timeout) { g_source_remove(c->timeout); c->timeout = 0; }
    g_clear_object(&c->connection); g_clear_object(&c->cancel);
    Instance *instance = command_instance(c);
    if (!success && instance && !c->quit && !instance->stopping && c->retries--) {
        g_timeout_add(100, retry_command, c); return;
    }
    if (instance && c->quit && !success) kill(-c->pid, SIGTERM);
    if (instance && !c->quit && !instance->stopping) {
        if (success) appd_emit(c->manager, "ActivationRequested", g_variant_new("(su)", instance->app->id, c->pid));
        else appd_emit(c->manager, "OperationFailed", g_variant_new("(ss)", instance->app->id, "Application activation endpoint is unavailable"));
    }
    g_strfreev(c->arguments); g_free(c->message); g_free(c);
}
static gboolean command_timeout(gpointer data)
{
    Command *c = data; c->timeout = 0; g_cancellable_cancel(c->cancel); return G_SOURCE_REMOVE;
}
static void read_reply(GObject *source, GAsyncResult *result, gpointer data)
{
    Command *c = data;
    gssize size = g_input_stream_read_finish(G_INPUT_STREAM(source), result, NULL);
    if (size <= 0) { command_done(c, FALSE); return; }
    c->reply_size += size; c->reply[c->reply_size] = 0;
    char *newline = strchr(c->reply, '\n');
    if (newline) {
        *newline = 0;
        command_done(c, !strcmp(c->reply, "{\"error\":\"\"}") || !strcmp(c->reply, "{\"error\": \"\"}"));
    } else if (c->reply_size == sizeof(c->reply) - 1) command_done(c, FALSE);
    else g_input_stream_read_async(G_INPUT_STREAM(source), c->reply + c->reply_size,
        sizeof(c->reply) - c->reply_size - 1, G_PRIORITY_DEFAULT, c->cancel, read_reply, c);
}
static void written(GObject *source, GAsyncResult *result, gpointer data)
{
    Command *c = data;
    if (!g_output_stream_write_all_finish(G_OUTPUT_STREAM(source), result, NULL, NULL)) { command_done(c, FALSE); return; }
    GInputStream *input = g_io_stream_get_input_stream(G_IO_STREAM(c->connection));
    g_input_stream_read_async(input, c->reply, sizeof(c->reply) - 1, G_PRIORITY_DEFAULT, c->cancel, read_reply, c);
}
static void connected(GObject *source, GAsyncResult *result, gpointer data)
{
    Command *c = data;
    c->connection = g_socket_client_connect_finish(G_SOCKET_CLIENT(source), result, NULL);
    if (!c->connection) { command_done(c, FALSE); return; }
    struct ucred peer; socklen_t size = sizeof(peer);
    int fd = g_socket_get_fd(g_socket_connection_get_socket(c->connection));
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) || peer.uid != c->manager->uid || peer.pid != c->pid) {
        command_done(c, FALSE); return;
    }
    g_output_stream_write_all_async(g_io_stream_get_output_stream(G_IO_STREAM(c->connection)),
        c->message, strlen(c->message), G_PRIORITY_DEFAULT, c->cancel, written, c);
}
static void send_command(Command *c)
{
    if (!command_instance(c)) { command_done(c, FALSE); return; }
    c->cancel = g_cancellable_new();
    c->reply_size = 0;
    c->timeout = g_timeout_add(1000, command_timeout, c);
    char *path = g_strdup_printf("%s/openwave-app-%d.socket", c->manager->runtime, c->pid);
    GSocketAddress *address = g_unix_socket_address_new(path); g_free(path);
    GSocketClient *client = g_socket_client_new();
    g_socket_client_connect_async(client, G_SOCKET_CONNECTABLE(address), c->cancel, connected, c);
    g_object_unref(address); g_object_unref(client);
}
static void quote(GString *text, const char *value)
{
    g_string_append_c(text, '"');
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '\\') g_string_append_c(text, '\\');
        if (*p < 32) g_string_append_printf(text, "\\u%04x", *p);
        else g_string_append_c(text, *p);
    }
    g_string_append_c(text, '"');
}
static void command(Instance *instance, char **arguments, gboolean quit)
{
    Command *c = g_new0(Command, 1); c->manager = instance->manager; c->pid = instance->pid;
    c->quit = quit; c->retries = quit ? 0 : 8; c->arguments = g_strdupv(arguments);
    GString *json = g_string_new(quit ? "{\"command\":\"quit\"}\n" : "{\"command\":\"activate\",\"arguments\":[");
    if (!quit) {
        for (guint i = 0; arguments && arguments[i]; ++i) {
            if (i) g_string_append_c(json, ',');
            quote(json, arguments[i]);
        }
        g_string_append(json, "]}\n");
    }
    c->message = g_string_free(json, FALSE); send_command(c);
}

void appd_activate(Manager *m, Instance *instance, char **arguments)
{
    if (arguments && arguments[0]) command(instance, arguments, FALSE);
    else appd_emit(m, "ActivationRequested", g_variant_new("(su)", instance->app->id, instance->pid));
}

typedef struct { Manager *manager; pid_t parent; const char *cgroup_procs; } Setup;
static void child_setup(gpointer data)
{
    Setup *setup = data; Manager *m = setup->manager;
    if (setsid() < 0) _exit(126);
    sigset_t mask; sigemptyset(&mask); sigprocmask(SIG_SETMASK, &mask, NULL);
    if (setup->cgroup_procs) {
        int fd = open(setup->cgroup_procs, O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd < 0 || write(fd, "0", 1) != 1) _exit(126);
        close(fd);
    }
    umask(0077);
    if (geteuid() == 0 && (setgroups(0, NULL) || setgid(m->gid) || setuid(m->uid))) _exit(126);
    if (geteuid() != m->uid || getegid() != m->gid) _exit(126);
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() != setup->parent) _exit(126);
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) _exit(126);
}

static char **environment(Manager *m, App *app)
{
    char **env = g_new0(char *, 1);
    const char *names[] = {"LANG", "LC_ALL", "LC_MESSAGES", "LC_CTYPE", NULL};
    for (guint i = 0; names[i]; ++i)
        if (g_getenv(names[i])) env = g_environ_setenv(env, names[i], g_getenv(names[i]), TRUE);
    const char *pairs[][2] = {{"PATH", "/usr/bin:/bin"}, {"QT_QPA_PLATFORM", "wayland"},
        {"QT_WAYLAND_DISABLE_WINDOWDECORATION", "1"}, {"WAYLAND_DISPLAY", "wayland-0"},
        {"QSG_RHI_BACKEND", "opengl"}, {"DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/run/dbus/system_bus_socket"},
        {"OPENWAVE_APP_ID", app->id}, {"HOME", m->home}, {"USER", m->user}, {"LOGNAME", m->user},
        {"XDG_RUNTIME_DIR", m->runtime}};
    for (guint i = 0; i < G_N_ELEMENTS(pairs); ++i) env = g_environ_setenv(env, pairs[i][0], pairs[i][1], TRUE);
    char *session = g_strdup_printf("unix:path=%s/bus", m->runtime);
    env = g_environ_setenv(env, "DBUS_SESSION_BUS_ADDRESS", session, TRUE); g_free(session);
    char *config = m->uid == 0 ? g_strdup("/var/lib/openwave") : g_build_filename(m->home, ".config", NULL);
    char *state = g_build_filename(config, "state", NULL);
    env = g_environ_setenv(env, "XDG_CONFIG_HOME", config, TRUE);
    env = g_environ_setenv(env, "XDG_STATE_HOME", state, TRUE);
    g_free(config); g_free(state); return env;
}

gboolean appd_launch(Manager *m, App *app, char **arguments, GError **error)
{
    char *socket = g_build_filename(m->runtime, "wayland-0", NULL);
    struct stat st;
    gboolean ready = m->ready && !m->quitting && lstat(socket, &st) == 0 && S_ISSOCK(st.st_mode) && st.st_uid == m->uid;
    g_free(socket);
    if (!ready) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, "The Wayland session is not ready"); return FALSE; }
    Instance *existing = appd_instance(m, app);
    if (existing && existing->stopping) { g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_BUSY, "Application is stopping"); return FALSE; }
    if (existing && app->single) { appd_activate(m, existing, arguments); return TRUE; }
    if (!appd_verified_file(app->desktop, FALSE, error) || !appd_verified_file(app->argv[0], TRUE, error)) return FALSE;
    char *data = g_strdup_printf("%s/.DataLocation/Applications/%s", m->home, app->id);
    /* Changing existing data ownership is deliberately excluded from this migration. */
    if (m->uid == 0 && g_mkdir_with_parents(data, 0700)) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot create application data directory: %s", g_strerror(errno));
        g_free(data); return FALSE;
    }
    if (m->uid != 0 && (stat(data, &st) || st.st_uid != m->uid || (st.st_mode & 0077))) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "Non-root application data must be provisioned by the session administrator");
        g_free(data); return FALSE;
    }
    g_free(data);
    GPtrArray *argv = g_ptr_array_new();
    for (char **p = app->argv; *p; ++p) g_ptr_array_add(argv, *p);
    for (char **p = arguments; *p; ++p) g_ptr_array_add(argv, *p);
    g_ptr_array_add(argv, NULL);
    char **env = environment(m, app);
    Instance *instance = g_new0(Instance, 1); instance->manager = m; instance->app = app;
    char *procs = NULL;
    if (m->cgroup_root) {
        char *uuid = g_uuid_string_random();
        char *name = g_strconcat("app-", uuid, NULL); g_free(uuid);
        instance->cgroup = g_build_filename(m->cgroup_root, name, NULL); g_free(name);
        if (mkdir(instance->cgroup, 0755)) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno), "Cannot create application cgroup: %s", g_strerror(errno));
            g_free(instance->cgroup); g_free(instance); g_strfreev(env); g_ptr_array_unref(argv); return FALSE;
        }
        procs = g_build_filename(instance->cgroup, "cgroup.procs", NULL);
    }
    Setup setup = {m, getpid(), procs};
    appd_state(m, app, "Starting", "");
    gboolean okay = g_spawn_async("/", (char **)argv->pdata, env, G_SPAWN_DO_NOT_REAP_CHILD,
        child_setup, &setup, &instance->pid, error);
    g_free(procs); g_strfreev(env); g_ptr_array_unref(argv);
    if (!okay) {
        kill_cgroup(instance->cgroup); g_free(instance->cgroup); g_free(instance);
        appd_state(m, app, "Stopped", error && *error ? (*error)->message : "Launch failed"); return FALSE;
    }
    instance->start_time = start_time(instance->pid);
    g_ptr_array_add(m->instances, instance); journal(instance, FALSE);
    appd_state(m, app, "Running", "");
    appd_emit(m, "ApplicationStarted", g_variant_new("(su)", app->id, instance->pid));
    return TRUE;
}

void appd_terminate(Manager *m, App *app)
{
    for (guint i = 0; i < m->instances->len; ++i) {
        Instance *instance = g_ptr_array_index(m->instances, i);
        if (instance->app != app || instance->stopping) continue;
        instance->stopping = TRUE;
        instance->deadline = g_timeout_add(m->shutdown_ms, force_exit, instance);
        command(instance, NULL, TRUE);
    }
    appd_state(m, app, appd_instance(m, app) ? "Stopping" : "Stopped", "");
}
void appd_stop_all(Manager *m)
{
    GHashTableIter iter; gpointer value;
    g_hash_table_iter_init(&iter, m->apps);
    while (g_hash_table_iter_next(&iter, NULL, &value)) appd_terminate(m, value);
}
void appd_renderer_lost(Manager *m)
{
    m->ready = FALSE; m->renderer_pid = 0; g_clear_pointer(&m->renderer, g_free);
    g_hash_table_remove_all(m->pending);
    /* A restarted compositor cannot adopt old Wayland connections. */
    appd_stop_all(m);
}
void appd_recover(Manager *m)
{
    struct statfs fs;
    if (!statfs("/sys/fs/cgroup", &fs) && (unsigned long)fs.f_type == CGROUP2_SUPER_MAGIC) {
        m->cgroup_root = g_strdup("/sys/fs/cgroup/openwave-apps");
        if (g_mkdir_with_parents(m->cgroup_root, 0755)) g_clear_pointer(&m->cgroup_root, g_free);
        if (m->cgroup_root) {
            GDir *groups = g_dir_open(m->cgroup_root, 0, NULL);
            const char *name;
            while (groups && (name = g_dir_read_name(groups))) {
                if (!g_str_has_prefix(name, "app-")) continue;
                char *path = g_build_filename(m->cgroup_root, name, NULL);
                kill_cgroup(path); g_free(path);
            }
            if (groups) g_dir_close(groups);
        }
    }
    if (!m->cgroup_root) g_warning("appd: cgroup v2 unavailable; process-group cleanup cannot contain helpers that escape their group");
    g_mkdir_with_parents(m->state_dir, 0700);
    GDir *dir = g_dir_open(m->state_dir, 0, NULL);
    if (!dir) return;
    const char *name;
    while ((name = g_dir_read_name(dir))) {
        if (!g_str_has_suffix(name, ".ini")) continue;
        char *path = g_build_filename(m->state_dir, name, NULL);
        GKeyFile *file = g_key_file_new();
        if (g_key_file_load_from_file(file, path, 0, NULL)) {
            GPid pid = g_key_file_get_uint64(file, "Process", "Pid", NULL);
            guint64 time = g_key_file_get_uint64(file, "Process", "StartTime", NULL);
            char *exe = g_key_file_get_string(file, "Process", "Executable", NULL);
            char *proc = g_strdup_printf("/proc/%d/exe", pid);
            char *actual = g_file_read_link(proc, NULL); g_free(proc);
            if (pid > 1 && time && time == start_time(pid) && !g_strcmp0(exe, actual)
                && getpgid(pid) == pid) kill(-pid, SIGKILL);
            g_free(exe); g_free(actual);
        }
        unlink(path); g_free(path); g_key_file_unref(file);
    }
    g_dir_close(dir);
}
