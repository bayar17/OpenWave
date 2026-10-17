#pragma once
#include <QColor>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>

class PageEdgeColor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQuickItem *source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(QColor color READ color NOTIFY colorChanged)
    Q_PROPERTY(bool darkForeground READ darkForeground NOTIFY colorChanged)
public:
    explicit PageEdgeColor(QObject *parent = nullptr);
    QQuickItem *source() const { return m_source; }
    void setSource(QQuickItem *source);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QColor color() const { return m_color; }
    bool darkForeground() const;
signals:
    void changed();
    void colorChanged();
private:
    void sample();
    QPointer<QQuickItem> m_source;
    QTimer m_timer;
    QColor m_color = Qt::white;
    bool m_enabled = false;
    bool m_pending = false;
    quint64 m_generation = 0;
};
