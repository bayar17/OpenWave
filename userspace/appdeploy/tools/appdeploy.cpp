#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char *kBusName = "org.openwave.Applications";
constexpr const char *kPath = "/org/openwave/Applications";
constexpr const char *kInterface = "org.openwave.AppDeploy1";

struct Context {
    GMainLoop *loop = nullptr;
    GVariant *reply = nullptr;
    GError *error = nullptr;
    std::string client;
    bool quiet = false;
};

void usage()
{
    std::fputs(
        "usage: appdeploy [options] COMMAND\n"
        "  install FILE.rpm...     install or update from local package files\n"
        "  remove PACKAGE...       remove packages (application data is kept)\n"
        "  info PACKAGE            describe an installed package\n"
        "  list                    list installed packages\n"
        "  verify PACKAGE          check installed files against the package database\n"
        "  reconcile               repair the application registry after an interrupted transaction\n"
        "options:\n"
        "  --system            authorize system packages (administrator only)\n"
        "  --purge-data        with remove: also delete the application's user data\n"
        "  -y, --yes           do not ask for confirmation (administrator only)\n"
        "  -n, --plan          resolve and verify, change nothing\n"
        "  --allow-downgrade   permit a transaction that downgrades a package\n",
        stderr);
}

std::string field(const std::vector<std::string> &fields, std::size_t index)
{
    return index < fields.size() ? fields[index] : std::string();
}

std::vector<std::string> split_tabs(const std::string &line)
{
    std::vector<std::string> fields;
    std::size_t begin = 0;
    for (;;) {
        const auto end = line.find('\t', begin);
        fields.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
        if (end == std::string::npos) return fields;
        begin = end + 1;
    }
}

void print_line(const std::string &line, bool progress)
{
    const auto fields = split_tabs(line);
    const std::string tag = field(fields, 0);
    if (tag == "P") {
        const std::string kind = field(fields, 1);
        if (kind == "info") return;
        const unsigned long long current = std::strtoull(field(fields, 2).c_str(), nullptr, 10);
        const unsigned long long total = std::strtoull(field(fields, 3).c_str(), nullptr, 10);
        (void)current; (void)total;
        if (kind == "warning") std::fprintf(stderr, "warning: %s\n", field(fields, 4).c_str());
        else std::printf("  %s\n", field(fields, 4).c_str());
    } else if (tag == "R") {
        if (!progress) std::printf("%s\n", field(fields, 1).c_str());
    } else if (tag == "E") {
        std::fprintf(stderr, "error: %s\n", field(fields, 1).c_str());
    } else if (tag == "ROW") {
        std::printf("%-44s %-12s %-12s %s\n", field(fields, 1).c_str(),
                    (field(fields, 2) + "-" + field(fields, 3)).c_str(), field(fields, 5).c_str(), field(fields, 6).c_str());
    } else if (tag == "I") {
        std::printf("%s\n", field(fields, 1).c_str());
    }
}

void on_signal(GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GVariant *parameters,
               gpointer data)
{
    auto *context = static_cast<Context *>(data);
    const char *client = nullptr, *line = nullptr;
    g_variant_get(parameters, "(&s&s)", &client, &line);
    if (context->client == client) print_line(line, true);
}

void on_reply(GObject *source, GAsyncResult *result, gpointer data)
{
    auto *context = static_cast<Context *>(data);
    context->reply = g_dbus_connection_call_with_unix_fd_list_finish(G_DBUS_CONNECTION(source), nullptr, result, &context->error);
    g_main_loop_quit(context->loop);
}

int run(const std::string &operation, const std::vector<std::string> &arguments, bool system, bool purge, bool plan,
        bool downgrade, bool yes, const std::vector<int> &files)
{
    GError *error = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!bus) {
        std::fprintf(stderr, "appdeploy: cannot reach the system bus: %s\n", error->message);
        return 1;
    }
    Context context;
    context.loop = g_main_loop_new(nullptr, FALSE);
    char token[32];
    std::snprintf(token, sizeof(token), "c%u-%u", unsigned(getpid()), unsigned(g_random_int()));
    context.client = token;
    const guint subscription = g_dbus_connection_signal_subscribe(bus, kBusName, kInterface, "Progress", kPath, nullptr,
                                                                  G_DBUS_SIGNAL_FLAGS_NONE, on_signal, &context, nullptr);
    GVariantBuilder strings, options;
    g_variant_builder_init(&strings, G_VARIANT_TYPE("as"));
    for (const auto &argument : arguments) g_variant_builder_add(&strings, "s", argument.c_str());
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&options, "{sv}", "client", g_variant_new_string(token));
    g_variant_builder_add(&options, "{sv}", "system", g_variant_new_boolean(system));
    g_variant_builder_add(&options, "{sv}", "purge-data", g_variant_new_boolean(purge));
    g_variant_builder_add(&options, "{sv}", "plan", g_variant_new_boolean(plan));
    g_variant_builder_add(&options, "{sv}", "allow-downgrade", g_variant_new_boolean(downgrade));
    g_variant_builder_add(&options, "{sv}", "yes", g_variant_new_boolean(yes));
    GUnixFDList *list = g_unix_fd_list_new();
    GVariantBuilder indexes;
    g_variant_builder_init(&indexes, G_VARIANT_TYPE("ah"));
    for (const int descriptor : files) {
        const int index = g_unix_fd_list_append(list, descriptor, &error);
        if (index < 0) { std::fprintf(stderr, "appdeploy: %s\n", error->message); return 1; }
        g_variant_builder_add(&indexes, "h", index);
    }
    GVariant *parameters = g_variant_new("(s@as@a{sv}@ah)", operation.c_str(), g_variant_builder_end(&strings),
                                         g_variant_builder_end(&options), g_variant_builder_end(&indexes));
    g_dbus_connection_call_with_unix_fd_list(bus, kBusName, kPath, kInterface, "Run", parameters, G_VARIANT_TYPE("(ias)"),
                                             G_DBUS_CALL_FLAGS_NONE, 3600 * 1000, list, nullptr, on_reply, &context);
    g_main_loop_run(context.loop);
    g_dbus_connection_signal_unsubscribe(bus, subscription);
    if (!context.reply) {
        std::fprintf(stderr, "appdeploy: %s\n", context.error ? context.error->message : "the request failed");
        return 1;
    }
    gint32 status = 1;
    GVariant *lines = nullptr;
    g_variant_get(context.reply, "(i@as)", &status, &lines);
    GVariantIter iterator;
    g_variant_iter_init(&iterator, lines);
    const char *line;
    while (g_variant_iter_next(&iterator, "&s", &line)) print_line(line, false);
    return status == 0 ? 0 : 1;
}

