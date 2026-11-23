#define _GNU_SOURCE
#include "../../desktop.h"
#include "../../mimeapps.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int passed, failed;
static char *root;

#define CHECK(condition) do { if (condition) ++passed; else { ++failed; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #condition); } } while (0)

static void write_file(const char *relative, const char *contents)
{
    char *path = g_build_filename(root, relative, NULL);
    char *directory = g_path_get_dirname(path);
    g_mkdir_with_parents(directory, 0755);
    CHECK(g_file_set_contents(path, contents, -1, NULL));
    chmod(path, 0644);
    g_free(directory); g_free(path);
}

static char *path_of(const char *relative)
{
    return g_build_filename(root, relative, NULL);
}

static DesktopPaths *make_paths(const char *language)
{
    char *home = path_of("home");
    char *system = path_of("usr/local/share");
    char *vendor = path_of("usr/share");
    char *dirs = g_strdup_printf("%s:%s", system, vendor);
    char *config = path_of("etc/xdg");
    DesktopPaths *paths = desktop_paths_new(home, getuid(), getgid(), "OpenWave", dirs, config, language);
    g_free(home); g_free(system); g_free(vendor); g_free(dirs); g_free(config);
    return paths;
}

static DesktopEntry *find(GPtrArray *entries, const char *id)
{
    for (guint i = 0; i < entries->len; ++i)
        if (!strcmp(((DesktopEntry *)g_ptr_array_index(entries, i))->id, id)) return g_ptr_array_index(entries, i);
    return NULL;
}

static void test_parsing(void)
{
    write_file("usr/share/applications/viewer.desktop",
        "[Desktop Entry]\nType=Application\nName=Viewer\nName[de]=Betrachter\nName[de_AT]=Betrachter AT\nGenericName=Image Viewer\n"
        "Comment=Looks at things\nExec=viewer --open %F\nTryExec=viewer\nIcon=viewer\nCategories=Graphics;Viewer;\n"
        "MimeType=image/png;image/jpeg;x-scheme-handler/viewer;\nActions=new;bad id;\nX-OpenWave-AppId=org.example.viewer\n"
        "[Desktop Action new]\nName=New Window\nExec=viewer --new\n[Desktop Action bad id]\nName=No\nExec=viewer\n");
    DesktopPaths *english = make_paths("en_US.UTF-8");
    DesktopPaths *german = make_paths("de_AT.UTF-8");
    char *file = path_of("usr/share/applications/viewer.desktop");
    GError *error = NULL;
    DesktopEntry *entry = desktop_entry_parse(file, "viewer.desktop", "x", english, &error);
    CHECK(entry && !error);
    CHECK(entry && !strcmp(entry->name, "Viewer"));
    CHECK(entry && !strcmp(entry->generic_name, "Image Viewer"));
    CHECK(entry && g_strv_length(entry->categories) == 2);
    CHECK(entry && g_strv_length(entry->mime_types) == 3);
    CHECK(entry && entry->actions->len == 1);
    CHECK(entry && desktop_entry_has_mime(entry, "image/png") && !desktop_entry_has_mime(entry, "image/gif"));
    char **schemes = entry ? desktop_entry_schemes(entry) : NULL;
    CHECK(schemes && g_strv_length(schemes) == 1 && !strcmp(schemes[0], "viewer"));
    g_strfreev(schemes);
    CHECK(entry && !entry->app_id == !entry->trusted_source);
    desktop_entry_free(entry);
    entry = desktop_entry_parse(file, "viewer.desktop", "x", german, &error);
    CHECK(entry && !strcmp(entry->name, "Betrachter AT"));
    desktop_entry_free(entry);
    g_free(file);
    desktop_paths_free(english); desktop_paths_free(german);
}

