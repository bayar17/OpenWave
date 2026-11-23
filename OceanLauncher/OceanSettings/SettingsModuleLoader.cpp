#include "SettingsModuleLoader.h"

#include "SettingsModuleRegistry.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQuickItem>
#include <QtQml/qqml.h>

namespace {

constexpr int MaxTreeEntries = 4096;

QString describe(const QList<QQmlError> &errors)
{
    QStringList lines;
    for (const QQmlError &error : errors)
        lines << error.toString();
    return lines.join(QLatin1Char('\n'));
}

}

SettingsModuleLoader::SettingsModuleLoader(QObject *parent)
    : QObject(parent)
{
}

SettingsModuleLoader::~SettingsModuleLoader()
{
    ++m_token;
    if (m_component)
        delete m_component.data();
    if (m_item)
        delete m_item.data();
    if (m_context)
        delete m_context.data();
}

QQuickItem *SettingsModuleLoader::host() const
{
    return m_host;
}

void SettingsModuleLoader::setHost(QQuickItem *host)
{
    if (m_host == host)
        return;
    if (m_item)
        unload();
    if (m_host)
        m_host->disconnect(this);
    m_host = host;
    if (m_host) {
        connect(m_host, &QQuickItem::widthChanged, this, &SettingsModuleLoader::fitToHost);
        connect(m_host, &QQuickItem::heightChanged, this, &SettingsModuleLoader::fitToHost);
        connect(m_host, &QObject::destroyed, this, [this]() { discard(); });
    }
    emit hostChanged();
}

QQuickItem *SettingsModuleLoader::item() const
{
    return m_item;
}

QString SettingsModuleLoader::activeModuleId() const
{
    return m_activeModuleId;
}

bool SettingsModuleLoader::loaded() const
{
    return m_status == Ready && m_item;
}

SettingsModuleLoader::Status SettingsModuleLoader::status() const
{
    return m_status;
}

QString SettingsModuleLoader::errorString() const
{
    return m_errorString;
}

void SettingsModuleLoader::setRegistry(SettingsModuleRegistry *registry)
{
    m_registry = registry;
}

SettingsModuleRegistry *SettingsModuleLoader::registry() const
{
    return m_registry ? m_registry.data() : SettingsModuleRegistry::shared();
}

bool SettingsModuleLoader::load(const QString &moduleId)
{
    if (m_status == Ready && m_item && m_activeModuleId == moduleId)
        return true;
    if (m_status == Loading && m_pendingModuleId == moduleId)
        return true;

    discard();
    const quint64 token = ++m_token;

    if (moduleId.isEmpty()) {
        fail(moduleId, QStringLiteral("no module id was requested"));
        return false;
    }
    if (!m_host) {
        fail(moduleId, QStringLiteral("no host item is set"));
        return false;
    }
    QQmlEngine *engine = qmlEngine(this);
    if (!engine) {
        fail(moduleId, QStringLiteral("the loader is not owned by a QML engine"));
        return false;
    }

    SettingsModuleRegistry::ValidatedModule validated;
    QString reason;
    if (!registry()->revalidate(moduleId, validated, reason)) {
        fail(moduleId, reason);
        return false;
    }
    if (!verifyTree(validated.directory, reason)) {
        fail(moduleId, reason);
        return false;
    }

    engine->trimComponentCache();
    m_pendingModuleId = moduleId;
    setErrorString(QString());
    setStatus(Loading);
    if (!compile(moduleId, validated.entrySource, validated.entryUrl))
        return false;
    if (token == m_token && m_component && m_component->status() != QQmlComponent::Loading)
        finishComponent(moduleId, token);
    return m_status != Error;
}

void SettingsModuleLoader::unload()
{
    const bool wasActive = m_status != Idle || m_item;
    discard();
    setErrorString(QString());
    if (wasActive)
        setStatus(Idle);
}

bool SettingsModuleLoader::verifyTree(const QString &directory, QString &reason) const
{
    QDirIterator iterator(directory,
                          QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    const QDir base(directory);
    int count = 0;
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        const QString relative = base.relativeFilePath(info.absoluteFilePath());
        if (++count > MaxTreeEntries) {
            reason = QStringLiteral("module contains too many files");
            return false;
        }
        if (info.isSymLink()) {
            reason = QStringLiteral("module contains a symbolic link: %1").arg(relative);
            return false;
        }
        if (!info.isFile() && !info.isDir()) {
            reason = QStringLiteral("module contains a special file: %1").arg(relative);
            return false;
        }
    }
    return true;
}

