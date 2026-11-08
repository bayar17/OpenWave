#include "appd.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdlib>
#include <cstdio>
extern "C" Instance *appd_instance(Manager *, App *) { return nullptr; }
extern "C" void appd_emit(Manager *, const char *, GVariant *) {}
extern "C" void appd_services_event(Manager *, const char *, App *, Instance *) {}
static void check(bool okay, const char *message)
{ if (!okay) { std::fprintf(stderr, "%s\n", message); std::exit(1); } }
int main()
{
    Manager manager{};
    appd_registry_scan(&manager);
    check(g_hash_table_size(manager.apps) == 2, "only the two valid, unique applications may be discovered");
    check(g_hash_table_contains(manager.apps,"org.example.system"), "System application root must be scanned");
    check(g_hash_table_contains(manager.apps,"org.example.user"), "user application root must be scanned");
    check(!g_hash_table_contains(manager.apps,"org.example.duplicate"), "duplicate ID must fail closed");
    check(!g_hash_table_contains(manager.apps,"com.openwave.shell"), "compositor must not be advertised");
    QFile shell("/System/Applications/com.openwave.shell/manifest.json");
    check(shell.open(QIODevice::ReadOnly), "read shell fixture");
    auto object=QJsonDocument::fromJson(shell.readAll()).object(); shell.close(); object.remove("role");
    check(shell.open(QIODevice::WriteOnly|QIODevice::Truncate), "update shell fixture");
    shell.write(QJsonDocument(object).toJson()); shell.close();
    check(!appd_read_bundle("/System/Applications/com.openwave.shell"), "omitting the role must not expose the compositor");
    g_hash_table_unref(manager.apps);
    std::puts("PASS production manifest validation and discovery: both roots, invalid manifests, duplicate ID, compositor role omitted");
}