static void test_malformed(void)
{
    DesktopPaths *paths = make_paths("C");
    const char *bad[][2] = {
        {"bad-binary.desktop", "\x01\x02\x03\xff\xfe garbage"},
        {"bad-type.desktop", "[Desktop Entry]\nType=Link\nName=X\nURL=http://example.com\n"},
        {"bad-name.desktop", "[Desktop Entry]\nType=Application\nExec=true\n"},
        {"bad-exec.desktop", "[Desktop Entry]\nType=Application\nName=X\nExec=\"unterminated\n"},
        {"bad-code.desktop", "[Desktop Entry]\nType=Application\nName=X\nExec=app %z\n"},
        {"bad-nogroup.desktop", "Name=X\nExec=true\n"},
        {"bad-empty.desktop", ""},
        {"bad-utf8.desktop", "[Desktop Entry]\nType=Application\nName=\xc3\x28\nExec=true\n"},
    };
    for (guint i = 0; i < G_N_ELEMENTS(bad); ++i) {
        char *relative = g_strconcat("usr/share/applications/", bad[i][0], NULL);
        write_file(relative, bad[i][1]);
        char *file = path_of(relative);
        GError *error = NULL;
        DesktopEntry *entry = desktop_entry_parse(file, bad[i][0], "x", paths, &error);
        CHECK(entry == NULL);
        CHECK(error != NULL);
        g_clear_error(&error); desktop_entry_free(entry); g_free(file); g_free(relative);
    }
    char *big = g_malloc(DESKTOP_MAX_BYTES + 100);
    memset(big, 'a', DESKTOP_MAX_BYTES + 99); big[DESKTOP_MAX_BYTES + 99] = 0;
    char *content = g_strconcat("[Desktop Entry]\nType=Application\nName=Big\nExec=true\nComment=", big, "\n", NULL);
    write_file("usr/share/applications/bad-big.desktop", content);
    char *file = path_of("usr/share/applications/bad-big.desktop");
    CHECK(desktop_entry_parse(file, "bad-big.desktop", "x", paths, NULL) == NULL);
    g_free(file); g_free(big); g_free(content);
    char *link = path_of("home/.local/share/applications/link.desktop");
    char *dir = g_path_get_dirname(link);
    g_mkdir_with_parents(dir, 0755);
    char *target = path_of("usr/share/applications/viewer.desktop");
    CHECK(symlink(target, link) == 0);
    CHECK(desktop_entry_parse(link, "link.desktop", "x", paths, NULL) == NULL);
    GPtrArray *entries = desktop_scan(paths);
    CHECK(find(entries, "link.desktop") == NULL);
    CHECK(find(entries, "viewer.desktop") != NULL);
    g_ptr_array_unref(entries);
    g_free(link); g_free(dir); g_free(target);
    desktop_paths_free(paths);
}

