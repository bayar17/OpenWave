#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>

class QuickMenuAppletRegistry;

class QuickMenuAppletManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT FINAL)
    Q_PROPERTY(int count READ count NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList rejections READ rejections NOTIFY changed FINAL)

public:
    explicit QuickMenuAppletManager(QuickMenuAppletRegistry *registry, QObject *parent = nullptr);

    QAbstractItemModel *model() const;
    int count() const;
    QVariantList rejections() const;

    Q_INVOKABLE bool setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE bool moveUp(const QString &id);
    Q_INVOKABLE bool moveDown(const QString &id);
    Q_INVOKABLE bool reset();
    Q_INVOKABLE QUrl settingsUrl(const QString &id) const;

signals:
    void changed();
    void failed(const QString &message);

private:
    QuickMenuAppletRegistry *m_registry;
};