int snapshot(const std::string &path)
{
    const int source = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    struct stat st;
    if (source < 0 || fstat(source, &st) || !S_ISREG(st.st_mode)) {
        std::fprintf(stderr, "appdeploy: cannot read %s as a package file\n", path.c_str());
        if (source >= 0) close(source);
        return -1;
    }
    if (st.st_size > (512LL << 20)) {
        std::fprintf(stderr, "appdeploy: %s is too large to be a package\n", path.c_str());
        close(source);
        return -1;
    }
    const int memory = memfd_create("appdeploy-package", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (memory < 0) { std::perror("appdeploy: memfd_create"); close(source); return -1; }
    char buffer[65536];
    long long total = 0;
    for (;;) {
        const ssize_t count = read(source, buffer, sizeof(buffer));
        if (count < 0) { std::fprintf(stderr, "appdeploy: cannot read %s\n", path.c_str()); close(source); close(memory); return -1; }
        if (count == 0) break;
        total += count;
        if (total > (512LL << 20) || write(memory, buffer, size_t(count)) != count) {
            std::fprintf(stderr, "appdeploy: cannot copy %s\n", path.c_str());
            close(source); close(memory);
            return -1;
        }
    }
    close(source);
    fcntl(memory, F_ADD_SEALS, F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE);
    char self[64];
    std::snprintf(self, sizeof(self), "/proc/self/fd/%d", memory);
    const int readonly = open(self, O_RDONLY | O_CLOEXEC);
    close(memory);
    if (readonly < 0) std::fprintf(stderr, "appdeploy: cannot reopen the package copy read-only\n");
    return readonly;
}

}

int main(int argc, char **argv)
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    bool system = false, purge = false, plan = false, downgrade = false, yes = false;
    std::vector<std::string> words;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--system") system = true;
        else if (argument == "--purge-data") purge = true;
        else if (argument == "-n" || argument == "--plan") plan = true;
        else if (argument == "-y" || argument == "--yes") yes = true;
        else if (argument == "--allow-downgrade") downgrade = true;
        else if (argument == "-h" || argument == "--help") { usage(); return 0; }
        else if (argument.rfind("-", 0) == 0 && argument != "-") { std::fprintf(stderr, "appdeploy: unknown option %s\n", argument.c_str()); return 2; }
        else words.push_back(argument);
    }
    if (words.empty()) { usage(); return 2; }
    const std::string command = words[0];
    std::vector<std::string> rest(words.begin() + 1, words.end());
    auto need = [&](std::size_t count, std::size_t maximum = 0) {
        if (rest.size() < count || (maximum && rest.size() > maximum)) { usage(); std::exit(2); }
    };

    if (command == "install") {
        need(1, 16);
        std::vector<int> files;
        std::vector<std::string> names;
        for (const auto &file : rest) {
            const int fd = snapshot(file);
            if (fd < 0) return 1;
            files.push_back(fd);
            names.push_back(file.substr(file.rfind('/') == std::string::npos ? 0 : file.rfind('/') + 1));
        }
        return run("install", names, system, false, plan, downgrade, yes, files);
    }
    if (command == "remove") { need(1); return run("remove", rest, system, purge, plan, false, yes, {}); }
    if (command == "info") { need(1, 1); return run("info", rest, false, false, false, false, false, {}); }
    if (command == "list") { need(0, 0); return run("list", {}, false, false, false, false, false, {}); }
    if (command == "verify") { need(1, 1); return run("verify", rest, false, false, false, false, false, {}); }
    if (command == "reconcile") { need(0, 0); return run("reconcile", {}, false, false, false, false, false, {}); }
    usage();
    return 2;
}