static void test_precedence(void)
{
    write_file("usr/share/applications/dup.desktop", "[Desktop Entry]\nType=Application\nName=Vendor\nExec=vendor\n");
    write_file("usr/local/share/applications/dup.desktop", "[Desktop Entry]\nType=Application\nName=Local\nExec=local\n");
    write_file("usr/share/applications/sub/nested.desktop", "[Desktop Entry]\nType=Application\nName=Nested\nExec=nested\n");
    write_file("home/.local/share/applications/mine.desktop", "[Desktop Entry]\nType=Application\nName=Mine\nExec=mine\nX-OpenWave-AppId=org.example.fake.app\n");
    write_file("usr/share/applications/trusted.desktop", "[Desktop Entry]\nType=Application\nName=Trusted\nExec=/System/Services/openwave-launch org.example.trusted.app\nX-OpenWave-AppId=org.example.trusted.app\n");
    write_file("home/.local/share/applications/trusted.desktop", "[Desktop Entry]\nType=Application\nName=Fake\nExec=evil\nX-OpenWave-AppId=org.example.trusted.app\n");
    write_file("usr/share/applications/hide.desktop", "[Desktop Entry]\nType=Application\nName=Hide\nExec=hide\nX-OpenWave-AppId=org.example.hide.app\n");
    write_file("home/.local/share/applications/hide.desktop", "[Desktop Entry]\nType=Application\nName=Hide\nHidden=true\n");
    DesktopPaths *paths = make_paths("C");
    char *stop = path_of("");
    stop[strlen(stop) - 1] = 0;
    desktop_trust_configure(getuid(), root);
    GPtrArray *entries = desktop_scan(paths);
    DesktopEntry *dup = find(entries, "dup.desktop");
    CHECK(dup && !strcmp(dup->name, "Local"));
    CHECK(find(entries, "sub-nested.desktop") != NULL);
    DesktopEntry *mine = find(entries, "mine.desktop");
    CHECK(mine && mine->user_source && !mine->trusted_source && mine->app_id == NULL);
    DesktopEntry *trusted = find(entries, "trusted.desktop");
    CHECK(trusted && !strcmp(trusted->name, "Trusted") && trusted->trusted_source);
    CHECK(trusted && !g_strcmp0(trusted->app_id, "org.example.trusted.app"));
    DesktopEntry *hide = find(entries, "hide.desktop");
    CHECK(hide && hide->hidden && hide->app_id != NULL);
    guint seen = 0;
    for (guint i = 0; i < entries->len; ++i) seen += !strcmp(((DesktopEntry *)g_ptr_array_index(entries, i))->id, "dup.desktop");
    CHECK(seen == 1);
    g_ptr_array_unref(entries);
    desktop_trust_configure(getuid() + 1, root);
    entries = desktop_scan(paths);
    trusted = find(entries, "trusted.desktop");
    CHECK(trusted && trusted->app_id == NULL);
    g_ptr_array_unref(entries);
    desktop_trust_configure(getuid(), root);
    g_free(stop);
    desktop_paths_free(paths);
}

static void test_visibility(void)
{
    DesktopEntry entry = {0};
    char *only[] = {"OpenWave", "KDE", NULL};
    char *not_in[] = {"GNOME", NULL};
    char *other[] = {"GNOME", NULL};
    entry.only_show_in = other;
    CHECK(!desktop_entry_shown(&entry, "OpenWave"));
    entry.only_show_in = only;
    CHECK(desktop_entry_shown(&entry, "OpenWave"));
    CHECK(desktop_entry_shown(&entry, "Other:OpenWave"));
    CHECK(!desktop_entry_shown(&entry, "Unity"));
    entry.only_show_in = NULL; entry.not_show_in = not_in;
    CHECK(desktop_entry_shown(&entry, "OpenWave"));
    CHECK(!desktop_entry_shown(&entry, "GNOME"));
    entry.not_show_in = NULL; entry.no_display = TRUE;
    CHECK(!desktop_entry_shown(&entry, "OpenWave"));
    entry.no_display = FALSE; entry.hidden = TRUE;
    CHECK(!desktop_entry_shown(&entry, "OpenWave"));
    entry.hidden = FALSE; entry.try_exec = "/definitely/missing/program";
    CHECK(!desktop_entry_try_exec_ok(&entry));
    entry.try_exec = "/bin/sh";
    CHECK(desktop_entry_try_exec_ok(&entry));
    entry.try_exec = "sh";
    CHECK(desktop_entry_try_exec_ok(&entry));
    entry.try_exec = "../bin/sh";
    CHECK(!desktop_entry_try_exec_ok(&entry));
    entry.try_exec = NULL;
    CHECK(desktop_entry_try_exec_ok(&entry));
}

