#include "../../preferences.h"
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
int failures = 0;

void check(bool condition, const char *what)
{
    if (!condition) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; }
}

bool apply(const QString &path, AppdPreferenceKind kind, const char *key, bool boolean = false,
           const char *string = nullptr, const char *const *list = nullptr)
{
    AppdPreferenceChange change{kind, key, boolean ? TRUE : FALSE, string, list};
    GError *error = nullptr;
    const bool ok = appd_preferences_apply(path.toUtf8().constData(), geteuid(), getegid(), &change, &error);
    if (error) g_error_free(error);
    return ok;
}

int snapshots = 0;

std::unique_ptr<QSettings> open(const QString &path)
{
    const QString copy = path + QStringLiteral(".snapshot%1").arg(++snapshots);
    QFile::copy(path, copy);
    return std::make_unique<QSettings>(copy, QSettings::IniFormat);
}

QString slurp(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}
}

int main()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/OpenWave/settings.ini");

    check(appd_preference_valid_id("wifi"), "plain id");
    check(appd_preference_valid_id("com.vendor.my-applet_2"), "dotted id");
    check(!appd_preference_valid_id(""), "empty id");
    check(!appd_preference_valid_id(".hidden"), "dot id");
    check(!appd_preference_valid_id("a/b"), "slash id");
    check(!appd_preference_valid_id("a=b"), "equals id");
    check(!appd_preference_valid_id("a b"), "space id");
    check(!appd_preference_valid_id(std::string(65, 'a').c_str()), "long id");

    check(appd_preference_allowed(APPD_PREFERENCE_BOOL, "accessibility/reduceMotion"), "reduceMotion allowed");
    check(appd_preference_allowed(APPD_PREFERENCE_BOOL, "quickmenu/applets/state/wifi"), "applet state allowed");
    check(!appd_preference_allowed(APPD_PREFERENCE_BOOL, "quickmenu/applets/state/"), "empty state id");
    check(!appd_preference_allowed(APPD_PREFERENCE_BOOL, "quickmenu/applets/state/a/b"), "nested state id");
    check(!appd_preference_allowed(APPD_PREFERENCE_BOOL, "device/name"), "wrong kind");
    check(!appd_preference_allowed(APPD_PREFERENCE_BOOL, "wallpaper/path"), "unlisted key");
    check(!appd_preference_allowed(APPD_PREFERENCE_STRING, "accessibility/largeText"), "bool as string");
    check(!appd_preference_allowed(APPD_PREFERENCE_RESET, "accessibility"), "reset unlisted group");
    check(appd_preference_allowed(APPD_PREFERENCE_RESET, "quickmenu/applets"), "reset applets");

    check(apply(path, APPD_PREFERENCE_BOOL, "accessibility/reduceMotion", true), "write reduceMotion");
    check(QDir(dir.path() + QStringLiteral("/OpenWave")).exists(), "directory created");
    struct stat st;
    check(!stat(path.toUtf8().constData(), &st) && (st.st_mode & 0777) == 0600, "mode 0600");
    {
        auto holder = open(path); QSettings &s = *holder;
        check(s.value(QStringLiteral("accessibility/reduceMotion")).toBool(), "QSettings reads reduceMotion");
    }

    check(apply(path, APPD_PREFERENCE_BOOL, "quickmenu/applets/state/wifi", false), "write state");
    check(apply(path, APPD_PREFERENCE_BOOL, "quickmenu/applets/state/lock", true), "write state 2");
    const char *order[] = {"lock", "wifi", "bluetooth", nullptr};
    check(apply(path, APPD_PREFERENCE_LIST, "quickmenu/applets/order", false, nullptr, order), "write order");
    {
        auto holder = open(path); QSettings &s = *holder;
        check(!s.value(QStringLiteral("quickmenu/applets/state/wifi"), true).toBool(), "QSettings state wifi");
        check(s.value(QStringLiteral("quickmenu/applets/state/lock"), false).toBool(), "QSettings state lock");
        check(s.value(QStringLiteral("quickmenu/applets/order")).toStringList()
              == QStringList({QStringLiteral("lock"), QStringLiteral("wifi"), QStringLiteral("bluetooth")}), "QSettings order");
        check(s.value(QStringLiteral("accessibility/reduceMotion")).toBool(), "other key preserved");
    }

    const char *names[] = {"My Phone", "A, B; \"C\"", "  padded  ", "Téléphone", "back\\slash", nullptr};
    for (int i = 0; names[i]; ++i) {
        check(apply(path, APPD_PREFERENCE_STRING, "device/name", false, names[i]), names[i]);
        auto holder = open(path); QSettings &s = *holder;
        check(s.value(QStringLiteral("device/name")).toString() == QString::fromUtf8(names[i]), names[i]);
    }

    const std::string tooLong(65, 'x');
    check(!apply(path, APPD_PREFERENCE_STRING, "device/name", false, tooLong.c_str()), "name too long");
    check(!apply(path, APPD_PREFERENCE_STRING, "device/name", false, ""), "empty name");
    check(!apply(path, APPD_PREFERENCE_STRING, "device/name", false, "line\nbreak"), "newline name");
    check(!apply(path, APPD_PREFERENCE_STRING, "device/name", false, "\xff\xfe"), "invalid utf8 name");
    {
        auto holder = open(path); QSettings &s = *holder;
        check(s.value(QStringLiteral("device/name")).toString() == QStringLiteral("back\\slash"), "rejected writes keep value");
    }

    const char *bad[] = {"wifi", "../evil", nullptr};
    check(!apply(path, APPD_PREFERENCE_LIST, "quickmenu/applets/order", false, nullptr, bad), "bad id in list");
    check(!apply(path, APPD_PREFERENCE_BOOL, "wallpaper/path", true), "unlisted key refused");

    check(apply(path, APPD_PREFERENCE_RESET, "quickmenu/applets"), "reset applets");
    {
        auto holder = open(path); QSettings &s = *holder;
        check(!s.contains(QStringLiteral("quickmenu/applets/order")), "order gone");
        check(!s.contains(QStringLiteral("quickmenu/applets/state/wifi")), "state gone");
        check(s.value(QStringLiteral("accessibility/reduceMotion")).toBool(), "accessibility survives reset");
        check(s.contains(QStringLiteral("device/name")), "device survives reset");
    }
    check(!slurp(path).contains(QStringLiteral("quickmenu")), "empty group removed");

    check(apply(path, APPD_PREFERENCE_LIST, "quickmenu/applets/order", false, nullptr, order), "write order again");
    const char *empty[] = {nullptr};
    check(apply(path, APPD_PREFERENCE_LIST, "quickmenu/applets/order", false, nullptr, empty), "clear order");
    check(appd_preference_allowed(APPD_PREFERENCE_LIST, "appearance/favorites"), "favorites allowed");
    check(!appd_preference_allowed(APPD_PREFERENCE_BOOL, "appearance/favorites"), "favorites not bool");
    check(!appd_preference_allowed(APPD_PREFERENCE_LIST, "appearance/current"), "current not writable");
    const char *favorites[] = {"purple", "qa-sunset", nullptr};
    check(apply(path, APPD_PREFERENCE_LIST, "appearance/favorites", false, nullptr, favorites), "write favorites");
    {
        auto holder = open(path); QSettings &s = *holder;
        check(s.value(QStringLiteral("appearance/favorites")).toStringList()
              == QStringList({QStringLiteral("purple"), QStringLiteral("qa-sunset")}), "QSettings favorites");
    }
    const char *badFavorite[] = {"purple", "../x", nullptr};
    check(!apply(path, APPD_PREFERENCE_LIST, "appearance/favorites", false, nullptr, badFavorite), "bad favorite id");
    check(!slurp(path).contains(QStringLiteral("order")), "empty list removes key");

    const QString junk = dir.path() + QStringLiteral("/junk/settings.ini");
    QDir().mkpath(dir.path() + QStringLiteral("/junk"));
    QFile broken(junk);
    check(broken.open(QIODevice::WriteOnly) && broken.write("this is = not [ valid\n[unterminated\n") > 0, "write broken");
    broken.close();
    check(!apply(junk, APPD_PREFERENCE_BOOL, "accessibility/largeText", true), "refuses to clobber unreadable file");
    check(slurp(junk).startsWith(QStringLiteral("this is")), "unreadable file untouched");

    const QString link = dir.path() + QStringLiteral("/linked/settings.ini");
    QDir().mkpath(dir.path() + QStringLiteral("/linked"));
    QFile::link(dir.path() + QStringLiteral("/OpenWave/settings.ini"), link);
    const QString before = slurp(path);
    check(!apply(link, APPD_PREFERENCE_BOOL, "accessibility/largeText", true), "symlinked preferences refused");
    check(slurp(path) == before, "symlink target untouched");

    GKeyFile *file = g_key_file_new();
    g_key_file_set_boolean(file, "accessibility", "reduceMotion", TRUE);
    g_key_file_set_value(file, "device", "name", "Phone");
    g_key_file_set_value(file, "device", "secret", "no");
    g_key_file_set_value(file, "quickmenu", "applets\\state\\wifi", "false");
    g_key_file_set_value(file, "other", "x", "y");
    g_key_file_set_value(file, "appearance", "favorites", "a, b");
    g_key_file_set_value(file, "appearance", "current", "a");
    gsize length = 0;
    char *shared = appd_preferences_public(file, &length);
    GKeyFile *copy = g_key_file_new();
    check(g_key_file_load_from_data(copy, shared, length, G_KEY_FILE_NONE, nullptr), "mirror parses");
    check(g_key_file_get_boolean(copy, "accessibility", "reduceMotion", nullptr), "mirror reduceMotion");
    check(!g_key_file_get_boolean(copy, "accessibility", "largeText", nullptr), "mirror largeText defaults false");
    char *name = g_key_file_get_value(copy, "device", "name", nullptr);
    check(name && !strcmp(name, "Phone"), "mirror device name");
    g_free(name);
    check(!g_key_file_has_key(copy, "device", "secret", nullptr), "mirror leaves other device keys");
    check(!g_key_file_has_group(copy, "other"), "mirror leaves other groups");
    char *mirroredFavorites = g_key_file_get_value(copy, "appearance", "favorites", nullptr);
    check(mirroredFavorites && !strcmp(mirroredFavorites, "a, b"), "mirror favorites");
    g_free(mirroredFavorites);
    check(!g_key_file_has_key(copy, "appearance", "current", nullptr), "mirror leaves current");
    char *state = g_key_file_get_value(copy, "quickmenu", "applets\\state\\wifi", nullptr);
    check(state && !strcmp(state, "false"), "mirror quickmenu");
    g_free(state); g_free(shared); g_key_file_unref(copy); g_key_file_unref(file);

    if (failures) { std::fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    std::puts("appd preference tests passed");
    return 0;
}
