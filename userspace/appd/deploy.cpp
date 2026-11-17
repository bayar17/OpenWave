#include "appd.h"
#include <OpenWave/OpenWaveXml.h>
#include <OpenWave/OpenWaveIdentity.h>
#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr auto interface = "org.openwave.AppDeploy1";
constexpr auto helper = "/usr/libexec/openwave-appdeploy/appdeploy-helper";
constexpr auto cli = "/usr/bin/appdeploy";
constexpr gint64 timeoutMs = 60LL * 60 * 1000;
constexpr std::size_t maxLines = 4000, maxLine = 4096;
constexpr int maxFiles = 16;
constexpr guint confirmationTimeoutMs = 180000, unsignedDelayMs = 3000;

struct Verb { const char *name; bool mutating; int minimumOperands; int maximumOperands; };
const Verb verbs[] = {
    {"install-android", true, 1, 1}, {"install", true, 1, maxFiles}, {"remove", true, 1, 32}, {"reconcile", true, 0, 0},
    {"info", false, 1, 1}, {"list", false, 0, 0}, {"verify", false, 1, 1},
};

struct Job {
    Manager *manager = nullptr;
    GDBusMethodInvocation *call = nullptr;
    std::string sender, client;
    bool purge = false;
    GPid pid = 0;
    int out = -1, in = -1;
    guint reader = 0, watch = 0, timeout = 0, poll = 0;
    std::string pending;
    std::vector<std::string> lines;
    bool exited = false;
    int status = 1;
    bool eof = false;
    bool asking = false;
    std::string promptOwner, promptId;
    bool polling = false;
    bool plain = false;
    std::string removeId;
    int refs = 0;
    bool closed = false;
};
Job *current = nullptr;

void release(Job *job)
{
    if (--job->refs == 0 && job->closed) delete job;
}

void failure(GDBusMethodInvocation *call, const char *message)
{
    g_warning("appd: appdeploy request rejected: %s", message);
    g_dbus_method_invocation_return_dbus_error(call, "org.openwave.Applications.Error", message);
}

bool validOperand(const std::string &value)
{
    if (value.empty() || value.size() > 512 || value[0] == '-') return false;
    for (unsigned char c : value)
        if (c < 0x21 || c > 0x7e || c == '`' || c == '$' || c == '\\' || c == '"' || c == '\'' || c == ';' || c == '|' || c == '&') return false;
    return true;
}

std::string display(const std::string &value, std::size_t limit)
{
    char *valid = g_utf8_make_valid(value.c_str(), -1);
    std::string out;
    std::size_t characters = 0;
    for (const char *p = valid; *p; p = g_utf8_next_char(p)) {
        const gunichar c = g_utf8_get_char(p);
        if (c < 0x20 || c == 0x7f || (c >= 0x80 && c < 0xa0) || (c >= 0x200b && c <= 0x200f)
            || (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069) || c == 0xfeff) continue;
        if (characters++ >= limit) { out += "…"; break; }
        char buffer[8];
        out.append(buffer, std::size_t(g_unichar_to_utf8(c, buffer)));
    }
    g_free(valid);
    return out;
}

std::vector<std::string> splitOn(const std::string &line, char separator)
{
    std::vector<std::string> fields;
    std::size_t begin = 0;
    for (;;) {
        const auto end = line.find(separator, begin);
        fields.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) return fields;
        begin = end + 1;
    }
}

void emitLine(Job *job, const std::string &line);

void note(Job *job, const char *kind, const std::string &text)
{
    emitLine(job, std::string(kind) + "\t" + text);
}