static void test_exec(void)
{
    DesktopEntry entry = {0};
    entry.name = "My App"; entry.icon = "myicon"; entry.path = "/usr/share/applications/my.desktop";
    const char *files[] = {"/home/a b.txt", "/home/c.txt", NULL};
    const char *uris[] = {"file:///home/a%20b.txt", "http://example.com/", NULL};
    DesktopExecContext context = {files, uris, NULL, NULL, NULL};
    GError *error = NULL;
    char **argv = desktop_exec_expand(&entry, "app --flag %f", &context, &error);
    CHECK(argv && g_strv_length(argv) == 3 && !strcmp(argv[2], "/home/a b.txt"));
    g_strfreev(argv);
    argv = desktop_exec_expand(&entry, "app %F end", &context, &error);
    CHECK(argv && g_strv_length(argv) == 4 && !strcmp(argv[1], "/home/a b.txt") && !strcmp(argv[2], "/home/c.txt") && !strcmp(argv[3], "end"));
    g_strfreev(argv);
    argv = desktop_exec_expand(&entry, "app %U", &context, &error);
    CHECK(argv && g_strv_length(argv) == 3 && !strcmp(argv[2], "http://example.com/"));
    g_strfreev(argv);
    argv = desktop_exec_expand(&entry, "app --uri=%u", &context, &error);
    CHECK(argv && !strcmp(argv[1], "--uri=file:///home/a%20b.txt"));
    g_strfreev(argv);
    argv = desktop_exec_expand(&entry, "app %i %c %k 100%% %n", &context, &error);
    CHECK(argv && g_strv_length(argv) == 6 && !strcmp(argv[1], "--icon") && !strcmp(argv[2], "myicon")
        && !strcmp(argv[3], "My App") && !strcmp(argv[4], "/usr/share/applications/my.desktop") && !strcmp(argv[5], "100%"));
    g_strfreev(argv);
    DesktopExecContext none = {NULL, NULL, NULL, NULL, NULL};
    argv = desktop_exec_expand(&entry, "app %f %U", &none, &error);
    CHECK(argv && g_strv_length(argv) == 1);
    g_strfreev(argv);
    argv = desktop_exec_expand(&entry, "\"/opt/My App/run\" \"quoted \\\" arg\" plain", &none, &error);
    CHECK(argv && g_strv_length(argv) == 3 && !strcmp(argv[0], "/opt/My App/run") && !strcmp(argv[1], "quoted \" arg"));
    g_strfreev(argv);
    argv = desktop_exec_expand(&entry, "sh -c 'rm -rf /; echo $(id) `id` | cat' ; ls", &none, &error);
    CHECK(argv && !strcmp(argv[0], "sh") && !strcmp(argv[1], "-c") && !strcmp(argv[2], "'rm") && g_strv_length(argv) == 12);
    g_strfreev(argv);
    const char *dash[] = {"-rf", NULL};
    DesktopExecContext hostile = {dash, dash, NULL, NULL, NULL};
    argv = desktop_exec_expand(&entry, "app %F %f", &hostile, &error);
    CHECK(argv && g_strv_length(argv) == 1);
    g_strfreev(argv);
    CHECK(!desktop_exec_valid("app \"%f\"", NULL));
    CHECK(!desktop_exec_valid("app %z", NULL));
    CHECK(!desktop_exec_valid("%f app", NULL));
    CHECK(!desktop_exec_valid("app %F %U", NULL));
    CHECK(!desktop_exec_valid("app prefix%F", NULL));
    CHECK(!desktop_exec_valid("", NULL));
    CHECK(!desktop_exec_valid("app \"open", NULL));
    CHECK(desktop_exec_valid("app --x=%u %%", NULL));
    char *huge = g_strnfill(5000, 'a');
    CHECK(!desktop_exec_valid(huge, NULL));
    g_free(huge);
    GString *many = g_string_new("app");
    for (int i = 0; i < 300; ++i) g_string_append(many, " x");
    CHECK(!desktop_exec_valid(many->str, NULL));
    g_string_free(many, TRUE);
    g_clear_error(&error);
}

