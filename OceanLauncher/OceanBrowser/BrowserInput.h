#pragma once

#include <QObject>
#include <QUrl>

class BrowserInput : public QObject {
    Q_OBJECT
public:
    explicit BrowserInput(QObject *parent = nullptr);
    Q_INVOKABLE void attachView(QObject *view);
    Q_INVOKABLE bool reloadView(QObject *view, const QUrl &url);
};