bool SettingsModuleLoader::compile(const QString &moduleId, const QByteArray &source,
                                   const QUrl &url)
{
    QQmlEngine *engine = qmlEngine(this);
    QQmlComponent *component = new QQmlComponent(engine, this);
    m_component = component;
    component->setData(source, url);
    if (component->status() == QQmlComponent::Loading) {
        const quint64 token = m_token;
        connect(component, &QQmlComponent::statusChanged, this, [this, moduleId, token]() {
            finishComponent(moduleId, token);
        });
    }
    if (component->status() == QQmlComponent::Error) {
        fail(moduleId, describe(component->errors()));
        return false;
    }
    return true;
}

void SettingsModuleLoader::finishComponent(const QString &moduleId, quint64 token)
{
    if (token != m_token || !m_component || m_status != Loading)
        return;
    QQmlComponent *component = m_component;
    if (component->status() == QQmlComponent::Loading)
        return;
    if (component->status() != QQmlComponent::Ready) {
        fail(moduleId, describe(component->errors()));
        return;
    }
    if (!m_host) {
        fail(moduleId, QStringLiteral("the host item was destroyed"));
        return;
    }

    QQmlEngine *engine = qmlEngine(this);
    const QString moduleUrlPrefix = QUrl::fromLocalFile(
        QFileInfo(component->url().toLocalFile()).absolutePath()).toString() + QLatin1Char('/');
    QStringList warnings;
    const QMetaObject::Connection collector = connect(engine, &QQmlEngine::warnings, this,
        [&warnings, &moduleUrlPrefix](const QList<QQmlError> &errors) {
            for (const QQmlError &error : errors) {
                if (error.url().toString().startsWith(moduleUrlPrefix))
                    warnings << error.toString();
            }
        });

    QQmlContext *context = new QQmlContext(engine->rootContext(), this);
    QObject *object = component->create(context);
    disconnect(collector);

    if (!object || component->isError()) {
        const QString reason = object ? describe(component->errors())
            : (component->isError() ? describe(component->errors())
                                    : QStringLiteral("the component could not be created"));
        delete object;
        delete context;
        fail(moduleId, reason.isEmpty() ? QStringLiteral("the component could not be created")
                                        : reason);
        return;
    }
    QQuickItem *item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        delete object;
        delete context;
        fail(moduleId, QStringLiteral("the module entry must be an Item"));
        return;
    }
    if (!warnings.isEmpty()) {
        delete item;
        delete context;
        fail(moduleId, warnings.join(QLatin1Char('\n')));
        return;
    }
    install(moduleId, item, context);
}

void SettingsModuleLoader::install(const QString &moduleId, QQuickItem *item, QQmlContext *context)
{
    item->setParent(m_host);
    item->setParentItem(m_host);
    m_item = item;
    m_context = context;
    fitToHost();
    m_pendingModuleId.clear();
    m_activeModuleId = moduleId;
    setErrorString(QString());
    emit itemChanged();
    emit activeModuleIdChanged();
    setStatus(Ready);
    emit loadedChanged();
    emit moduleLoaded(moduleId);
}

void SettingsModuleLoader::fail(const QString &moduleId, const QString &reason)
{
    discard();
    setErrorString(reason);
    setStatus(Error);
    emit loadFailed(moduleId, reason);
}

void SettingsModuleLoader::discard()
{
    ++m_token;
    const QString previous = m_activeModuleId;
    const bool hadItem = m_item;
    m_pendingModuleId.clear();

    if (m_component) {
        m_component->disconnect(this);
        m_component->deleteLater();
        m_component = nullptr;
    }
    if (m_item) {
        m_item->setVisible(false);
        m_item->setParentItem(nullptr);
        m_item->deleteLater();
        m_item = nullptr;
    }
    if (m_context) {
        m_context->deleteLater();
        m_context = nullptr;
    }
    if (!previous.isEmpty()) {
        m_activeModuleId.clear();
        emit activeModuleIdChanged();
    }
    if (hadItem) {
        emit itemChanged();
        emit loadedChanged();
    }
    if (m_status == Ready || m_status == Loading) {
        m_status = Idle;
        emit statusChanged();
    }
    if (!previous.isEmpty())
        emit moduleUnloaded(previous);
}

void SettingsModuleLoader::setStatus(Status status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void SettingsModuleLoader::setErrorString(const QString &errorString)
{
    if (m_errorString == errorString)
        return;
    m_errorString = errorString;
    emit errorStringChanged();
}

void SettingsModuleLoader::fitToHost()
{
    if (!m_item || !m_host)
        return;
    m_item->setWidth(m_host->width());
    m_item->setHeight(m_host->height());
}
