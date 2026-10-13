#pragma once

#include <QObject>

class BrowserInput : public QObject {
    Q_OBJECT
public:
    explicit BrowserInput(QObject *parent = nullptr);
    Q_INVOKABLE void attachView(QObject *view);
};