static void test_waydroid(void)
{
    write_file("home/.local/share/applications/waydroid.com.example.app.desktop",
        "[Desktop Entry]\nType=Application\nName=Example\nExec=waydroid app launch com.example.app\nIcon=/nonexistent/icon.png\nCategories=X-WayDroid-App;\n");
    write_file("home/.local/share/applications/waydroid.evil.desktop",
        "[Desktop Entry]\nType=Application\nName=Evil\nExec=waydroid app launch com.example.app; rm -rf /\n");
    DesktopPaths *paths = make_paths("C");
    char *good = path_of("home/.local/share/applications/waydroid.com.example.app.desktop");
    char *evil = path_of("home/.local/share/applications/waydroid.evil.desktop");
    DesktopEntry *entry = desktop_entry_parse(good, "waydroid.com.example.app.desktop", "x", paths, NULL);
    CHECK(entry && !g_strcmp0(entry->package, "com.example.app") && entry->user_source && !entry->app_id);
    desktop_entry_free(entry);
    entry = desktop_entry_parse(evil, "waydroid.evil.desktop", "x", paths, NULL);
    CHECK(entry && entry->package == NULL);
    desktop_entry_free(entry);
    g_free(good); g_free(evil);
    desktop_paths_free(paths);
}

static void test_icons(void)
{
    write_file("usr/share/icons/hicolor/index.theme",
        "[Icon Theme]\nName=Hicolor\nDirectories=48x48/apps,64x64/apps,scalable/apps\n"
        "[48x48/apps]\nSize=48\nType=Fixed\n[64x64/apps]\nSize=64\nType=Fixed\n[scalable/apps]\nSize=128\nMinSize=16\nMaxSize=512\nType=Scalable\n");
    write_file("usr/share/icons/custom/index.theme", "[Icon Theme]\nName=Custom\nInherits=hicolor\nDirectories=32x32/apps\n[32x32/apps]\nSize=32\nType=Fixed\n");
    write_file("usr/share/icons/custom/32x32/apps/small.png", "png");
    write_file("usr/share/icons/hicolor/48x48/apps/app.png", "png");
    write_file("usr/share/icons/hicolor/64x64/apps/app.png", "png");
    write_file("usr/share/icons/hicolor/scalable/apps/vector.svg", "<svg/>");
    write_file("usr/share/pixmaps/legacy.xpm", "xpm");
    write_file("usr/share/icons/hicolor/48x48/apps/inherited.png", "png");
    DesktopPaths *paths = make_paths("C");
    char *expected = path_of("usr/share/icons/hicolor/64x64/apps/app.png");
    char *found = desktop_resolve_icon(paths, "app", 64);
    CHECK(!g_strcmp0(found, expected));
    g_free(found);
    expected = path_of("usr/share/icons/hicolor/48x48/apps/app.png");
    found = desktop_resolve_icon(paths, "app", 40);
    CHECK(!g_strcmp0(found, expected));
    g_free(found); g_free(expected);
    found = desktop_resolve_icon(paths, "vector", 64);
    CHECK(found && g_str_has_suffix(found, "scalable/apps/vector.svg"));
    g_free(found);
    g_free(paths->icon_theme); paths->icon_theme = g_strdup("custom");
    found = desktop_resolve_icon(paths, "small", 32);
    CHECK(found && g_str_has_suffix(found, "custom/32x32/apps/small.png"));
    g_free(found);
    found = desktop_resolve_icon(paths, "inherited", 48);
    CHECK(found && g_str_has_suffix(found, "hicolor/48x48/apps/inherited.png"));
    g_free(found);
    found = desktop_resolve_icon(paths, "legacy", 48);
    CHECK(found && g_str_has_suffix(found, "pixmaps/legacy.xpm"));
    g_free(found);
    CHECK(desktop_resolve_icon(paths, "missing", 48) == NULL);
    CHECK(desktop_resolve_icon(paths, "../../etc/passwd", 48) == NULL);
    CHECK(desktop_resolve_icon(paths, "a/b", 48) == NULL);
    char *absolute = path_of("usr/share/icons/hicolor/48x48/apps/app.png");
    found = desktop_resolve_icon(paths, absolute, 48);
    CHECK(!g_strcmp0(found, absolute));
    g_free(found); g_free(absolute);
    CHECK(desktop_resolve_icon(paths, "/etc/passwd", 48) == NULL);
    desktop_paths_free(paths);
}

static gboolean usable_a(const char *id, gpointer data)
{
    (void)data;
    return strcmp(id, "missing.desktop") != 0;
}

