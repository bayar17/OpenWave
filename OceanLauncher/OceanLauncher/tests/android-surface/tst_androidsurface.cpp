// Host unit test: which Wayland toplevels Ocean accepts as Android apps.
#include "../../AndroidSurface.h"
#include <cstdio>
#include <cstdlib>

static int failures = 0;
static void check(bool condition, const char *what)
{
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}

int main()
{
    const QByteArray android = OpenWaveAndroidPayloadLabel;
    check(openwaveAndroidApplicationId(android, "waydroid.com.android.deskclock")
          == "org.openwave.android.com.android.deskclock", "valid Android window maps to reserved ID");
    check(openwaveAndroidApplicationId(android + '\0', "waydroid.com.android.calculator2")
          == "org.openwave.android.com.android.calculator2", "NUL-terminated SO_PEERSEC label accepted");
    // Only the Android payload's kernel label is trusted, never the app_id alone.
    for (const char *label : {"system_u:system_r:openwave_app_t:s0", "system_u:system_r:openwave_shell_t:s0",
                              "system_u:system_r:openwave_android_t:s0:c1", "", "openwave_android_t"})
        check(openwaveAndroidApplicationId(label, "waydroid.com.android.deskclock").isEmpty(), label);
    // The full Android UI and malformed or spoofed app_ids are never accepted.
    for (const char *appId : {"Waydroid", "waydroid.", "waydroid.deskclock", "com.android.deskclock",
                              "waydroid.com.openwave..x", "waydroid.1com.a", "waydroid.com.a/b",
                              "waydroid.com.a b", "waydroid.com.android.deskclock\n", "WAYDROID.com.a.b",
                              "xwaydroid.com.a.b"})
        check(openwaveAndroidApplicationId(android, appId).isEmpty(), appId);
    const QString longPackage = "a." + QString(120, 'b');
    check(openwaveAndroidPackage("waydroid." + longPackage).isEmpty(), "IDs stay within 128 characters");
    check(openwaveAndroidApplicationIdPrefix("org.openwave.android.com.x"), "prefix recognised");
    check(!openwaveAndroidApplicationIdPrefix("com.openwave.settings"), "native ID is not Android");
    if (!failures) std::puts("tst_androidsurface: all checks passed");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
