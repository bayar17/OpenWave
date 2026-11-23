#include "FileOpener.h"
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QPointer>
#include <QRegularExpression>
#include <QStringList>
#include <fcntl.h>
#undef signals
#include <gio/gio.h>
#include <OpenWave/OpenWaveIdentity.h>
#include <gio/gunixfdlist.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>

namespace {
constexpr qint64 MaximumPackageBytes = 1LL << 30;

QString builtinName(const QString &id)
{
    if (id == QLatin1String(FileOpener::PackageInstaller)) return QObject::tr("Package Installer");
    if (id == QLatin1String("system.android-installer")) return QObject::tr("Android App Installer");
    return {};
}

struct InstallResult {
    int status = 1;
    QStringList lines;
    QString failure;
};

InstallResult runInstall(int descriptor, const QString &name, bool android)
{
    InstallResult result;
    GError *error = nullptr;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
    if (!bus) {
        result.failure = QString::fromUtf8(error ? error->message : "The system bus is unavailable.");
        g_clear_error(&error);
        close(descriptor);
        return result;
    }
    char *owner = openwave_service_owner(bus, "org.openwave.Applications", "/run/openwave/appd.pid", &error);
    if (!owner) {
        result.failure = QString::fromUtf8(error ? error->message : "The package service could not be verified.");
        g_clear_error(&error); close(descriptor); g_object_unref(bus); return result;
    }
    GUnixFDList *list = g_unix_fd_list_new();
    const gint index = g_unix_fd_list_append(list, descriptor, &error);
    close(descriptor);
    if (index < 0) {
        result.failure = QString::fromUtf8(error ? error->message : "The package could not be handed over.");
        g_clear_error(&error);
        g_free(owner);
        g_object_unref(list);
        g_object_unref(bus);
        return result;
    }
    GVariantBuilder strings, options, indexes;
    g_variant_builder_init(&strings, G_VARIANT_TYPE("as"));
    g_variant_builder_add(&strings, "s", name.toUtf8().constData());
    g_variant_builder_init(&options, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&options, "{sv}", "client", g_variant_new_string("files"));
    g_variant_builder_init(&indexes, G_VARIANT_TYPE("ah"));
    g_variant_builder_add(&indexes, "h", index);
    GVariant *parameters = g_variant_new("(s@as@a{sv}@ah)", android ? "install-android" : "install", g_variant_builder_end(&strings),
                                         g_variant_builder_end(&options), g_variant_builder_end(&indexes));
    GVariant *reply = g_dbus_connection_call_with_unix_fd_list_sync(bus, owner,
        "/org/openwave/Applications", "org.openwave.AppDeploy1", "Run", parameters, G_VARIANT_TYPE("(ias)"),
        G_DBUS_CALL_FLAGS_NONE, 3600 * 1000, list, nullptr, nullptr, &error);
    g_free(owner);
    g_object_unref(list);
    if (!reply) {
        result.failure = QString::fromUtf8(error ? error->message : "The installer did not answer.");
        g_clear_error(&error);
        g_object_unref(bus);
        return result;
    }
    gint32 status = 1;
    GVariant *lines = nullptr;
    g_variant_get(reply, "(i@as)", &status, &lines);
    result.status = status;
    GVariantIter iterator;
    g_variant_iter_init(&iterator, lines);
    const char *line = nullptr;
    while (g_variant_iter_next(&iterator, "&s", &line)) result.lines.append(QString::fromUtf8(line));
    g_variant_unref(lines);
    g_variant_unref(reply);
    g_object_unref(bus);
    return result;
}
}

FileOpener::FileOpener(QObject *parent) : QObject(parent)
{
    connect(&m_client, &ApplicationsClient::registryChanged, this, [this] { if (!m_path.isEmpty()) prepare(m_path); });
    connect(&m_client, &ApplicationsClient::fileDefaultsChanged, this, [this] { if (!m_path.isEmpty()) prepare(m_path); });
}