static void test_mimeapps(void)
{
    write_file("usr/share/applications/mimeapps.list", "[Default Applications]\nimage/png=system-default.desktop;\n[Added Associations]\nimage/png=system-added.desktop;\n");
    write_file("etc/xdg/openwave-mimeapps.list", "[Default Applications]\ntext/plain=desktop-specific.desktop;\n");
    write_file("etc/xdg/mimeapps.list", "[Default Applications]\nimage/jpeg=config-dirs.desktop;\n[Removed Associations]\nimage/png=removed-here.desktop;\n");
    DesktopPaths *paths = make_paths("C");
    char *config = g_build_filename(paths->config_home, NULL);
    g_mkdir_with_parents(config, 0700);
    MimeApps *apps = mimeapps_load(paths);
    char *value = mimeapps_default(apps, "image/png", NULL, NULL);
    CHECK(!g_strcmp0(value, "system-default.desktop"));
    g_free(value);
    value = mimeapps_default(apps, "text/plain", NULL, NULL);
    CHECK(!g_strcmp0(value, "desktop-specific.desktop"));
    g_free(value);
    CHECK(mimeapps_default(apps, "video/mp4", NULL, NULL) == NULL);
    CHECK(mimeapps_default(apps, "not a mime", NULL, NULL) == NULL);
    GPtrArray *entries = g_ptr_array_new_with_free_func((GDestroyNotify)desktop_entry_free);
    DesktopEntry *a = g_new0(DesktopEntry, 1), *b = g_new0(DesktopEntry, 1);
    a->id = g_strdup("removed-here.desktop"); a->mime_types = g_strsplit("image/png", ";", -1);
    b->id = g_strdup("plain.desktop"); b->mime_types = g_strsplit("image/png;image/gif", ";", -1);
    g_ptr_array_add(entries, a); g_ptr_array_add(entries, b);
    GPtrArray *associated = mimeapps_associated(apps, "image/png", entries);
    CHECK(associated->len == 2 && !strcmp(g_ptr_array_index(associated, 0), "system-added.desktop") && !strcmp(g_ptr_array_index(associated, 1), "plain.desktop"));
    g_ptr_array_unref(associated); g_ptr_array_unref(entries);
    char *digest = g_strdup(mimeapps_digest(apps));
    mimeapps_free(apps);
    write_file("home/.config/mimeapps.list", "# user comment\n[Default Applications]\nimage/png=mine.desktop;missing.desktop;\nimage/gif=keep.desktop\n[Custom]\nkey=value\n");
    apps = mimeapps_load(paths);
    CHECK(g_strcmp0(digest, mimeapps_digest(apps)) != 0);
    value = mimeapps_default(apps, "image/png", NULL, NULL);
    CHECK(!g_strcmp0(value, "mine.desktop"));
    g_free(value);
    value = mimeapps_default(apps, "image/png", usable_a, NULL);
    CHECK(!g_strcmp0(value, "mine.desktop"));
    g_free(value);
    char **ids = mimeapps_default_ids(apps, "image/png");
    CHECK(g_strv_length(ids) == 3);
    g_strfreev(ids);
    mimeapps_free(apps); g_free(digest);
    GError *error = NULL;
    CHECK(mimeapps_set_default(paths, "image/png", "other.desktop", &error));
    CHECK(mimeapps_set_default(paths, "video/mp4", "player.desktop", &error));
    char *path = mimeapps_user_path(paths);
    char *contents = NULL;
    g_file_get_contents(path, &contents, NULL, NULL);
    CHECK(strstr(contents, "# user comment") != NULL);
    CHECK(strstr(contents, "image/gif=keep.desktop") != NULL);
    CHECK(strstr(contents, "key=value") != NULL);
    CHECK(strstr(contents, "image/png=other.desktop") != NULL);
    CHECK(strstr(contents, "video/mp4=player.desktop") != NULL);
    g_free(contents);
    struct stat st;
    CHECK(stat(path, &st) == 0 && st.st_uid == getuid() && (st.st_mode & 0777) == 0644);
    CHECK(mimeapps_clear_application(paths, "other.desktop", &error));
    g_file_get_contents(path, &contents, NULL, NULL);
    CHECK(strstr(contents, "other.desktop") == NULL);
    CHECK(strstr(contents, "image/gif=keep.desktop") != NULL);
    CHECK(strstr(contents, "video/mp4=player.desktop") != NULL);
    g_free(contents);
    CHECK(mimeapps_clear_default(paths, "video/mp4", &error));
    g_file_get_contents(path, &contents, NULL, NULL);
    CHECK(strstr(contents, "image/gif=keep.desktop") != NULL);
    g_free(contents);
    apps = mimeapps_load(paths);
    CHECK(mimeapps_default(apps, "video/mp4", NULL, NULL) == NULL);
    mimeapps_free(apps);
    CHECK(!mimeapps_set_default(paths, "bad type", "x.desktop", NULL));
    CHECK(!mimeapps_set_default(paths, "image/png", "../x.desktop", NULL));
    CHECK(!mimeapps_set_default(paths, "image/png", "notdesktop", NULL));
    g_free(path); g_free(config);
    desktop_paths_free(paths);
}