GDBusConnection *sessionBus(Manager *manager)
{
    static GDBusConnection *connection = nullptr;
    if (connection && !g_dbus_connection_is_closed(connection)) return connection;
    if (connection) { g_object_unref(connection); connection = nullptr; }
    char *address = g_strdup_printf("unix:path=%s/bus", manager->runtime);
    GError *error = nullptr;
    connection = g_dbus_connection_new_for_address_sync(address, GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT
        | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION), nullptr, nullptr, &error);
    g_free(address);
    if (!connection) { g_warning("appd: no session bus for the confirmation: %s", error ? error->message : "unknown"); g_clear_error(&error); }
    return connection;
}

void answer(Job *job, bool yes, const std::string &why)
{
    if (!job->asking) return;
    job->asking = false;
    if (job->poll) { g_source_remove(job->poll); job->poll = 0; }
    if (!yes && !why.empty()) note(job, "E", why);
    if (job->in >= 0) {
        const char *reply = yes ? "Y\n" : "N\n";
        if (write(job->in, reply, 2) < 0) g_warning("appd: cannot answer the appdeploy helper: %s", strerror(errno));
    }
}

void closePrompt(Job *job, bool finished)
{
    if (job->promptId.empty()) return;
    if (GDBusConnection *bus = sessionBus(job->manager))
        g_dbus_connection_call(bus, job->promptOwner.c_str(), "/org/openwave/Prompt", "org.openwave.Prompt1",
                               finished ? "Acknowledge" : "Cancel", g_variant_new("(s)", job->promptId.c_str()), nullptr,
                               G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr, nullptr);
    job->promptId.clear();
}

gboolean pollPrompt(gpointer data);

void promptResult(GObject *source, GAsyncResult *result, gpointer data)
{
    auto *job = static_cast<Job *>(data);
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    job->polling = false;
    if (job->closed || current != job || !job->asking) {
        if (reply) g_variant_unref(reply);
        g_clear_error(&error);
        release(job);
        return;
    }
    if (!reply) {
        g_warning("appd: the confirmation prompt failed: %s", error ? error->message : "unknown");
        g_clear_error(&error);
        job->promptId.clear();
        answer(job, false, "the confirmation prompt was lost");
        release(job);
        return;
    }
    gboolean finished; const char *state, *text;
    g_variant_get(reply, "(b&s&s)", &finished, &state, &text);
    if (finished) {
        const bool accepted = !strcmp(state, "accepted");
        const std::string why = accepted ? "" : !strcmp(state, "cancelled") ? "cancelled by the person"
            : !strcmp(state, "timed_out") ? "nobody answered the confirmation in time" : std::string("the confirmation ended: ") + state;
        closePrompt(job, true);
        answer(job, accepted, why);
    }
    g_variant_unref(reply);
    release(job);
}

gboolean pollPrompt(gpointer data)
{
    auto *job = static_cast<Job *>(data);
    if (current != job || !job->asking || job->promptId.empty()) { job->poll = 0; return G_SOURCE_REMOVE; }
    GDBusConnection *bus = sessionBus(job->manager);
    if (!bus) { job->poll = 0; answer(job, false, "the OpenWave session is gone"); return G_SOURCE_REMOVE; }
    if (!job->polling) {
        job->polling = true;
        ++job->refs;
        g_dbus_connection_call(bus, job->promptOwner.c_str(), "/org/openwave/Prompt", "org.openwave.Prompt1", "GetResult",
                               g_variant_new("(s)", job->promptId.c_str()), G_VARIANT_TYPE("(bss)"), G_DBUS_CALL_FLAGS_NONE, 2000,
                               nullptr, promptResult, job);
    }
    return G_SOURCE_CONTINUE;
}

