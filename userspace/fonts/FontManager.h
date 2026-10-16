#pragma once

#include <QFont>
#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

namespace OpenWave {

class FontManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Fonts)
    QML_SINGLETON
    Q_PROPERTY(QString systemFamily READ systemFamily CONSTANT)
    Q_PROPERTY(QString family READ family NOTIFY fontChanged)
    Q_PROPERTY(QFont defaultFont READ defaultFont NOTIFY fontChanged)
    Q_PROPERTY(QStringList families READ families NOTIFY familiesChanged)
    Q_PROPERTY(bool sourceSansProAvailable READ sourceSansProAvailable NOTIFY familiesChanged)

public:
    explicit FontManager(QObject *parent = nullptr);
    static bool initialize(const QString &fontDirectory = {});
    QString systemFamily() const;
    QString family() const;
    QFont defaultFont() const;
    QStringList families() const;
    bool sourceSansProAvailable() const;
    Q_INVOKABLE bool isAvailable(const QString &family) const;
    Q_INVOKABLE QStringList styles(const QString &family) const;
    Q_INVOKABLE QFont font(int pixelSize, int weight = QFont::Normal,
                          bool italic = false, const QString &family = {}) const;

signals:
    void fontChanged();
    void familiesChanged();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
};

}
