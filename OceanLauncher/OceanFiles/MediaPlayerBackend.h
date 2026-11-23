#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <atomic>
#include <thread>
#include <pipewire/pipewire.h>

class MediaPlayerBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY statusChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY statusChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY statusChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged)

public:
    explicit MediaPlayerBackend(QString allowedRoot, QObject *parent = nullptr);
    ~MediaPlayerBackend() override;

    bool loading() const { return m_loading; }
    bool playing() const { return m_playing.load(); }
    qint64 positionMs() const { return m_positionMs; }
    qint64 durationMs() const { return m_durationMs; }
    QString error() const { return m_error; }

    Q_INVOKABLE void open(const QString &path);
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void seek(qint64 positionMs);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refreshPosition();

signals:
    void statusChanged();

private:
    bool startAudio();
    void stopAudio();
    static void process(void *userdata);
    static void stateChanged(void *userdata, pw_stream_state oldState,
                             pw_stream_state state, const char *error);

    QString m_allowedRoot;
    QByteArray m_pcm;
    std::thread m_decodeThread;
    std::atomic<unsigned> m_generation{0};
    std::atomic<qint64> m_framePosition{0};
    std::atomic<bool> m_playing{false};
    pw_thread_loop *m_loop = nullptr;
    pw_stream *m_stream = nullptr;
    int m_rate = 0;
    int m_channels = 0;
    bool m_loading = false;
    bool m_notifiedPlaying = false;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    QString m_error;
};
