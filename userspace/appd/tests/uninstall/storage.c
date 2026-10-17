#include "../../uninstall-storage.h"
#include <glib/gstdio.h>
#include <unistd.h>
#include <string.h>

static const char *id = "org.openwave.webapp.h0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static char *root, *apps, *data, *outside, *bundle, *private_data, *pending_app, *pending_data;
static void file(const char *base, const char *relative)
{
    char *path = g_build_filename(base, relative, NULL), *parent = g_path_get_dirname(path);
    g_assert_cmpint(g_mkdir_with_parents(parent, 0700), ==, 0);
    g_assert_true(g_file_set_contents(path, "keep me", -1, NULL));
    g_free(parent); g_free(path);
}
static void setup(void)
{
    root = g_dir_make_tmp("openwave-uninstall-XXXXXX", NULL);
    apps = g_build_filename(root, "apps", NULL); data = g_build_filename(root, "data", NULL);
    outside = g_build_filename(root, "documents", NULL); bundle = g_build_filename(apps, id, NULL);
    private_data = g_build_filename(data, id, NULL);
    char *pending = g_strconcat(".uninstall-", id, NULL);
    pending_app = g_build_filename(apps, pending, NULL); pending_data = g_build_filename(data, pending, NULL); g_free(pending);
    file(bundle, "bin/openwave-webapp"); file(bundle, "manifest.json");
    file(private_data, "Data/WebKit/cookies/cookies.sqlite"); file(private_data, "Cache/WebKit/cache.bin");
    file(private_data, "Config/preferences.json"); file(outside, "document.txt"); file(data, "other-app/sentinel");
}
static void teardown(void)
{
    char *argv[] = {"rm", "-rf", "--", root, NULL};
    g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, NULL));
    g_free(pending_data); g_free(pending_app); g_free(private_data); g_free(bundle); g_free(outside); g_free(data); g_free(apps); g_free(root);
}
static void shared_survives(void)
{
    char *sentinel = g_build_filename(outside, "document.txt", NULL);
    g_assert_true(g_file_test(sentinel, G_FILE_TEST_IS_REGULAR)); g_free(sentinel);
    sentinel = g_build_filename(data, "other-app/sentinel", NULL);
    g_assert_true(g_file_test(sentinel, G_FILE_TEST_IS_REGULAR)); g_free(sentinel);
}
static void remove_all(void)
{
    setup();
    char *link = g_build_filename(private_data, "Documents", NULL);
    g_assert_cmpint(symlink(outside, link), ==, 0); g_free(link);
    GError *error = NULL; g_assert_true(appd_uninstall_storage(apps, data, id, &error)); g_assert_no_error(error);
    g_assert_false(g_file_test(bundle, G_FILE_TEST_EXISTS)); g_assert_false(g_file_test(private_data, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(pending_app, G_FILE_TEST_EXISTS)); g_assert_false(g_file_test(pending_data, G_FILE_TEST_EXISTS));
    shared_survives(); teardown();
}
static void interrupted(void)
{
    setup(); g_assert_cmpint(rename(bundle, pending_app), ==, 0); g_assert_cmpint(rename(private_data, pending_data), ==, 0);
    g_assert_true(appd_uninstall_storage(apps, data, id, NULL));
    g_assert_false(g_file_test(pending_app, G_FILE_TEST_EXISTS)); g_assert_false(g_file_test(pending_data, G_FILE_TEST_EXISTS));
    shared_survives(); teardown();
}
static void bundle_only_interrupted(void)
{
    setup(); g_assert_cmpint(rename(bundle, pending_app), ==, 0);
    g_assert_true(appd_uninstall_storage(apps, data, id, NULL));
    g_assert_false(g_file_test(private_data, G_FILE_TEST_EXISTS)); shared_survives(); teardown();
}
static void invalid_identifiers(void)
{
    setup();
    const char *ids[] = {"../documents", "com.openwave.settings", "org.openwave.webapp.h/../../../documents", "org.openwave.webapp.hbad", NULL};
    for (int i = 0; ids[i]; ++i) g_assert_false(appd_uninstall_storage(apps, data, ids[i], NULL));
    g_assert_true(g_file_test(bundle, G_FILE_TEST_EXISTS)); g_assert_true(g_file_test(private_data, G_FILE_TEST_EXISTS));
    shared_survives(); teardown();
}
static void data_link(void)
{
    setup();
    char *original = g_strconcat(private_data, "-original", NULL);
    g_assert_cmpint(rename(private_data, original), ==, 0); g_assert_cmpint(symlink(outside, private_data), ==, 0);
    g_assert_true(appd_uninstall_storage(apps, data, id, NULL));
    g_assert_false(g_file_test(private_data, G_FILE_TEST_IS_SYMLINK)); shared_survives(); g_free(original); teardown();
}
static void parent_link(void)
{
    setup();
    char *alias = g_build_filename(root, "alias", NULL); g_assert_cmpint(symlink(data, alias), ==, 0);
    g_assert_false(appd_uninstall_storage(apps, alias, id, NULL));
    g_assert_true(g_file_test(private_data, G_FILE_TEST_EXISTS)); g_assert_true(g_file_test(pending_app, G_FILE_TEST_EXISTS));
    g_assert_true(appd_uninstall_storage(apps, data, id, NULL)); shared_survives(); g_free(alias); teardown();
}
static void conflict(void)
{
    setup(); file(pending_app, "sentinel");
    g_assert_false(appd_uninstall_storage(apps, data, id, NULL));
    g_assert_true(g_file_test(bundle, G_FILE_TEST_EXISTS)); g_assert_true(g_file_test(private_data, G_FILE_TEST_EXISTS));
    shared_survives(); teardown();
}
static void no_data(void)
{
    setup(); char *missing = g_build_filename(root, "never-launched", NULL);
    g_assert_true(appd_uninstall_storage(apps, missing, id, NULL));
    g_assert_false(g_file_test(bundle, G_FILE_TEST_EXISTS)); shared_survives(); g_free(missing); teardown();
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/uninstall/private-data-shared-links", remove_all);
    g_test_add_func("/uninstall/interrupted-data", interrupted);
    g_test_add_func("/uninstall/interrupted-bundle", bundle_only_interrupted);
    g_test_add_func("/uninstall/identifiers", invalid_identifiers);
    g_test_add_func("/uninstall/data-root-link", data_link);
    g_test_add_func("/uninstall/parent-link", parent_link);
    g_test_add_func("/uninstall/pending-conflict", conflict);
    g_test_add_func("/uninstall/never-launched", no_data);
    return g_test_run();
}
