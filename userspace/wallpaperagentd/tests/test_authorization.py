#!/usr/bin/env python3
"""Exercise the production wallpaper authorization predicates and the appearance lookup."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[3]
SDK = ROOT / 'toolchain/sysroot/x86_64-unknown-openwave'

class WallpaperAuthorization(unittest.TestCase):
    def test_only_verified_trusted_settings_can_write_and_only_the_shell_can_apply_appearances(self):
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
    GVariant *settings = identity("com.openwave.settings", TRUE, TRUE);
    GVariant *files = identity("com.openwave.files", TRUE, TRUE);
    GVariant *untrusted = identity("com.openwave.settings", FALSE, TRUE);
    g_assert_true(appearance_caller(TRUE, NULL));
    g_assert_true(appearance_caller(FALSE, settings));
    g_assert_false(appearance_caller(FALSE, files));
    g_assert_false(appearance_caller(FALSE, untrusted));
    g_assert_false(appearance_caller(FALSE, NULL));
    g_variant_unref(settings); g_variant_unref(files); g_variant_unref(untrusted);

    g_assert_true(valid_appearance_id("dusk"));
    g_assert_true(valid_appearance_id("winter-2026"));
    g_assert_false(valid_appearance_id(NULL));
    g_assert_false(valid_appearance_id(""));
    g_assert_false(valid_appearance_id("Dusk"));
    g_assert_false(valid_appearance_id("1dusk"));
    g_assert_false(valid_appearance_id("../dusk"));
    g_assert_false(valid_appearance_id("du/sk"));
    g_assert_false(valid_appearance_id("a123456789012345678901234567890123"));

    g_assert_true(valid_plain_file_name("wall.png"));
    g_assert_false(valid_plain_file_name(NULL));
    g_assert_false(valid_plain_file_name(".hidden.png"));
    g_assert_false(valid_plain_file_name("../wall.png"));
    g_assert_false(valid_plain_file_name("dir/wall.png"));
    g_assert_false(valid_plain_file_name("dir\\wall.png"));

    const char *good = "# c\n[Other]\nX-OpenWave-Appearance-Wallpaper=wrong.png\n[Desktop Entry]\nType=X-OpenWave-Appearance\nName=A\nX-OpenWave-Appearance-Wallpaper = wall.png\n";
    char *name = appearance_wallpaper_name((const guint8 *) good, strlen(good));
    g_assert_cmpstr(name, ==, "wall.png"); g_free(name);
    const char *application = "[Desktop Entry]\nType=Application\nX-OpenWave-Appearance-Wallpaper=wall.png\n";
    g_assert_null(appearance_wallpaper_name((const guint8 *) application, strlen(application)));
    const char *escaping = "[Desktop Entry]\nType=X-OpenWave-Appearance\nX-OpenWave-Appearance-Wallpaper=../etc/passwd\n";
    g_assert_null(appearance_wallpaper_name((const guint8 *) escaping, strlen(escaping)));
    const char *missing = "[Desktop Entry]\nType=X-OpenWave-Appearance\nName=A\n";
    g_assert_null(appearance_wallpaper_name((const guint8 *) missing, strlen(missing)));

    char *root = g_dir_make_tmp("openwave-appearance-XXXXXX", NULL);
    g_assert_nonnull(root);
    Service service = { .root = root };
    char *directory = g_build_filename(root, "System/QuickMenu/Appearances", NULL);
    g_assert_cmpint(g_mkdir_with_parents(directory, 0755), ==, 0);
    char *desktop = g_build_filename(directory, "dusk.desktop", NULL);
    char *wallpaper = g_build_filename(directory, "dusk.png", NULL);
    g_assert_true(g_file_set_contents(desktop, "[Desktop Entry]\nType=X-OpenWave-Appearance\nName=Dusk\nX-OpenWave-Appearance-Wallpaper=dusk.png\n", -1, NULL));
    g_assert_true(g_file_set_contents(wallpaper, "PNGDATA", -1, NULL));
    guint8 *bytes = NULL; gsize length = 0; gboolean found = FALSE;
    g_assert_true(appearance_find_in(&service, SYSTEM_APPEARANCES, "dusk", &bytes, &length, &found));
    g_assert_true(found);
    g_assert_cmpint(length, ==, 7); g_assert_cmpmem(bytes, length, "PNGDATA", 7); g_free(bytes); bytes = NULL;
    g_assert_false(appearance_find_in(&service, SYSTEM_APPEARANCES, "missing", &bytes, &length, &found));
    g_assert_false(found);
    g_assert_false(appearance_png(&service, 0, "../dusk", &bytes, &length, &found));
    g_assert_false(found);
    char *link = g_build_filename(directory, "linked.desktop", NULL);
    g_assert_cmpint(symlink(desktop, link), ==, 0);
    g_assert_false(appearance_find_in(&service, SYSTEM_APPEARANCES, "linked", &bytes, &length, &found));
    char *outside = g_build_filename(root, "outside.png", NULL);
    g_assert_true(g_file_set_contents(outside, "OUTSIDE", -1, NULL));
    char *sneaky_link = g_build_filename(directory, "sneaky.png", NULL);
    g_assert_cmpint(symlink(outside, sneaky_link), ==, 0);
    char *sneaky = g_build_filename(directory, "sneaky.desktop", NULL);
    g_assert_true(g_file_set_contents(sneaky, "[Desktop Entry]\nType=X-OpenWave-Appearance\nX-OpenWave-Appearance-Wallpaper=sneaky.png\n", -1, NULL));
    g_assert_false(appearance_find_in(&service, SYSTEM_APPEARANCES, "sneaky", &bytes, &length, &found));
    g_assert_true(found);
    char *empty = g_build_filename(directory, "empty.desktop", NULL);
    g_assert_true(g_file_set_contents(empty, "", 0, NULL));
    g_assert_false(appearance_find_in(&service, SYSTEM_APPEARANCES, "empty", &bytes, &length, &found));
    char *big = g_build_filename(directory, "big.desktop", NULL);
    char *huge = g_malloc0(MAX_DESKTOP_BYTES + 10);
    memset(huge, 'x', MAX_DESKTOP_BYTES + 9);
    g_assert_true(g_file_set_contents(big, huge, MAX_DESKTOP_BYTES + 9, NULL));
    g_assert_false(appearance_find_in(&service, SYSTEM_APPEARANCES, "big", &bytes, &length, &found));
    char *plain = g_build_filename(directory, "plain.desktop", NULL);
    g_assert_true(g_file_set_contents(plain, "[Desktop Entry]\nType=X-OpenWave-Appearance\nName=Plain\n", -1, NULL));
    g_assert_true(appearance_find_in(&service, SYSTEM_APPEARANCES, "plain", &bytes, &length, &found));
    g_assert_true(found); g_assert_null(bytes); g_assert_cmpint(length, ==, 0);
    g_free(plain);
    g_free(huge); g_free(big); g_free(empty); g_free(sneaky); g_free(sneaky_link); g_free(outside);
    g_free(link); g_free(wallpaper); g_free(desktop); g_free(directory); g_free(root);
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