QString FileOpener::extensionOf(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

QString FileOpener::summarize(int status, const QStringList &lines)
{
    QString result, problem;
    for (const auto &line : lines) {
        const QStringList fields = line.split(QLatin1Char('\t'));
        if (fields.value(0) == QLatin1String("R") && !fields.value(1).isEmpty()) result = fields.value(1);
        else if (fields.value(0) == QLatin1String("E") && !fields.value(1).isEmpty()) problem = fields.value(1);
    }
    if (status == 0) return result.isEmpty() ? tr("The package was installed.") : result;
    return problem.isEmpty() ? tr("The package was not installed.") : tr("Not installed: %1").arg(problem);
}

void FileOpener::setMessage(const QString &message)
{
    if (m_message == message) return;
    m_message = message;
    emit messageChanged();
}

void FileOpener::clearMessage()
{
    setMessage({});
}

void FileOpener::prepare(const QString &path)
{
    const quint64 generation = ++m_generation;
    m_path = path;
    m_extension = extensionOf(path);
    m_ready = false;
    m_handlers.clear();
    m_effectiveId.clear();
    m_effectiveName.clear();
    emit changed();
    m_client.call("ResolveFile", {path}, [this, generation](const QVariantList &associations, const QString &error) {
        if (generation != m_generation) return;
        if (!error.isEmpty()) { setMessage(error); m_ready = true; emit changed(); return; }
        m_extension = associations.value(0).toString();
        const QString chosen = associations.value(1).toString();
        const QStringList ids = associations.value(2).toStringList();
        m_client.listApplications([this, generation, ids, chosen](const QVariantList &values, const QString &) {
            if (generation != m_generation) return;
            QMap<QString, QString> names;
            for (const auto &entry : values.value(0).toList()) {
                const auto application = entry.toMap();
                names.insert(application.value("appId").toString(), application.value("name").toString());
            }
            const auto nameOf = [&](const QString &id) { const QString builtin = builtinName(id); return builtin.isEmpty() ? names.value(id, id) : builtin; };
            for (const auto &id : ids) m_handlers.append(QVariantMap{{"id", id}, {"name", nameOf(id)}});
            if (!chosen.isEmpty() && ids.contains(chosen)) m_effectiveId = chosen;
            m_effectiveName = m_effectiveId.isEmpty() ? QString() : nameOf(m_effectiveId);
            m_ready = true;
            emit changed();
        });
    });
}

void FileOpener::open(const QString &path, const QString &handlerId, bool always)
{
    if (m_busy || handlerId.isEmpty()) return;
    if (handlerId == QLatin1String(PackageInstaller) || handlerId == QLatin1String("system.android-installer")) { install(path, handlerId); return; }
    m_busy = true; emit busyChanged();
    m_client.call("OpenFile", {path, handlerId, always}, [this](const QVariantList &, const QString &error) {
        m_busy = false; emit busyChanged();
        if (error.isEmpty()) emit opened();
        else setMessage(tr("The app could not open this file: %1").arg(error));
    });
}

void FileOpener::install(const QString &path, const QString &handlerId)
{
    const QByteArray encoded = QFile::encodeName(path);
    const int descriptor = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    struct stat st;
    if (descriptor < 0 || fstat(descriptor, &st) || !S_ISREG(st.st_mode)) {
        if (descriptor >= 0) ::close(descriptor);
        setMessage(tr("This package cannot be read."));
        return;
    }
    if (st.st_size > MaximumPackageBytes) {
        ::close(descriptor);
        setMessage(tr("This package is too large to install."));
        return;
    }
    const QString name = QFileInfo(path).fileName();
    m_busy = true;
    setMessage(tr("Waiting for your confirmation…"));
    emit busyChanged();
    emit opened();
    QPointer<FileOpener> guard(this);
    const bool android = handlerId == QLatin1String("system.android-installer");
    std::thread([guard, descriptor, name, android] {
        const InstallResult result = runInstall(descriptor, name, android);
        if (!guard) return;
        QMetaObject::invokeMethod(guard.data(), [guard, result] {
            if (!guard) return;
            guard->m_busy = false;
            guard->setMessage(result.failure.isEmpty() ? summarize(result.status, result.lines) : tr("Not installed: %1").arg(result.failure));
            emit guard->busyChanged();
        }, Qt::QueuedConnection);
    }).detach();
}