void promptStarted(GObject *source, GAsyncResult *result, gpointer data)
{
    auto *job = static_cast<Job *>(data);
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
    if (job->closed || current != job || !job->asking) {
        if (reply) {
            const char *id; g_variant_get(reply, "(&s)", &id);
            g_dbus_connection_call(G_DBUS_CONNECTION(source), job->promptOwner.c_str(), "/org/openwave/Prompt", "org.openwave.Prompt1",
                                   "Cancel", g_variant_new("(s)", id), nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr, nullptr);
            g_variant_unref(reply);
        }
        g_clear_error(&error);
        release(job);
        return;
    }
    if (!reply) {
        const std::string detail = error ? error->message : "unknown";
        g_clear_error(&error);
        answer(job, false, "no OpenWave session could be asked to confirm (" + detail + "); an administrator can pass --yes");
        release(job);
        return;
    }
    const char *id; g_variant_get(reply, "(&s)", &id);
    job->promptId = id;
    g_variant_unref(reply);
    job->poll = g_timeout_add(400, pollPrompt, job);
    release(job);
}

void askPerson(Job *job, const std::string &line)
{
    const auto f = splitOn(line, '\t');
    if (f.size() < 10 || job->asking) { if (job->in >= 0) { job->asking = true; answer(job, false, "the helper asked an invalid question"); } return; }
    const std::string action = f[1];
    const bool remove = action == "remove", update = action == "update", firstUse = f[7] == "publisher", untrusted = f[7] == "untrusted" || firstUse;
    const std::string name = display(f[2], 64), version = display(f[3], 40), previous = display(f[4], 40), package = display(f[5], 96);
    std::string title = std::string("Are you sure you want to ") + (remove ? "remove " : update ? "update " : "install ") + name + "?";
    std::string message;
    if (update && !previous.empty()) message += "Version " + previous + " will be replaced by version " + version + ".\n";
    else if (!version.empty()) message += "Version " + version + "\n";
    if (!package.empty() && package != name) message += "Package: " + package + "\n";
    if (f[6] == "system") message += "This is a system package.\n";
    if (remove) message += job->purge ? "Its data will also be deleted.\n" : "Its data is kept.\n";
    else if (!f[9].empty()) {
        std::string permissions;
        std::size_t count = 0;
        for (const auto &item : splitOn(f[9], ',')) { if (count++ >= 12) { permissions += ", …"; break; } permissions += (permissions.empty() ? "" : ", ") + display(item, 48); }
        message += "It asks for: " + permissions + ".\n";
    }
    if (firstUse && !remove)
        message += "\nWarning: this is the " + display(f[8], 160) + ". "
                   "Install it only if you got it from a source you trust; later versions signed with the same key will not ask again.";
    else if (untrusted && !remove)
        message += "\nWarning: this package is not from a trusted publisher (" + display(f[8], 120) + "). "
                   "Install it only if you got it from a source you trust.";
    if (message.size() > 1900) message.resize(1900);

    GDBusConnection *bus = sessionBus(job->manager);
    char *owner = bus ? openwave_service_owner(bus, "org.openwave.Prompt", "/run/openwave/promptd.pid", nullptr) : nullptr;
    job->asking = true;
    if (!owner) {
        answer(job, false, "no OpenWave session could be asked to confirm; an administrator can pass --yes");
        return;
    }
    job->promptOwner = owner;
    g_free(owner);
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&options, "{sv}", "title", g_variant_new_string(title.c_str()));
    g_variant_builder_add(&options, "{sv}", "message", g_variant_new_string(message.c_str()));
    g_variant_builder_add(&options, "{sv}", "acceptLabel", g_variant_new_string(remove ? "Remove" : update ? "Update" : "Install"));
    g_variant_builder_add(&options, "{sv}", "cancelLabel", g_variant_new_string("Cancel"));
    g_variant_builder_add(&options, "{sv}", "timeoutMs", g_variant_new_uint32(confirmationTimeoutMs));
    if (untrusted && !remove) g_variant_builder_add(&options, "{sv}", "acceptDelayMs", g_variant_new_uint32(unsignedDelayMs));
    note(job, "P", "ask\t0\t0\tWaiting for you to confirm " + name + " in the OpenWave session");
    ++job->refs;
    g_dbus_connection_call(bus, job->promptOwner.c_str(), "/org/openwave/Prompt", "org.openwave.Prompt1", "Request",
                           g_variant_new("(s@a{sv})", "confirmation", g_variant_builder_end(&options)), G_VARIANT_TYPE("(s)"),
                           G_DBUS_CALL_FLAGS_NONE, 5000, nullptr, promptStarted, job);
}

