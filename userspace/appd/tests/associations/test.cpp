#include "../../appd.h"
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>

static int checks, failures, signalCount;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); } } while (0)
extern "C" void appd_emit(Manager *, const char *, GVariant *) { ++signalCount; }
static QString chosen(Manager *m, const char *key) {
    auto *all = g_variant_ref_sink(appd_file_associations(m));
    auto *defaults = g_variant_get_child_value(all, 0);
    const char *value = ""; g_variant_lookup(defaults, key, "&s", &value);
    QString result = QString::fromUtf8(value);
    g_variant_unref(defaults); g_variant_unref(all); return result;
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
    QDir(APPD_DEFAULTS_DIRECTORY).removeRecursively();
    QDir().mkpath(APPD_DEFAULTS_DIRECTORY);
    QTemporaryDir storage;
    Manager m{}; m.uid = getuid(); m.home = g_strdup(storage.path().toUtf8().constData());
    m.apps = g_hash_table_new(g_str_hash, g_str_equal);
    App viewer{}, other{}, custom{};
    char *opens[] = {(char *)"pdf", (char *)"jpg", (char *)"xlsx", nullptr};
    char *mimes[] = {(char *)"application/pdf", (char *)"image/jpeg", (char *)"image/png", nullptr};
    char *customTypes[] = {(char *)"application/x-openwave-test", nullptr};
    viewer.id = (char *)"org.test.viewer"; viewer.opens = opens; viewer.paths = TRUE;
    other.id = (char *)"org.test.other"; other.mime_types = mimes; other.paths = TRUE;
    custom.id = (char *)"org.test.custom"; custom.mime_types = customTypes; custom.paths = TRUE;
    g_hash_table_insert(m.apps, viewer.id, &viewer); g_hash_table_insert(m.apps, other.id, &other); g_hash_table_insert(m.apps, custom.id, &custom);
    GError *error = nullptr;
    QFile legacy(QStringLiteral(APPD_DEFAULTS_DIRECTORY "/file-defaults.ini"));
    CHECK(legacy.open(QIODevice::WriteOnly)); legacy.write("[Defaults]\npdf=org.test.viewer\n"); legacy.close();
    legacy.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    appd_prune_file_defaults(&m);
    CHECK(chosen(&m, "application/pdf") == viewer.id);
    CHECK(QFile::exists(QStringLiteral(APPD_DEFAULTS_DIRECTORY "/file-defaults.legacy")));
    CHECK(!legacy.exists());
    appd_clear_file_defaults(&m, viewer.id, &error); g_clear_error(&error);
    appd_clear_file_defaults(&m, other.id, &error); g_clear_error(&error);
    CHECK(chosen(&m, "application/pdf").isEmpty()); // first open, even one compatible handler
    CHECK(appd_handler_valid(&m, "pdf", viewer.id));
    CHECK(appd_handler_valid(&m, "jpeg", viewer.id));
    CHECK(!appd_handler_valid(&m, "xslx", viewer.id));
    CHECK(appd_handler_valid(&m, "xlsx", viewer.id));
    CHECK(appd_set_file_default(&m, "pdf", viewer.id, &error));
    CHECK(chosen(&m, "application/pdf") == viewer.id);
    Manager restarted = m; CHECK(chosen(&restarted, "application/pdf") == viewer.id); // persistence
    Manager second = m; second.uid += 1;
    CHECK(chosen(&second, "application/pdf").isEmpty());
    CHECK(appd_set_file_default(&m, "application/pdf", other.id, &error));
    CHECK(chosen(&m, "application/pdf") == other.id); // same key for both UIs
    CHECK(!appd_set_file_default(&m, "pdf", "org.missing.app", &error)); g_clear_error(&error);
    CHECK(!appd_set_file_default(&m, "png", viewer.id, &error)); g_clear_error(&error);
    CHECK(!appd_set_file_default(&m, "../bad", viewer.id, &error)); g_clear_error(&error);
    CHECK(!appd_set_file_default(&m, "rpm", APPD_PACKAGE_INSTALLER, &error)); g_clear_error(&error);
    CHECK(!appd_set_file_default(&m, "apk", APPD_ANDROID_INSTALLER, &error)); g_clear_error(&error);
    CHECK(appd_set_file_default(&m, "application/x-openwave-test", custom.id, &error));
    CHECK(chosen(&m, "application/x-openwave-test") == custom.id);
    CHECK(appd_set_file_default(&m, "jpg", viewer.id, &error));
    CHECK(chosen(&m, "image/jpeg") == viewer.id);
    CHECK(appd_clear_file_defaults(&m, viewer.id, &error));
    CHECK(chosen(&m, "image/jpeg").isEmpty());
    CHECK(chosen(&m, "application/pdf") == other.id);
    other.removing = TRUE; appd_prune_file_defaults(&m);
    CHECK(chosen(&m, "application/pdf").isEmpty());
    other.removing = FALSE; CHECK(chosen(&m, "application/pdf") == other.id); // failed uninstall keeps preferences
    g_hash_table_remove(m.apps, other.id); appd_prune_file_defaults(&m);
    g_hash_table_insert(m.apps, other.id, &other); CHECK(chosen(&m, "application/pdf").isEmpty());
    CHECK(appd_set_file_default(&m, "pdf", viewer.id, &error));
    g_hash_table_remove(m.apps, viewer.id); appd_prune_file_defaults(&m);
    g_hash_table_insert(m.apps, viewer.id, &viewer);
    CHECK(chosen(&m, "application/pdf").isEmpty());
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
        CHECK(!strcmp(type, "application/pdf")); CHECK(!*id); CHECK(g_variant_n_children(ids) == 2);
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
    char *path = g_strdup_printf(APPD_DEFAULTS_DIRECTORY "/file-defaults-%u.ini", m.uid);
    struct stat st{}; CHECK(!stat(path, &st) && !(st.st_mode & 0077));
    g_free(path); g_free(m.home); g_hash_table_unref(m.apps);
    printf("%d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
}
