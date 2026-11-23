#include "../../appd.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QMap>
#include <QTemporaryDir>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

static int checks, failures, signalCount;
static QMap<QString, bool> bundleDefinitions;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); } } while (0)

extern "C" void appd_emit(Manager *, const char *, GVariant *) { ++signalCount; }

extern "C" App *appd_lookup(Manager *m, const char *id)
{
    App *app = static_cast<App *>(g_hash_table_lookup(m->apps, id));
    if (!app && m->android_apps) app = static_cast<App *>(g_hash_table_lookup(m->android_apps, id));
    return app;
}

extern "C" void appd_app_free(gpointer data)
{
    App *a = static_cast<App *>(data);
    g_free(a->id); g_free(a->name); g_free(a->icon); g_free(a->manifest); g_free(a->bundle); g_free(a->digest);
    g_strfreev(a->permissions); g_strfreev(a->opens); g_strfreev(a->mime_types); g_strfreev(a->argv);
    g_free(a->state); g_free(a->error); g_free(a->package); g_free(a->desktop_id); g_free(a->comment);
    g_free(a->generic_name); g_strfreev(a->categories); g_strfreev(a->schemes); g_free(a->presentation); g_free(a);
}

static App *blank(const char *id, const char *name)
{
    App *app = g_new0(App, 1);
    app->id = g_strdup(id); app->name = g_strdup(name); app->icon = g_strdup(""); app->manifest = g_strdup("");
    app->bundle = g_strdup(""); app->digest = g_strdup("d"); app->permissions = g_new0(char *, 1);
    app->opens = g_new0(char *, 1); app->mime_types = g_new0(char *, 1); app->argv = g_new0(char *, 2);
    app->argv[0] = g_strdup("/bin/true"); app->state = g_strdup("Stopped"); app->error = g_strdup("");
    return app;
}

extern "C" App *appd_read_bundle(const char *directory)
{
    const QString id = QString::fromUtf8(directory).section('/', -1);
    if (!bundleDefinitions.contains(id)) return nullptr;
    App *app = blank(id.toUtf8().constData(), "Manifest Name");
    app->paths = bundleDefinitions.value(id);
    g_free(app->bundle); app->bundle = g_strdup(directory);
    return app;
}

static void writeFile(const QString &path, const QByteArray &data)
{
    g_mkdir_with_parents(qPrintable(QFileInfo(path).absolutePath()), 0755);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) { file.write(data); file.close(); }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
}

static QString entry(const char *name, const char *exec, const char *extra)
{
    return QStringLiteral("[Desktop Entry]\nType=Application\nName=%1\nExec=%2\n%3").arg(name, exec, extra);
}

static QString launcher(const char *id, const char *tail = " %F")
{
    return QStringLiteral("/System/Services/openwave-launch %1%2").arg(id, tail);
}

static void rescan(Manager &m)
{
    GHashTable *candidates = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, appd_app_free);
    appd_desktop_scan(&m, candidates);
    GHashTableIter iter; gpointer key, value;
    g_hash_table_remove_all(m.apps);
    g_hash_table_iter_init(&iter, candidates);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        App *app = static_cast<App *>(value);
        g_hash_table_iter_steal(&iter); g_free(key);
        g_hash_table_insert(m.apps, app->id, app);
    }
    g_hash_table_unref(candidates);
    appd_desktop_attach_android(&m);
    appd_defaults_refresh(&m);
}

static QString state(Manager &m, const char *type)
{
    GError *error = nullptr;
    GVariant *result = g_variant_ref_sink(appd_default_file_handler(&m, type, &error));
    const char *id, *status; g_variant_get(result, "(&s&s)", &id, &status);
    const QString text = QString::fromUtf8(id) + "|" + status;
    g_variant_unref(result); g_clear_error(&error);
    return text;
}

static QString chosen(Manager *m, const char *key) {
    auto *all = g_variant_ref_sink(appd_file_associations(m));
    auto *defaults = g_variant_get_child_value(all, 0);
    const char *value = ""; g_variant_lookup(defaults, key, "&s", &value);
    QString result = QString::fromUtf8(value);
    g_variant_unref(defaults); g_variant_unref(all); return result;
}

static QStringList handlersOf(Manager *m, const char *key) {
    auto *all = g_variant_ref_sink(appd_file_associations(m));
    auto *handlers = g_variant_get_child_value(all, 1);
    QStringList result;
    GVariant *ids = g_variant_lookup_value(handlers, key, G_VARIANT_TYPE("as"));
    for (gsize i = 0; ids && i < g_variant_n_children(ids); ++i) {
        const char *id; g_variant_get_child(ids, i, "&s", &id); result << QString::fromUtf8(id);
    }
    if (ids) g_variant_unref(ids);
    g_variant_unref(handlers); g_variant_unref(all); return result;
}

