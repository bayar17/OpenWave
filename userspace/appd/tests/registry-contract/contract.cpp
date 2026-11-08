#include "appd.h"
// Exercise the production wire decoder and the exact predicate used by Ocean.
#include "../../../application-client/ApplicationsClient.cpp"
#include "../../../../OceanLauncher/OceanLauncher/ApplicationMetadata.h"
#include <cstdlib>
#include <cstdio>
extern "C" Instance *appd_instance(Manager *, App *) { return nullptr; }
extern "C" gboolean appd_is_webapp(App *) { return FALSE; }
static void check(bool okay, const char *reason)
{
    if (!okay) { std::fprintf(stderr, "%s\n", reason); std::exit(1); }
}
int main()
{
    Manager manager{};
    char id[] = "com.openwave.settings", name[] = "Settings", empty[] = "", digest[] = "test";
    char bundle[] = "/System/Applications/com.openwave.settings";
    char executable[] = "/System/Applications/com.openwave.settings/bin/ocean-settings";
    char *argv[] = {executable, nullptr}, *permissions[] = {nullptr};
    App app{}; app.id=id; app.name=name; app.icon=empty; app.digest=digest;
    app.bundle=bundle; app.argv=argv; app.permissions=permissions; app.state=empty; app.error=empty;
    GVariant *wire = g_variant_ref_sink(appd_info(&manager, &app, nullptr));
    auto info = decode(wire).toMap();
    check(oceanLaunchableApplication(info), "appd normal record must appear in Ocean");
    check(info.value("trusted").toBool(), "Settings trust metadata preserved");
    check(info.value("appId").toString() == id, "wire decoder must preserve ID");
    g_variant_unref(wire);
    app.removing=TRUE;
    wire=g_variant_ref_sink(appd_info(&manager, &app, nullptr));
    check(!oceanLaunchableApplication(decode(wire).toMap()), "removing application must be hidden");
    g_variant_unref(wire);
    app.removing=FALSE; app.id=const_cast<char *>("com.openwave.shell");
    wire=g_variant_ref_sink(appd_info(&manager, &app, nullptr));
    check(!oceanLaunchableApplication(decode(wire).toMap()), "compositor must never appear");
    g_variant_unref(wire);
    info.remove("launchable");
    check(!oceanLaunchableApplication(info), "missing eligibility must fail closed");
    info.insert("launchable", "true");
    check(!oceanLaunchableApplication(info), "string eligibility must fail closed");
    info.insert("launchable", false);
    check(!oceanLaunchableApplication(info), "component eligibility must remain filtered");
    info.insert("launchable", true); info.insert("appId", "com.openwave.shell");
    check(!oceanLaunchableApplication(info), "malformed shell record must remain filtered");
    std::puts("PASS appd serialization -> ApplicationsClient decoder -> Ocean eligibility (7 checks)");
}