void finish(Job *job)
{
    if (!job->exited || !job->eof) return;
    if (!job->pending.empty()) { job->lines.push_back(job->pending); job->pending.clear(); }
    if (job->plain) {
        if (job->status == 0) g_dbus_method_invocation_return_value(job->call, nullptr);
        else {
            std::string message = "The application could not be uninstalled.";
            for (const auto &line : job->lines)
                if (line.rfind("E\t", 0) == 0) { message = display(line.substr(2), 200); break; }
            g_dbus_method_invocation_return_dbus_error(job->call, "org.openwave.Applications.Error", message.c_str());
        }
        g_object_unref(job->call);
        if (App *app = static_cast<App *>(g_hash_table_lookup(job->manager->apps, job->removeId.c_str()))) app->removing = FALSE;
    } else {
        GVariantBuilder result;
        g_variant_builder_init(&result, G_VARIANT_TYPE("as"));
        for (const auto &line : job->lines)
            if (line.rfind("P\t", 0) != 0) g_variant_builder_add(&result, "s", line.c_str());
        g_dbus_method_invocation_return_value(job->call, g_variant_new("(i@as)", job->status, g_variant_builder_end(&result)));
        g_object_unref(job->call);
    }
    if (job->reader) g_source_remove(job->reader);
    if (job->timeout) g_source_remove(job->timeout);
    if (job->poll) g_source_remove(job->poll);
    job->asking = false;
    closePrompt(job, false);
    if (job->out >= 0) close(job->out);
    if (job->in >= 0) close(job->in);
    g_spawn_close_pid(job->pid);
    if (job->status == 0) appd_registry_scan(job->manager);
    if (current == job) current = nullptr;
    job->closed = true;
    if (job->refs == 0) delete job;
}

void emitLine(Job *job, const std::string &line)
{
    if (line.rfind("Q\t", 0) == 0) { askPerson(job, line); return; }
    if (job->lines.size() < maxLines) job->lines.push_back(line.substr(0, maxLine));
    if (line.rfind("P\t", 0) == 0 && !job->client.empty())
        g_dbus_connection_emit_signal(job->manager->bus, job->sender.c_str(), APPD_PATH, interface, "Progress",
                                      g_variant_new("(ss)", job->client.c_str(), line.substr(0, maxLine).c_str()), nullptr);
}

bool drainOutput(Job *job)
{
    char buffer[4096];
    for (;;) {
        const auto count = read(job->out, buffer, sizeof(buffer));
        if (count > 0) {
            job->pending.append(buffer, std::size_t(count));
            std::size_t newline;
            while ((newline = job->pending.find('\n')) != std::string::npos) {
                emitLine(job, job->pending.substr(0, newline));
                job->pending.erase(0, newline + 1);
            }
            if (job->pending.size() > maxLine * 4) job->pending.clear();
            continue;
        }
        if (count < 0 && (errno == EAGAIN || errno == EINTR)) return false;
        return true;
    }
}

gboolean readOutput(int, GIOCondition, gpointer data)
{
    auto *job = static_cast<Job *>(data);
    if (!drainOutput(job)) return G_SOURCE_CONTINUE;
    job->eof = true;
    job->reader = 0;
    finish(job);
    return G_SOURCE_REMOVE;
}

void childExited(Job *job, int status)
{
    job->watch = 0;
    job->exited = true;
    job->status = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    if (!job->eof) {
        drainOutput(job);
        job->eof = true;
        if (job->reader) { g_source_remove(job->reader); job->reader = 0; }
    }
    finish(job);
}

