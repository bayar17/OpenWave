#pragma once

#include <QObject>
#include <QUrl>

namespace OpenWave {

class WebInput : public QObject {
    Q_OBJECT
public:
    explicit WebInput(QObject *parent = nullptr);
    Q_INVOKABLE void attachView(QObject *view);
    Q_INVOKABLE bool reloadView(QObject *view, const QUrl &url);
};

}
