#include "ApplicationsBackend.h"
#include <QCollator>
#include <QLocale>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <algorithm>

ApplicationsBackend::ApplicationsBackend(QObject *parent, bool handleDetailsRequests) : QObject(parent)
{
    connect(&m_prompt, &OpenWavePromptClient::busyChanged, this, &ApplicationsBackend::changed);
    connect(&m_prompt, &OpenWavePromptClient::errorOccurred, this, [this](const QString &error) { m_uninstallError = error; emit changed(); });
    connect(&m_prompt, &OpenWavePromptClient::finished, this, [this](const QString &, const QString &result, const QString &) {
        const QString id = m_pendingUninstall;
        m_pendingUninstall.clear();
        if (result != "accepted" || id.isEmpty()) return;
        m_uninstalling = true; m_uninstallError.clear(); emit changed();
        QPointer<ApplicationsBackend> guard(this);
        m_client.uninstallApplication(id, [guard, id](const QVariantList &, const QString &error) {
            if (!guard) return;
            guard->m_uninstalling = false;
            guard->m_uninstallError = error;
            emit guard->changed();
            if (error.isEmpty()) { emit guard->uninstalled(id); guard->refresh(); }
        });
    });
    if (handleDetailsRequests) {
    connect(&m_client, &ApplicationsClient::applicationDetailsRequested, this, &ApplicationsBackend::detailsRequested);
    QMetaObject::invokeMethod(this, [this] {
        m_client.takeApplicationDetailsRequest([this](const QVariantList &values, const QString &error) {
            const QString id = values.value(0).toString();
            if (error.isEmpty() && !id.isEmpty()) emit detailsRequested(id);
        });
    }, Qt::QueuedConnection);
    }
    connect(&m_client, &ApplicationsClient::registryChanged, this, &ApplicationsBackend::refresh);
    connect(&m_client, &ApplicationsClient::registryChanged, this, &ApplicationsBackend::refreshFileTypes);
    connect(&m_client, &ApplicationsClient::fileDefaultsChanged, this, &ApplicationsBackend::refreshFileTypes);
    connect(&m_client, &ApplicationsClient::serviceChanged, this, [this](bool) { refresh(); refreshFileTypes(); });
}

void ApplicationsBackend::refresh()
{
    if (m_busy) { m_again = true; return; }
    m_busy = true; m_error.clear(); emit changed();
    m_client.listApplications([this](const QVariantList &values, const QString &error) {
        m_error = error;
        m_applications = error.isEmpty() ? values.value(0).toList() : QVariantList();
        QCollator collator; collator.setCaseSensitivity(Qt::CaseInsensitive); collator.setNumericMode(true);
        std::sort(m_applications.begin(), m_applications.end(), [&](const QVariant &a, const QVariant &b) {
            const auto first = a.toMap(), second = b.toMap();
            const int result = collator.compare(first.value("name").toString(), second.value("name").toString());
            return result ? result < 0 : first.value("appId").toString() < second.value("appId").toString();
        });
        m_busy = false; emit changed();
        if (m_associationsLoaded) rebuildFileTypes();
        if (m_again) { m_again = false; refresh(); }
    });
}

namespace {
const QMap<QString, QString> commonTypes{
    {"application/vnd.openxmlformats-officedocument.wordprocessingml.document", ".docx"},
    {"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet", ".xlsx"},
    {"application/pdf", ".pdf"}, {"text/plain", ".txt"}, {"application/vnd.oasis.opendocument.text", ".odt"},
    {"image/png", ".png"}, {"image/jpeg", ".jpg, .jpeg"}, {"image/webp", ".webp"},
    {"image/gif", ".gif"}, {"image/svg+xml", ".svg"}, {"application/zip", ".zip"},
    {"application/x-tar", ".tar"}, {"application/gzip", ".gz"},
    {"application/vnd.android.package-archive", ".apk"}, {"application/x-rpm", ".rpm"}
};

QString typeLabel(const QString &extension)
{
    static const QMap<QString, QString> labels{
        {QStringLiteral("apk"), QObject::tr("Android app")},
        {QStringLiteral("docx"), QObject::tr("Word document")},
        {QStringLiteral("rpm"), QObject::tr("OpenWave package")},
        {QStringLiteral("xlsx"), QObject::tr("Excel spreadsheet")}};
    return labels.value(extension, QObject::tr("%1 file").arg(extension.toUpper()));
}

QString builtinHandlerName(const QString &id)
{
    if (id == QLatin1String("system.package-installer")) return QObject::tr("Package Installer");
    if (id == QLatin1String("system.android-installer")) return QObject::tr("Android App Installer");
    return {};
}
}