gboolean jobTimeout(gpointer data)
{
    auto *job = static_cast<Job *>(data);
    job->timeout = 0;
    g_warning("appd: appdeploy helper timed out");
    if (job->pid > 0 && !job->exited) kill(job->pid, SIGKILL);
    return G_SOURCE_REMOVE;
}

extern "C" void appd_deploy_peer_gone(Manager *, const char *name)
{
    if (!current || current->closed || current->sender != name) return;
    current->asking = true;
    answer(current, false, "");
    closePrompt(current, false);
    if (current->pid > 0 && !current->exited) kill(current->pid, SIGTERM);
}

extern "C" gboolean appd_deploy_child_exited(Manager *, GPid pid, int status)
{
    if (!current || current->pid != pid || current->exited) return FALSE;
    childExited(current, status);
    return TRUE;
}

struct ChildData { int files[maxFiles]; int count; };
void childSetup(gpointer data)
{
    auto *child = static_cast<ChildData *>(data);
    sigset_t none;
    sigemptyset(&none);
    sigprocmask(SIG_SETMASK, &none, nullptr);
    int moved[maxFiles];
    for (int i = 0; i < child->count; ++i) moved[i] = fcntl(child->files[i], F_DUPFD, 100 + maxFiles);
    for (int i = 0; i < child->count; ++i) { dup2(moved[i], 3 + i); close(moved[i]); }
    for (int fd = 3 + child->count; fd < 1024; ++fd) close(fd);
    umask(0077);
}

std::string safeName(const std::string &value)
{
    std::string out;
    for (unsigned char c : value) out += (c < 0x21 || c > 0x7e || c == '`' || c == '$' || c == '\\' || c == '"' || c == '\'' || c == ';' || c == '|' || c == '&') ? '_' : char(c);
    if (out.size() > 120) out.resize(120);
    if (out.empty() || out[0] == '-') out.insert(0, "_");
    return out;
}

