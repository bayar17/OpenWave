#define _GNU_SOURCE
#include "../user-folders.h"
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void remove_tree(const char *path)
{
    struct stat st;
    if (g_lstat(path, &st)) return;
    if (S_ISDIR(st.st_mode)) {
        GDir *dir = g_dir_open(path, 0, NULL);
        const char *name;
        while (dir && (name = g_dir_read_name(dir))) {
            char *child = g_build_filename(path, name, NULL); remove_tree(child); g_free(child);
        }
        if (dir) g_dir_close(dir);
        g_rmdir(path);
    } else g_unlink(path);
}

int main(void)
{
    GError *error = NULL;
    char *temporary = g_dir_make_tmp("openwave-folders-XXXXXX", &error);
    g_assert_no_error(error); g_assert_nonnull(temporary);
    char *home = g_build_filename(temporary, "user", NULL);
    char *data = g_build_filename(temporary, "app", NULL);
    char *config = g_build_filename(data, "Config", NULL);
    g_assert_cmpint(g_mkdir_with_parents(home, 0700), ==, 0);
    g_assert_cmpint(g_mkdir_with_parents(config, 0700), ==, 0);
    char *documents = g_build_filename(data, "Documents", NULL);
    g_assert_true(g_file_set_contents(documents, "existing application data", -1, &error));
    g_assert_true(appd_user_folders(home, data, geteuid(), &error)); g_assert_no_error(error);
    const char *names[][2] = {{"Pictures", "Pictures"}, {"Downloads", "Downloads"}, {"Music", "Musics"}, {"Musics", "Musics"}, {"Videos", "Videos"}};
    for (guint i = 0; i < G_N_ELEMENTS(names); ++i) {
        char *path = g_build_filename(data, names[i][0], NULL);
        char *target = g_file_read_link(path, &error);
        char *expected = g_build_filename(home, names[i][1], NULL);
        g_assert_no_error(error); g_assert_cmpstr(target, ==, expected);
        struct stat st; g_assert_cmpint(lstat(path, &st), ==, 0); g_assert_cmpuint(st.st_uid, ==, geteuid());
        g_free(path); g_free(target); g_free(expected);
    }
    char *text = NULL;
    g_assert_true(g_file_get_contents(documents, &text, NULL, &error));
    g_assert_cmpstr(text, ==, "existing application data"); g_free(text);
    char *dirs = g_build_filename(config, "user-dirs.dirs", NULL);
    g_assert_true(g_file_get_contents(dirs, &text, NULL, &error));
    g_assert_nonnull(strstr(text, "XDG_PICTURES_DIR=\"$HOME/Pictures\"")); g_free(text);
    g_assert_true(g_file_set_contents(dirs, "custom preferences", -1, &error));
    g_assert_true(appd_user_folders(home, data, geteuid(), &error));
    g_assert_true(g_file_get_contents(dirs, &text, NULL, &error));
    g_assert_cmpstr(text, ==, "custom preferences"); g_free(text);
    g_assert_cmpint(g_unlink(dirs), ==, 0); g_assert_cmpint(g_rmdir(config), ==, 0);
    g_assert_cmpint(symlink(home, config), ==, 0);
    g_assert_false(appd_user_folders(home, data, geteuid(), &error)); g_clear_error(&error);
    char *outside = g_build_filename(home, "user-dirs.dirs", NULL);
    g_assert_false(g_file_test(outside, G_FILE_TEST_EXISTS));
    g_free(outside); g_free(dirs); g_free(documents); g_free(config); g_free(data); g_free(home);
    remove_tree(temporary); g_free(temporary);
    g_print("User-folder links, ownership, conflicts, preferences, and symlink rejection passed\n");
    return 0;
}