void ApplicationsBackend::rebuildFileTypes()
{
    QMap<QString, QString> names;
    for (const auto &entry : m_applications) {
        const auto application = entry.toMap();
        names.insert(application.value("appId").toString(), application.value("name").toString());
    }
    QMap<QString, QString> defaults;
    for (const auto &pair : m_associationValues.value(0).toList()) defaults.insert(pair.toList().value(0).toString(), pair.toList().value(1).toString());
    QMap<QString, QStringList> handlers;
    for (const auto &pair : m_associationValues.value(1).toList()) handlers.insert(pair.toList().value(0).toString(), pair.toList().value(1).toStringList());
    const auto commonKeys = commonTypes.keys();
    QSet<QString> extensions(commonKeys.begin(), commonKeys.end());
    for (auto it = handlers.cbegin(); it != handlers.cend(); ++it) extensions.insert(it.key());
    QStringList ordered(extensions.begin(), extensions.end());
    ordered.sort();
    QVariantList types;
    for (const auto &extension : std::as_const(ordered)) {
        QVariantList choices;
        QStringList ids = handlers.value(extension);
        QCollator collator; collator.setCaseSensitivity(Qt::CaseInsensitive);
        const auto nameOf = [&](const QString &id) { const QString builtin = builtinHandlerName(id); return builtin.isEmpty() ? names.value(id, id) : builtin; };
        std::sort(ids.begin(), ids.end(), [&](const QString &a, const QString &b) { return collator.compare(nameOf(a), nameOf(b)) < 0; });
        for (const auto &id : std::as_const(ids)) choices.append(QVariantMap{{"id", id}, {"name", nameOf(id)}});
        const QString chosen = defaults.value(extension);
        types.append(QVariantMap{{"extension", extension}, {"label", commonTypes.value(extension, extension)}, {"handlers", choices},
            {"protectedType", extension == "application/x-rpm" || extension == "application/vnd.android.package-archive"},
            {"defaultId", chosen}, {"defaultName", chosen.isEmpty() ? QString() : nameOf(chosen)}});
    }
    m_fileTypes = types;
    emit fileTypesChanged();
}

void ApplicationsBackend::refreshFileTypes()
{
    m_client.getFileAssociations([this](const QVariantList &values, const QString &error) {
        m_fileTypesError = error;
        m_associationValues = error.isEmpty() ? values : QVariantList();
        m_associationsLoaded = error.isEmpty();
        rebuildFileTypes();
    });
}

void ApplicationsBackend::setFileDefault(const QString &extension, const QString &appId)
{
    m_client.setFileDefault(extension, appId, [this](const QVariantList &, const QString &error) {
        if (!error.isEmpty()) { m_fileTypesError = error; emit fileTypesChanged(); }
        else refreshFileTypes();
    });
}

void ApplicationsBackend::requestUninstall(const QString &id)
{
    if (uninstallBusy()) return;
    for (const auto &entry : m_applications) {
        const auto application = entry.toMap();
        if (application.value("appId").toString() != id || !application.value("canUninstall").toBool()) continue;
        m_pendingUninstall = id;
        m_uninstallError.clear(); emit changed();
        m_prompt.confirmation(tr("Uninstall %1?").arg(application.value("name").toString()),
            tr("This will permanently remove the app and its data, settings and cache. Files saved in shared folders will be kept."),
            {{"acceptLabel", tr("Uninstall")}, {"cancelLabel", tr("Cancel")}});
        return;
    }
}

void ApplicationsBackend::clearDefaults(const QString &id)
{
    m_client.call("ClearFileDefaults", {id}, [this](const QVariantList &, const QString &error) {
        m_error = error; emit changed();
        m_fileTypesError = error;
        if (error.isEmpty()) refreshFileTypes();
        else emit fileTypesChanged();
    });
}
void ApplicationsBackend::loadDetails(const QString &id)
{
    const auto generation = ++m_storageGeneration;
    m_storageUsage = tr("Calculating…"); emit storageChanged();
    m_client.call("GetApplicationStorage", {id}, [this, generation](const QVariantList &values, const QString &error) {
        if (generation != m_storageGeneration) return;
        m_storageUsage = !error.isEmpty() ? tr("Unavailable: %1").arg(error)
            : !values.value(1).toBool() && !values.value(0).toULongLong() ? tr("Unavailable")
            : QLocale().formattedDataSize(values.value(0).toULongLong()) + (values.value(1).toBool() ? QString() : tr(" (partial)"));
        emit storageChanged();
    });
}