void run(Manager *m, const char *sender, GVariant *parameters, GDBusMethodInvocation *call)
{
    guint uid = 0, pid = 0;
    if (!appd_credentials(m, sender, &uid, &pid)) { failure(call, "Caller credentials are required"); return; }
    char *path = g_strdup_printf("/proc/%u/exe", pid);
    char *exe = g_file_read_link(path, nullptr); g_free(path);
    const bool command = exe && !strcmp(exe, cli) && appd_verified_file(cli, TRUE, nullptr);
    g_free(exe);
    Instance *opener = command ? nullptr : appd_caller(m, uid, pid);
    const bool files = opener && !strcmp(opener->app->id, "com.openwave.files") && appd_trusted_app(opener->app)
        && appd_validate_bundle(opener->app, nullptr);
    if (!command && !files) { failure(call, "Only the installed appdeploy command may submit package requests"); return; }
    const char *operation; GVariant *argumentValues, *optionValues, *fileValues;
    g_variant_get(parameters, "(&s@as@a{sv}@ah)", &operation, &argumentValues, &optionValues, &fileValues);
    const Verb *verb = nullptr;
    for (const auto &candidate : verbs) if (!strcmp(candidate.name, operation)) verb = &candidate;
    if (!verb) { failure(call, "Unknown operation"); return; }
    const bool android = !strcmp(operation, "install-android");
    if (android && !files) { failure(call, "Android installation requires verified Files and an interactive confirmation"); return; }
    if (files && ((!android && strcmp(operation, "install")) || uid != m->uid)) {
        failure(call, "Files may only ask to install a package for the session user"); return;
    }
    if (!files && (verb->mutating ? uid != 0 : (uid != 0 && uid != m->uid))) {
        failure(call, verb->mutating ? "Only an administrator may change packages"
                                     : "Only an administrator or the session user may query packages");
        return;
    }
    const bool install = android || !strcmp(operation, "install");
    std::vector<std::string> operands;
    GVariantIter iterator; const char *argument;
    g_variant_iter_init(&iterator, argumentValues);
    while (g_variant_iter_next(&iterator, "&s", &argument)) {
        std::string value = install ? safeName(argument) : std::string(argument);
        if (!install && !validOperand(value)) { failure(call, "An argument is not acceptable"); return; }
        operands.push_back(value);
    }
    if (int(operands.size()) < verb->minimumOperands || int(operands.size()) > verb->maximumOperands) {
        failure(call, "Wrong number of arguments"); return;
    }
    const gsize fileCount = g_variant_n_children(fileValues);
    if (fileCount != (install ? operands.size() : 0)) { failure(call, "The number of package files does not match the request"); return; }
    auto flag = [&](const char *name) { gboolean value = FALSE; g_variant_lookup(optionValues, name, "b", &value); return value != FALSE; };
    const char *clientText = ""; g_variant_lookup(optionValues, "client", "&s", &clientText);
    std::string client = clientText;
    if (client.size() > 64) { failure(call, "Invalid client identifier"); return; }
    const bool system = flag("system"), purge = flag("purge-data"), yes = flag("yes");
    if (!verb->mutating && (system || purge || yes)) { failure(call, "Options do not apply to this operation"); return; }
    if (files && (system || purge || yes || flag("plan") || flag("allow-downgrade") || operands.size() != 1)) {
        failure(call, "Files may only ask to install one package, with the person's confirmation"); return;
    }
    if (purge && strcmp(operation, "remove")) { failure(call, "--purge-data applies only to remove"); return; }
    if (current) { failure(call, "Another package transaction is in progress"); return; }

    ChildData child{};
    auto closeFiles = [&]() { for (int i = 0; i < child.count; ++i) close(child.files[i]); child.count = 0; };
    if (install) {
        GDBusMessage *message = g_dbus_method_invocation_get_message(call);
        GUnixFDList *list = g_dbus_message_get_unix_fd_list(message);
        for (gsize i = 0; i < fileCount; ++i) {
            gint32 index = 0;
            g_variant_get_child(fileValues, i, "h", &index);
            GError *error = nullptr;
            const int descriptor = list ? g_unix_fd_list_get(list, index, &error) : -1;
            g_clear_error(&error);
            struct stat st;
            if (descriptor < 0) { closeFiles(); failure(call, "A package file descriptor is missing"); return; }
            child.files[child.count++] = descriptor;
            if (fstat(descriptor, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > (1LL << 30)) { closeFiles(); failure(call, "A regular package file is required"); return; }
            const int flags = fcntl(descriptor, F_GETFL);
            if (flags < 0 || (flags & O_ACCMODE) != O_RDONLY) { closeFiles(); failure(call, "A package must be opened read-only"); return; }
        }
    }
    const char *selectedHelper = android ? "/usr/libexec/openwave-waydroid/android-install" : helper;
    if (!appd_verified_file(selectedHelper, TRUE, nullptr)
        || (android && !appd_verified_file("/usr/libexec/openwave-waydroid/android-apps", TRUE, nullptr))) {
        closeFiles(); failure(call, "The verified package installer is unavailable"); return;
    }
    std::vector<std::string> storage{selectedHelper, install ? "install-file" : operation};
    if (system) storage.push_back("--system");
    if (purge) storage.push_back("--purge-data");
    if (flag("plan")) storage.push_back("--plan");
    if (flag("allow-downgrade")) storage.push_back("--allow-downgrade");
    if (yes) storage.push_back("--yes");
    for (const auto &operand : operands) storage.push_back(operand);
    std::vector<char *> argv;
    for (auto &item : storage) argv.push_back(item.data());
    argv.push_back(nullptr);
    char *envp[] = {const_cast<char *>("PATH=/usr/bin:/bin"), const_cast<char *>("LANG=C.UTF-8"), nullptr};
    auto *job = new Job;
    job->manager = m; job->sender = sender; job->client = client; job->purge = purge;
    job->call = static_cast<GDBusMethodInvocation *>(g_object_ref(call));
    GError *error = nullptr;
    int out = -1, in = -1;
    if (!g_spawn_async_with_pipes("/", argv.data(), envp, GSpawnFlags(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL),
            childSetup, &child, &job->pid, &in, &out, nullptr, &error)) {
        g_warning("appd: cannot run the appdeploy helper: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        closeFiles();
        g_object_unref(job->call); delete job;
        failure(call, "The package helper could not be started");
        return;
    }
    closeFiles();
    job->out = out; job->in = in;
    g_unix_set_fd_nonblocking(out, TRUE, nullptr);
    job->reader = g_unix_fd_add(out, GIOCondition(G_IO_IN | G_IO_HUP | G_IO_ERR), readOutput, job);
    job->timeout = g_timeout_add(guint(timeoutMs), jobTimeout, job);
    current = job;
    g_message("appd: appdeploy %s started by uid=%u pid=%u%s", operation, uid, pid, yes ? " (confirmation skipped by --yes)" : "");
}

extern "C" void appd_deploy_remove(Manager *m, const char *id, GDBusMethodInvocation *call)
{
    auto fail = [&](const char *message) {
        if (App *app = static_cast<App *>(g_hash_table_lookup(m->apps, id))) app->removing = FALSE;
        g_dbus_method_invocation_return_dbus_error(call, "org.openwave.Applications.Error", message);
    };
    if (!validOperand(id)) { fail("An argument is not acceptable"); return; }
    if (current) { fail("Another package transaction is in progress"); return; }
    std::vector<std::string> storage{helper, "remove", "--yes", "--purge-data", id};
    std::vector<char *> argv;
    for (auto &item : storage) argv.push_back(item.data());
    argv.push_back(nullptr);
    char *envp[] = {const_cast<char *>("PATH=/usr/bin:/bin"), const_cast<char *>("LANG=C.UTF-8"), nullptr};
    auto *job = new Job;
    job->manager = m; job->plain = true; job->removeId = id; job->purge = true;
    job->call = static_cast<GDBusMethodInvocation *>(g_object_ref(call));
    ChildData child{};
    GError *error = nullptr;
    int out = -1, in = -1;
    if (!g_spawn_async_with_pipes("/", argv.data(), envp, GSpawnFlags(G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDERR_TO_DEV_NULL),
            childSetup, &child, &job->pid, &in, &out, nullptr, &error)) {
        g_warning("appd: cannot run the appdeploy helper: %s", error ? error->message : "unknown error");
        g_clear_error(&error);
        g_object_unref(job->call); delete job;
        fail("The package helper could not be started");
        return;
    }
    job->out = out; job->in = in;
    g_unix_set_fd_nonblocking(out, TRUE, nullptr);
    job->reader = g_unix_fd_add(out, GIOCondition(G_IO_IN | G_IO_HUP | G_IO_ERR), readOutput, job);
    job->timeout = g_timeout_add(guint(timeoutMs), jobTimeout, job);
    current = job;
    g_message("appd: appdeploy remove %s started for the session", id);
}

void dispatch(GDBusConnection *, const char *sender, const char *, const char *, const char *member,
              GVariant *parameters, GDBusMethodInvocation *call, void *data)
{
    if (!strcmp(member, "Run")) run(static_cast<Manager *>(data), sender, parameters, call);
    else failure(call, "Unknown method");
}
}

extern "C" gboolean appd_register_deploy(Manager *m, GError **error)
{
    auto *node = openwave_xml_load_dbus_interface(interface, error);
    if (!node) return FALSE;
    static const GDBusInterfaceVTable vtable = {dispatch, nullptr, nullptr, {nullptr}};
    const guint registration = g_dbus_connection_register_object(m->bus, APPD_PATH, node->interfaces[0], &vtable, m, nullptr, error);
    g_dbus_node_info_unref(node);
    return registration != 0;
}
