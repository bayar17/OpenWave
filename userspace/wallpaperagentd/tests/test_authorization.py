#!/usr/bin/env python3
"""Exercise the production Settings-only wallpaper authorization predicate."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / 'toolchain/sysroot/x86_64-unknown-openwave'

class WallpaperAuthorization(unittest.TestCase):
    def test_only_verified_trusted_settings_can_write(self):
        with tempfile.TemporaryDirectory(prefix='openwave-wallpaper-auth-') as temporary:
            source = Path(temporary) / 'auth.c'
            binary = Path(temporary) / 'auth'
            source.write_text(r'''
#define main wallpaper_service_main
#include "main.c"
#undef main
static GVariant *identity(const char *app, gboolean trusted, gboolean include_trust) {
    GVariantBuilder b; g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", "appId", g_variant_new_string(app));
    if (include_trust) g_variant_builder_add(&b, "{sv}", "trusted", g_variant_new_boolean(trusted));
    return g_variant_ref_sink(g_variant_builder_end(&b));
}
int main(void) {
    g_assert_false(settings_identity(NULL));
    const char *apps[] = {"com.openwave.settings", "com.openwave.settings", "com.openwave.files", "org.example.settings", "com.openwave.settings"};
    gboolean trust[] = {TRUE, FALSE, TRUE, TRUE, TRUE};
    for (guint i = 0; i < G_N_ELEMENTS(apps); ++i) {
        GVariant *id = identity(apps[i], trust[i], i != 4);
        g_assert_cmpint(settings_identity(id), ==, i == 0);
        g_variant_unref(id);
    }
    return 0;
}
''')
            subprocess.run([str(ROOT / 'toolchain/bin/clang'), '--target=x86_64-unknown-openwave',
                '--sysroot=' + str(SDK), '-O2', '-ffunction-sections', '-fdata-sections',
                '-I' + str(ROOT / 'userspace/wallpaperagentd'), '-I' + str(ROOT / 'toolchain/include'),
                '-I' + str(SDK / 'usr/include/libpng16'), '-I' + str(SDK / 'usr/include/glib-2.0'), '-I' + str(SDK / 'usr/lib64/glib-2.0/include'),
                str(source), '-Wl,--gc-sections', '-lgio-2.0', '-lgobject-2.0', '-lglib-2.0', '-o', str(binary)], check=True)
            subprocess.run([str(SDK / 'lib64/ld-linux-x86-64.so.2'), '--library-path',
                ':'.join(str(SDK / p) for p in ('usr/lib64', 'usr/lib', 'lib64')), str(binary)], check=True)
if __name__ == '__main__': unittest.main()