static void test_unsafe_config(void)
{
    char *elsewhere = path_of("elsewhere");
    g_mkdir_with_parents(elsewhere, 0700);
    char *home = path_of("home2");
    char *dirs = path_of("usr/share");
    g_mkdir_with_parents(home, 0700);
    char *config = g_build_filename(home, ".config", NULL);
    CHECK(symlink(elsewhere, config) == 0);
    DesktopPaths *paths = desktop_paths_new(home, getuid(), getgid(), "OpenWave", dirs, NULL, "C");
    GError *error = NULL;
    CHECK(!mimeapps_set_default(paths, "image/png", "a.desktop", &error));
    CHECK(error != NULL);
    char *planted = g_build_filename(elsewhere, "mimeapps.list", NULL);
    CHECK(!g_file_test(planted, G_FILE_TEST_EXISTS));
    g_clear_error(&error);
    unlink(config);
    CHECK(mkdir(config, 0700) == 0);
    char *victim = path_of("victim.txt");
    CHECK(g_file_set_contents(victim, "precious", -1, NULL));
    char *list = g_build_filename(config, "mimeapps.list", NULL);
    CHECK(symlink(victim, list) == 0);
    CHECK(mimeapps_set_default(paths, "image/png", "a.desktop", &error));
    char *contents = NULL;
    g_file_get_contents(victim, &contents, NULL, NULL);
    CHECK(!g_strcmp0(contents, "precious"));
    g_free(contents);
    struct stat st;
    CHECK(lstat(list, &st) == 0 && S_ISREG(st.st_mode));
    MimeApps *apps = mimeapps_load(paths);
    char *value = mimeapps_default(apps, "image/png", NULL, NULL);
    CHECK(!g_strcmp0(value, "a.desktop"));
    g_free(value);
    mimeapps_free(apps);
    g_free(planted); g_free(victim); g_free(list); g_free(config); g_free(home); g_free(dirs); g_free(elsewhere);
    desktop_paths_free(paths);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    root = g_dir_make_tmp("desktop-test-XXXXXX", NULL);
    CHECK(root != NULL);
    desktop_trust_configure(getuid(), root);
    printf("running parsing\n");
    test_parsing();
    printf("running malformed\n");
    test_malformed();
    printf("running precedence\n");
    test_precedence();
    printf("running visibility\n");
    test_visibility();
    printf("running exec\n");
    test_exec();
    printf("running waydroid\n");
    test_waydroid();
    printf("running icons\n");
    test_icons();
    printf("running mimeapps\n");
    test_mimeapps();
    printf("running unsafe_config\n");
    test_unsafe_config();
    printf("desktop tests: %d passed, %d failed\n", passed, failed);
    char *command = g_strdup_printf("rm -rf '%s'", root);
    if (system(command)) printf("cleanup failed\n");
    g_free(command);
    return failed ? 1 : 0;
}
