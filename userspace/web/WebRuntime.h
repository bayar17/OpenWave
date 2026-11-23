#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;

namespace OpenWave {

class WebRuntime : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit WebRuntime(QObject *parent = nullptr);
    static void configureGraphics();
    static bool initialize(QQmlEngine *engine = nullptr);
    Q_INVOKABLE bool attachView(QObject *view);
};

}