static QString readText(const QString &path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString(); }

static GVariant *categoryList(Manager &m) { return g_variant_ref_sink(appd_default_categories(&m)); }

static bool hasCategory(Manager &m, const char *id, QString *defaultId = nullptr)
{
    GVariant *reply = categoryList(m);
    GVariant *list = g_variant_get_child_value(reply, 0);
    bool found = false;
    for (gsize i = 0; i < g_variant_n_children(list); ++i) {
        GVariant *item = g_variant_get_child_value(list, i);
        const char *name = ""; g_variant_lookup(item, "id", "&s", &name);
        if (!strcmp(name, id)) { found = true; const char *chosenId = ""; g_variant_lookup(item, "default", "&s", &chosenId); if (defaultId) *defaultId = QString::fromUtf8(chosenId); }
        g_variant_unref(item);
    }
    g_variant_unref(list); g_variant_unref(reply);
    return found;
}

static bool detects(const QString &path, const QByteArray &data, const char *expected) {
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(data); file.close(); GError *error = nullptr;
    char *type = appd_detect_type(path.toUtf8().constData(), &error);
    const bool okay = type && !strcmp(type, expected);
    if (!okay) fprintf(stderr, "type %s: got %s expected %s (%s)\n", qPrintable(path), type ? type : "null", expected, error ? error->message : "");
    g_free(type); g_clear_error(&error); return okay;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    umask(022);
    QDir(APPD_DEFAULTS_DIRECTORY).removeRecursively();
    g_mkdir_with_parents(APPD_DEFAULTS_DIRECTORY, 0700);
    QDir(APPD_FIXTURE_ROOT).removeRecursively();
    g_mkdir_with_parents(APPD_FIXTURE_ROOT, 0755);
    chmod(APPD_FIXTURE_ROOT, 0755);
    desktop_trust_configure(getuid(), APPD_FIXTURE_ROOT);
    QTemporaryDir storage;
    Manager m{}; m.uid = getuid(); m.gid = getgid(); m.home = g_strdup(storage.path().toUtf8().constData());
    m.apps = g_hash_table_new(g_str_hash, g_str_equal);
    m.desktop = desktop_paths_new(m.home, m.uid, m.gid, "OpenWave", nullptr, nullptr, "de_DE.UTF-8");
    const QString system = QStringLiteral(APPD_SYSTEM_APPLICATIONS), user = QStringLiteral(APPD_USER_APPLICATIONS);
    const QString shared = QStringLiteral(APPD_FIXTURE_ROOT "/usr/share/applications");
    bundleDefinitions = {{"org.test.viewer", true}, {"org.test.other", true}, {"org.test.custom", true}, {"org.test.nopaths", false}, {"org.test.victim", true}, {"org.test.linked", true}, {"org.test.linkclaim", true}};
    writeFile(system + "/org.test.viewer/share/applications/org.test.viewer.desktop",
        entry("Viewer", qPrintable(launcher("org.test.viewer")), "X-OpenWave-AppId=org.test.viewer\nName[de]=Betrachter\nCategories=Graphics;Viewer;\nMimeType=application/pdf;image/jpeg;application/vnd.openxmlformats-officedocument.spreadsheetml.sheet;x-scheme-handler/test;\nIcon=viewer\n").toUtf8());
    writeFile(system + "/org.test.other/share/applications/org.test.other.desktop",
        entry("Other", qPrintable(launcher("org.test.other")), "X-OpenWave-AppId=org.test.other\nMimeType=application/pdf;image/png;\nOnlyShowIn=OpenWave;\n").toUtf8());
    writeFile(user + "/org.test.custom/share/applications/org.test.custom.desktop",
        entry("Custom", qPrintable(launcher("org.test.custom")), "X-OpenWave-AppId=org.test.custom\nMimeType=application/x-openwave-test;\nNoDisplay=true\n").toUtf8());
    writeFile(user + "/org.test.nopaths/share/applications/org.test.nopaths.desktop",
        entry("NoPaths", qPrintable(launcher("org.test.nopaths")), "X-OpenWave-AppId=org.test.nopaths\nMimeType=application/pdf;\n").toUtf8());
    writeFile(system + "/org.test.victim/share/applications/org.test.victim.desktop",
        entry("Victim", qPrintable(launcher("org.test.victim")), "X-OpenWave-AppId=org.test.victim\nMimeType=text/plain;\n").toUtf8());
    writeFile(user + "/org.test.nopaths/share/applications/forged.desktop",
        (entry("Forged", qPrintable(launcher("org.test.victim")), "X-OpenWave-AppId=org.test.victim\nMimeType=application/pdf;\n") ).toUtf8());
    writeFile(shared + "/wrongexec.desktop",
        entry("Wrong Exec", "/bin/sh -c evil", "X-OpenWave-AppId=org.test.victim\nMimeType=application/pdf;\n").toUtf8());
    writeFile(shared + "/editor.desktop", entry("Editor", "editor %F", "MimeType=text/plain;application/pdf;\nTryExec=/definitely/missing\n").toUtf8());
    writeFile(shared + "/hidden.desktop", entry("Hidden", "hidden", "Hidden=true\n").toUtf8());
    writeFile(shared + "/broken.desktop", "garbage\x01\x02");
    writeFile(system + "/org.test.linked/share/applications/org.test.linked.desktop",
        entry("Linked", qPrintable(launcher("org.test.linked")), "X-OpenWave-AppId=org.test.linked\nMimeType=text/csv;\n").toUtf8());
    QFile::link(system + "/org.test.linked/share/applications/org.test.linked.desktop", shared + "/org.test.linked.desktop");
    writeFile(system + "/org.test.linkclaim/share/applications/org.test.linkclaim.desktop",
        entry("Claim", qPrintable(launcher("org.test.victim")), "X-OpenWave-AppId=org.test.victim\nMimeType=text/csv;\n").toUtf8());
    QFile::link(system + "/org.test.linkclaim/share/applications/org.test.linkclaim.desktop", shared + "/org.test.linkclaim.desktop");
    writeFile(storage.filePath(".local/share/applications/userland.desktop"), entry("Userland", "userland", "MimeType=application/pdf;\n").toUtf8());
    writeFile(storage.filePath(".local/share/applications/org.test.viewer.desktop"),
        entry("Impostor", "/bin/evil", "X-OpenWave-AppId=org.test.viewer\nMimeType=application/pdf;\n").toUtf8());
    writeFile(storage.filePath(".local/share/applications/waydroid.com.example.reader.desktop"),
        entry("Reader", "waydroid app launch com.example.reader", "Categories=X-WayDroid-App;Office;\n").toUtf8());
    rescan(m);
    App *viewer = nullptr, *other = nullptr, *custom = nullptr, *editor = nullptr, *userland = nullptr;
    const auto reload = [&] {
        viewer = appd_lookup(&m, "org.test.viewer"); other = appd_lookup(&m, "org.test.other"); custom = appd_lookup(&m, "org.test.custom");
        editor = appd_app_for_desktop_id(&m, "editor.desktop"); userland = appd_app_for_desktop_id(&m, "userland.desktop");
    };
    reload();
    CHECK(viewer && !g_strcmp0(viewer->desktop_id, "org.test.viewer.desktop"));
    CHECK(viewer && !g_strcmp0(viewer->name, "Betrachter"));
    CHECK(viewer && viewer->visible && viewer->schemes && !g_strcmp0(viewer->schemes[0], "test"));
    CHECK(viewer && viewer->categories && !g_strcmp0(viewer->categories[0], "Graphics"));
    CHECK(viewer && g_strv_length(viewer->mime_types) == 3);
    CHECK(viewer && !g_strcmp0(viewer->argv[0], "/bin/true"));
    CHECK(other && other->visible && custom && !custom->visible);
    CHECK(appd_lookup(&m, "org.test.victim") && !g_strcmp0(appd_lookup(&m, "org.test.victim")->desktop_id, "org.test.victim.desktop"));
    CHECK(appd_lookup(&m, "org.test.victim") && !g_strcmp0(appd_lookup(&m, "org.test.victim")->name, "Victim"));
    CHECK(appd_lookup(&m, "org.test.linked") && !g_strcmp0(appd_lookup(&m, "org.test.linked")->desktop_id, "org.test.linked.desktop"));
    CHECK(appd_lookup(&m, "org.test.linked") && appd_lookup(&m, "org.test.linked")->visible && !appd_lookup(&m, "org.test.linked")->linux_app);
    CHECK(!appd_app_for_desktop_id(&m, "org.test.linkclaim.desktop") || appd_app_for_desktop_id(&m, "org.test.linkclaim.desktop")->linux_app);
    CHECK(appd_lookup(&m, "org.test.victim") && handlersOf(&m, "text/csv").contains("org.test.victim") == false);
    CHECK(!QFile::exists(user + "/org.test.nopaths/share/applications/forged.desktop") || !appd_app_for_desktop_id(&m, "forged.desktop"));
    CHECK(!appd_app_for_desktop_id(&m, "wrongexec.desktop") || appd_app_for_desktop_id(&m, "wrongexec.desktop")->linux_app);
    CHECK(appd_lookup(&m, "org.test.victim") && handlersOf(&m, "application/pdf").contains("org.test.victim") == false);
    CHECK(editor && editor->linux_app && !editor->visible);
    CHECK(editor && !g_strcmp0(appd_app_kind(editor), "linux"));
    CHECK(!appd_app_for_desktop_id(&m, "hidden.desktop"));
    CHECK(!appd_app_for_desktop_id(&m, "broken.desktop"));
    CHECK(userland && userland->linux_app);
    CHECK(!g_strcmp0(appd_app_for_desktop_id(&m, "org.test.viewer.desktop")->id, "org.test.viewer"));
    CHECK(!appd_handler_valid(&m, "pdf", editor->id));
    CHECK(!appd_handler_valid(&m, "pdf", userland->id));
    CHECK(!appd_handler_valid(&m, "pdf", "org.test.nopaths"));
    CHECK(handlersOf(&m, "application/pdf") == (QStringList{"org.test.viewer", "org.test.other"}));
    GError *error = nullptr;
    CHECK(!appd_set_file_default(&m, "pdf", editor->id, &error)); g_clear_error(&error);
    CHECK(!appd_set_file_default(&m, "pdf", userland->id, &error)); g_clear_error(&error);
    App *android = blank("org.openwave.android.com.example.reader", "Reader");
    android->android = TRUE; android->package = g_strdup("com.example.reader");
    g_strfreev(android->mime_types); android->mime_types = g_strsplit("application/pdf;application/epub+zip", ";", -1);
    android->schemes = g_strsplit("reader", ";", -1);
    m.android_apps = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(m.android_apps, android->id, android);
    appd_desktop_attach_android(&m);
    CHECK(!g_strcmp0(android->desktop_id, "waydroid.com.example.reader.desktop"));
    CHECK(android->categories && !g_strcmp0(android->categories[0], "X-WayDroid-App"));
    CHECK(handlersOf(&m, "application/pdf").contains("org.openwave.android.com.example.reader"));
    CHECK(handlersOf(&m, "application/epub+zip") == QStringList{"org.openwave.android.com.example.reader"});
    CHECK(appd_handler_valid(&m, "epub+zip", "x") == false);
    CHECK(appd_handler_valid(&m, "application/epub+zip", android->id));
    g_free(android->icon);
    android->icon = g_strdup("data:image/png;base64,iVBORw0KGgo=");
    App *tricky = blank("org.openwave.android.com.example.other", "Other\nExec=/bin/sh\\");
    tricky->android = TRUE; tricky->package = g_strdup("com.example.other");
    g_hash_table_insert(m.android_apps, tricky->id, tricky);
    const QString androidShare = QStringLiteral(APPD_ANDROID_ROOT "/share");
    const QString readerEntry = androidShare + "/applications/waydroid.com.example.reader.desktop";
    const QString otherEntry = androidShare + "/applications/waydroid.com.example.other.desktop";
    const QString readerIcon = androidShare + "/icons/waydroid.com.example.reader.png";
    appd_desktop_sync_android(&m);
    CHECK(QFile::exists(readerEntry) && QFile::exists(otherEntry) && QFile::exists(readerIcon));
    const QString entryText = readText(readerEntry);
    CHECK(entryText.contains("Exec=waydroid app launch com.example.reader\n"));
    CHECK(entryText.contains("Categories=X-WayDroid-App;\n"));
    CHECK(entryText.contains("MimeType=application/pdf;application/epub+zip;x-scheme-handler/reader;\n"));
    CHECK(entryText.contains("Icon=" + readerIcon + "\n"));
    CHECK(entryText.contains("X-OpenWave-AndroidPackage=com.example.reader\n"));
    const QString otherText = readText(otherEntry);
    int execLines = 0;
    for (const auto &line : otherText.split('\n')) execLines += line.startsWith("Exec=");
    CHECK(execLines == 1 && otherText.contains("Name=Other Exec=/bin/sh"));
    struct stat entryStat{};
    CHECK(!stat(qPrintable(readerEntry), &entryStat) && entryStat.st_uid == getuid() && (entryStat.st_mode & 0777) == 0644);
    CHECK(!stat(qPrintable(androidShare + "/applications"), &entryStat) && (entryStat.st_mode & 0022) == 0);
    rescan(m); reload();
    android = appd_lookup(&m, "org.openwave.android.com.example.reader");
    CHECK(android && android->categories && !g_strcmp0(android->categories[0], "X-WayDroid-App"));
    CHECK(!appd_app_for_desktop_id(&m, "waydroid.com.example.reader.desktop") || !appd_app_for_desktop_id(&m, "waydroid.com.example.reader.desktop")->linux_app);
    CHECK(handlersOf(&m, "application/epub+zip") == QStringList{"org.openwave.android.com.example.reader"});
    const qint64 entryTime = QFileInfo(readerEntry).lastModified().toMSecsSinceEpoch();
    appd_desktop_sync_android(&m);
    CHECK(QFileInfo(readerEntry).lastModified().toMSecsSinceEpoch() == entryTime);
    g_hash_table_steal(m.android_apps, tricky->id);
    appd_desktop_sync_android(&m);
    CHECK(QFile::exists(readerEntry) && !QFile::exists(otherEntry));
    appd_app_free(tricky);
    GHashTable *kept = m.android_apps;
    m.android_apps = g_hash_table_new(g_str_hash, g_str_equal);
    appd_desktop_sync_android(&m);
    CHECK(QFile::exists(readerEntry) && QFile::exists(readerIcon));
    g_hash_table_unref(m.android_apps);
    m.android_apps = kept;
    appd_desktop_attach_android(&m);
    QFile::remove(storage.filePath(".local/share/applications/waydroid.com.example.reader.desktop"));
    const QString mimeList = storage.filePath(".config/mimeapps.list");
    CHECK(appd_set_file_default(&m, "pdf", android->id, &error));
    CHECK(chosen(&m, "application/pdf") == android->id);
    CHECK(state(m, "application/pdf") == QString(android->id) + "|set");
    CHECK(readText(mimeList).contains("application/pdf=waydroid.com.example.reader.desktop"));
    g_hash_table_remove(m.android_apps, android->id);
    appd_desktop_attach_android(&m);
    CHECK(chosen(&m, "application/pdf").isEmpty());
    CHECK(state(m, "application/pdf") == "|unavailable");
    CHECK(readText(mimeList).contains("application/pdf=waydroid.com.example.reader.desktop"));
    g_hash_table_insert(m.android_apps, android->id, android);
    appd_desktop_attach_android(&m);
    CHECK(chosen(&m, "application/pdf") == android->id);
    App *browser = blank("org.openwave.android.org.example.browser", "Example Browser");
    browser->android = TRUE; browser->package = g_strdup("org.example.browser");
    browser->schemes = g_strsplit("http;https", ";", -1);
    App *mailer = blank("org.openwave.android.org.example.mail", "Example Mail");
    mailer->android = TRUE; mailer->package = g_strdup("org.example.mail");
    mailer->schemes = g_strsplit("mailto", ";", -1);
    App *dialer = blank("org.openwave.android.org.example.dialer", "Example Dialer");
    dialer->android = TRUE; dialer->package = g_strdup("org.example.dialer");
    dialer->schemes = g_strsplit("tel;sms", ";", -1);
    g_hash_table_insert(m.android_apps, browser->id, browser);
    g_hash_table_insert(m.android_apps, mailer->id, mailer);
    g_hash_table_insert(m.android_apps, dialer->id, dialer);
    appd_desktop_attach_android(&m);
    CHECK(g_strv_length(browser->categories) == 3 && !g_strcmp0(browser->categories[1], "Network") && !g_strcmp0(browser->categories[2], "WebBrowser"));
    CHECK(g_strv_length(mailer->categories) == 3 && !g_strcmp0(mailer->categories[2], "Email"));
    CHECK(g_strv_length(dialer->categories) == 3 && !g_strcmp0(dialer->categories[1], "Telephony") && !g_strcmp0(dialer->categories[2], "InstantMessaging"));
    CHECK(handlersOf(&m, "x-scheme-handler/http").contains(browser->id) && handlersOf(&m, "x-scheme-handler/https").contains(browser->id));
    CHECK(handlersOf(&m, "x-scheme-handler/mailto") == QStringList{mailer->id});
    CHECK(handlersOf(&m, "x-scheme-handler/tel") == QStringList{dialer->id} && handlersOf(&m, "x-scheme-handler/sms") == QStringList{dialer->id});
    CHECK(!handlersOf(&m, "x-scheme-handler/mailto").contains(browser->id));
    appd_desktop_sync_android(&m);
    const QString browserText = readText(androidShare + "/applications/waydroid.org.example.browser.desktop");
    CHECK(browserText.contains("Categories=X-WayDroid-App;Network;WebBrowser;\n"));
    CHECK(browserText.contains("MimeType=x-scheme-handler/http;x-scheme-handler/https;\n"));
    CHECK(appd_set_default_category(&m, "web-browser", browser->id, &error)); g_clear_error(&error);
    CHECK(readText(mimeList).contains("x-scheme-handler/http=waydroid.org.example.browser.desktop"));
    CHECK(readText(mimeList).contains("x-scheme-handler/https=waydroid.org.example.browser.desktop"));
    CHECK(!appd_set_default_category(&m, "web-browser", mailer->id, &error)); g_clear_error(&error);
    CHECK(appd_set_default_category(&m, "email", mailer->id, &error)); g_clear_error(&error);
    CHECK(appd_set_default_category(&m, "phone", dialer->id, &error)); g_clear_error(&error);
    CHECK(appd_set_default_category(&m, "messaging", dialer->id, &error)); g_clear_error(&error);
    CHECK(!appd_set_default_category(&m, "photos", browser->id, &error)); g_clear_error(&error);
    g_hash_table_steal(m.android_apps, browser->id); g_hash_table_steal(m.android_apps, mailer->id); g_hash_table_steal(m.android_apps, dialer->id);
    appd_desktop_attach_android(&m);
    appd_desktop_sync_android(&m);
    CHECK(!QFile::exists(androidShare + "/applications/waydroid.org.example.browser.desktop"));
    appd_app_free(browser); appd_app_free(mailer); appd_app_free(dialer);
    CHECK(appd_set_file_default(&m, "application/pdf", viewer->id, &error));
    CHECK(state(m, "application/pdf") == "org.test.viewer|set");
    CHECK(readText(mimeList).contains("application/pdf=org.test.viewer.desktop"));
    CHECK(appd_set_file_default(&m, "jpg", viewer->id, &error));
    CHECK(chosen(&m, "image/jpeg") == viewer->id);
    CHECK(appd_set_file_default(&m, "xlsx", viewer->id, &error));
    CHECK(appd_set_file_default(&m, "x-scheme-handler/test", viewer->id, &error));
    CHECK(chosen(&m, "x-scheme-handler/test") == viewer->id);
    CHECK(appd_set_file_default(&m, "image/png", other->id, &error));
    writeFile(mimeList, readText(mimeList).toUtf8() + "\n[Default Applications]\nimage/gif=org.test.other.desktop\n");
    int before = signalCount;
    rescan(m); reload();
    CHECK(signalCount > before);
    CHECK(readText(mimeList).contains("image/png=org.test.other.desktop"));
    CHECK(appd_clear_file_defaults(&m, viewer->id, &error));
    CHECK(chosen(&m, "application/pdf").isEmpty());
    CHECK(chosen(&m, "image/jpeg").isEmpty());
    CHECK(chosen(&m, "image/png") == other->id);
    CHECK(readText(mimeList).contains("image/png=org.test.other.desktop"));
    CHECK(!appd_set_file_default(&m, "pdf", "org.missing.app", &error)); g_clear_error(&error);
    CHECK(!appd_set_file_default(&m, "png", viewer->id, &error)); g_clear_error(&error);
    CHECK(!appd_set_file_default(&m, "../bad", viewer->id, &error)); g_clear_error(&error);
    CHECK(!appd_set_file_default(&m, "rpm", APPD_PACKAGE_INSTALLER, &error)); g_clear_error(&error);
    CHECK(!appd_set_file_default(&m, "apk", APPD_ANDROID_INSTALLER, &error)); g_clear_error(&error);
    CHECK(state(m, "rpm") == QString(APPD_PACKAGE_INSTALLER) + "|protected");
    CHECK(state(m, "apk") == QString(APPD_ANDROID_INSTALLER) + "|protected");
    writeFile(mimeList, "[Default Applications]\napplication/x-rpm=org.test.viewer.desktop\n");
    CHECK(chosen(&m, "application/x-rpm").isEmpty());
    CHECK(handlersOf(&m, "application/x-rpm") == QStringList{APPD_PACKAGE_INSTALLER});
    CHECK(appd_set_file_default(&m, "application/x-openwave-test", custom->id, &error));
    CHECK(chosen(&m, "application/x-openwave-test") == custom->id);
    CHECK(appd_clear_file_default(&m, "application/x-openwave-test", &error));
    CHECK(chosen(&m, "application/x-openwave-test").isEmpty());
    QString current;
    CHECK(hasCategory(m, "pdf", &current) && current.isEmpty());
    CHECK(hasCategory(m, "photos") && hasCategory(m, "web-browser") == false && hasCategory(m, "email") == false);
    CHECK(appd_set_default_category(&m, "pdf", viewer->id, &error));
    CHECK(hasCategory(m, "pdf", &current) && current == viewer->id);
    CHECK(appd_set_default_category(&m, "photos", other->id, &error));
    CHECK(chosen(&m, "image/png") == other->id && chosen(&m, "image/jpeg").isEmpty());
    CHECK(!appd_set_default_category(&m, "pdf", userland->id, &error)); g_clear_error(&error);
    CHECK(!appd_set_default_category(&m, "bogus", viewer->id, &error)); g_clear_error(&error);
    CHECK(appd_set_default_category(&m, "pdf", "", &error));
    CHECK(hasCategory(m, "pdf", &current) && current.isEmpty());
    GVariant *types = g_variant_ref_sink(appd_supported_types(&m, viewer->id, &error));
    GVariant *typeList = g_variant_get_child_value(types, 0), *schemeList = g_variant_get_child_value(types, 1);
    CHECK(g_variant_n_children(typeList) == 3 && g_variant_n_children(schemeList) == 1);
    g_variant_unref(typeList); g_variant_unref(schemeList); g_variant_unref(types);
    CHECK(appd_supported_types(&m, "org.missing", &error) == nullptr); g_clear_error(&error);
    CHECK(appd_set_file_default(&m, "x-scheme-handler/test", viewer->id, &error));
    GVariant *uri = g_variant_ref_sink(appd_resolve_uri(&m, "TEST://example/path", &error));
    const char *uriType, *uriChosen; g_variant_get(uri, "(&s&s@as)", &uriType, &uriChosen, nullptr);
    CHECK(!strcmp(uriType, "x-scheme-handler/test") && !strcmp(uriChosen, "org.test.viewer"));
    g_variant_unref(uri);
    CHECK(appd_resolve_uri(&m, "no scheme here", &error) == nullptr); g_clear_error(&error);
    CHECK(chosen(&m, "x-scheme-handler/test") == viewer->id);
    QFile::remove(APPD_DEFAULTS_DIRECTORY "/file-defaults.ini");
    QByteArray legacyData = "[Defaults]\npdf=org.test.viewer\napplication/x-openwave-test=org.test.custom\njpg=org.future.absent\nxlsx=org.test.other\n";
    char *perUser = g_strdup_printf(APPD_DEFAULTS_DIRECTORY "/file-defaults-%u.ini", m.uid);
    writeFile(perUser, legacyData); chmod(perUser, 0600);
    QFile::remove(mimeList);
    rescan(m); reload();
    CHECK(chosen(&m, "application/pdf") == viewer->id);
    CHECK(chosen(&m, "application/x-openwave-test") == custom->id);
    CHECK(QFile::exists(perUser));
    CHECK(readText(perUser).contains("org.future.absent") && !readText(perUser).contains("org.test.viewer"));
    CHECK(!chosen(&m, "image/jpeg").isEmpty() == false);
    bundleDefinitions.insert("org.future.absent", true);
    writeFile(system + "/org.future.absent/share/applications/org.future.absent.desktop",
        entry("Absent", qPrintable(launcher("org.future.absent")), "X-OpenWave-AppId=org.future.absent\nMimeType=image/jpeg;\n").toUtf8());
    rescan(m); reload();
    CHECK(chosen(&m, "image/jpeg") == "org.future.absent");
    CHECK(!QFile::exists(perUser) && QFile::exists(QString(perUser) + ".migrated"));
    struct stat migrated{}; CHECK(!stat(qPrintable(mimeList), &migrated) && migrated.st_uid == getuid());
    g_free(perUser);
    other->removing = TRUE;
    CHECK(chosen(&m, "application/pdf") == viewer->id);
    CHECK(appd_set_file_default(&m, "pdf", other->id, &error) == FALSE); g_clear_error(&error);
    other->removing = FALSE;
    g_hash_table_remove(m.apps, other->id);
    CHECK(state(m, "application/pdf") == "org.test.viewer|set");
    CHECK(appd_set_file_default(&m, "gif", viewer->id, &error) == FALSE); g_clear_error(&error);
    const auto file = storage.filePath("renamed.txt");
    CHECK(detects(file, "%PDF-1.7\n", "application/pdf"));
    CHECK(detects(storage.filePath("photo.pdf"), QByteArray::fromHex("89504e470d0a1a0a0000000d49484452"), "image/png"));
    CHECK(detects(storage.filePath("photo.jpeg"), QByteArray::fromHex("ffd8ffe000104a4649460001"), "image/jpeg"));
    CHECK(detects(storage.filePath("note.txt"), "hello\n", "text/plain"));
    CHECK(detects(storage.filePath("image.gif"), "GIF89a", "image/gif"));
    CHECK(detects(storage.filePath("vector.svg"), "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>", "image/svg+xml"));
    CHECK(detects(storage.filePath("image.webp"), QByteArray("RIFF\x20\0\0\0WEBPVP8 ", 16), "image/webp"));
    const QByteArray zip = QByteArray::fromHex("504b0304140000000000000000000000000000000000000000000000000000");
    CHECK(detects(storage.filePath("doc.docx"), zip, "application/vnd.openxmlformats-officedocument.wordprocessingml.document"));
    CHECK(detects(storage.filePath("sheet.xlsx"), zip, "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"));
    CHECK(detects(storage.filePath("doc.odt"), zip, "application/vnd.oasis.opendocument.text"));
    CHECK(detects(storage.filePath("archive.zip"), zip, "application/zip"));
    QByteArray tar(512, '\0'); tar.replace(257, 5, "ustar");
    CHECK(detects(storage.filePath("archive.tar"), tar, "application/x-tar"));
    CHECK(detects(storage.filePath("archive.gz"), QByteArray::fromHex("1f8b0800000000000003"), "application/gzip"));
    CHECK(detects(storage.filePath("wrong.rpm"), "malformed", "application/x-rpm"));
    CHECK(detects(storage.filePath("wrong.apk"), "malformed", "application/vnd.android.package-archive"));
    CHECK(detects(storage.filePath("renamed.bin"), QByteArray::fromHex("edabeedb0300"), "application/x-rpm"));
    CHECK(detects(storage.filePath("typo.xslx"), "hello", "xslx"));
    auto *resolved = appd_resolve_file(&m, file.toUtf8().constData(), &error);
    CHECK(resolved != nullptr);
    if (resolved) { const char *type, *id; GVariant *ids; g_variant_get(resolved, "(&s&s@as)", &type, &id, &ids);
        CHECK(!strcmp(type, "application/pdf")); CHECK(!strcmp(id, "org.test.viewer")); CHECK(g_variant_n_children(ids) == 2);
        g_variant_unref(ids); g_variant_ref_sink(resolved); g_variant_unref(resolved); }
    CHECK(appd_resolve_file(&m, "/etc/passwd", &error) == nullptr); g_clear_error(&error);
    QFile::link(file, storage.filePath("link"));
    CHECK(!appd_resolve_file(&m, storage.filePath("link").toUtf8().constData(), &error)); g_clear_error(&error);
    mkfifo(storage.filePath("pipe").toUtf8().constData(), 0600);
    CHECK(!appd_resolve_file(&m, storage.filePath("pipe").toUtf8().constData(), &error)); g_clear_error(&error);
    const auto hidden = storage.filePath(".private"); CHECK(detects(hidden, "hello", "private"));
    CHECK(!appd_resolve_file(&m, hidden.toUtf8().constData(), &error)); g_clear_error(&error);
    QTemporaryDir runtime;
    Instance instance{}; instance.uid = getuid();
    const auto runtimeBytes = runtime.path().toUtf8(); instance.runtime = const_cast<char *>(runtimeBytes.constData());
    char *copy = appd_deliver_file(&instance, file.toUtf8().constData(), &error);
    CHECK(copy != nullptr);
    if (copy) {
        QFile contents(QString::fromUtf8(copy)); CHECK(contents.open(QIODevice::ReadOnly));
        CHECK(contents.readAll() == "%PDF-1.7\n");
        CHECK(QString::fromUtf8(copy).startsWith(runtime.path() + "/OpenWith-"));
        struct stat copied{}; CHECK(!stat(copy, &copied) && copied.st_uid == getuid() && (copied.st_mode & 0777) == 0600);
        g_free(copy);
    }
    CHECK(!appd_deliver_file(&instance, storage.filePath("link").toUtf8().constData(), &error)); g_clear_error(&error);
    CHECK(!appd_deliver_file(&instance, storage.filePath("pipe").toUtf8().constData(), &error)); g_clear_error(&error);
    CHECK(signalCount >= 8);
    g_free(m.home); g_hash_table_unref(m.apps);
    printf("%d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
