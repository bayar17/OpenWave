#include "QuickMenuAppletManager.h"

#include "QuickMenuAppletRegistry.h"

QuickMenuAppletManager::QuickMenuAppletManager(QuickMenuAppletRegistry *registry, QObject *parent)
    : QObject(parent)
    , m_registry(registry)
{
    connect(m_registry, &QuickMenuAppletRegistry::changed, this, &QuickMenuAppletManager::changed);
}

QAbstractItemModel *QuickMenuAppletManager::model() const
{
    return m_registry;
}

int QuickMenuAppletManager::count() const
{
    return m_registry->count();
}

QVariantList QuickMenuAppletManager::rejections() const
{
    return m_registry->rejections();
}

bool QuickMenuAppletManager::setEnabled(const QString &id, bool enabled)
{
    if (m_registry->setEnabled(id, enabled))
        return true;
    emit failed(tr("Could not save this setting. Please try again."));
    return false;
}

bool QuickMenuAppletManager::moveUp(const QString &id)
{
    const int index = static_cast<int>(m_registry->orderedIds().indexOf(id));
    if (index <= 0)
        return false;
    if (m_registry->move(id, index - 1))
        return true;
    emit failed(tr("Could not save this setting. Please try again."));
    return false;
}

bool QuickMenuAppletManager::moveDown(const QString &id)
{
    const QStringList ids = m_registry->orderedIds();
    const int index = static_cast<int>(ids.indexOf(id));
    if (index < 0 || index >= ids.size() - 1)
        return false;
    if (m_registry->move(id, index + 1))
        return true;
    emit failed(tr("Could not save this setting. Please try again."));
    return false;
}

bool QuickMenuAppletManager::reset()
{
    if (m_registry->resetState())
        return true;
    emit failed(tr("Could not save this setting. Please try again."));
    return false;
}

QUrl QuickMenuAppletManager::settingsUrl(const QString &id) const
{
    return m_registry->settingsUrl(id);
}
