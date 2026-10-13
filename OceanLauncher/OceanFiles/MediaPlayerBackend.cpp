#include "MediaPlayerBackend.h"

#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QMetaObject>
#include <QtGlobal>
#include <mpg123.h>
#include <spa/param/audio/format-utils.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <utility>

namespace {
struct DecodeResult {
    QByteArray pcm;
    int rate = 0;
    int channels = 0;
    QString error;
};

DecodeResult decodeMp3(const QString &path, const std::atomic<unsigned> &generation,
                       unsigned expectedGeneration)
{
    DecodeResult result;
    if (mpg123_init() != MPG123_OK) {
        result.error = QStringLiteral("MP3 decoder could not start.");
        return result;
    }
    int decoderError = MPG123_OK;
    mpg123_handle *decoder = mpg123_new(nullptr, &decoderError);
    if (!decoder) {
        result.error = QStringLiteral("MP3 decoder could not open.");
        mpg123_exit();
        return result;
    }
    const QByteArray fileName = QFile::encodeName(path);
    if (mpg123_open_fixed(decoder, fileName.constData(), MPG123_STEREO,
                          MPG123_ENC_SIGNED_16) != MPG123_OK) {
        result.error = QString::fromLocal8Bit(mpg123_strerror(decoder));
    } else {
        long rate = 0;
        int channels = 0;
        int encoding = 0;
        if (mpg123_getformat(decoder, &rate, &channels, &encoding) != MPG123_OK
            || rate <= 0 || channels != 2 || encoding != MPG123_ENC_SIGNED_16) {
            result.error = QStringLiteral("Unsupported MP3 audio format.");
        } else {
            result.rate = static_cast<int>(rate);
            result.channels = channels;
            std::array<unsigned char, 65536> buffer{};
            for (;;) {
                if (generation.load() != expectedGeneration)
                    break;
                size_t written = 0;
                const int status = mpg123_read(decoder, buffer.data(), buffer.size(), &written);
                if (written > 0) {
                    if (result.pcm.size() > 256 * 1024 * 1024 - static_cast<int>(written)) {
                        result.error = QStringLiteral("This MP3 is too long for the preview.");
                        break;
                    }
                    result.pcm.append(reinterpret_cast<const char *>(buffer.data()),
                                      static_cast<qsizetype>(written));
                }
                if (status == MPG123_DONE)
                    break;
                if (status != MPG123_OK && status != MPG123_NEW_FORMAT) {
                    result.error = QString::fromLocal8Bit(mpg123_strerror(decoder));
                    break;
                }
            }
        }
    }
    mpg123_close(decoder);
    mpg123_delete(decoder);
    mpg123_exit();
    if (result.error.isEmpty() && result.pcm.isEmpty()
        && generation.load() == expectedGeneration)
        result.error = QStringLiteral("This MP3 contains no playable audio.");
    return result;
}
}

MediaPlayerBackend::MediaPlayerBackend(QString allowedRoot, QObject *parent)
    : QObject(parent), m_allowedRoot(std::move(allowedRoot))
{
    pw_init(nullptr, nullptr);
}

MediaPlayerBackend::~MediaPlayerBackend()
{
    stop();
    pw_deinit();
}

void MediaPlayerBackend::open(const QString &path)
{
    stop();
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (m_allowedRoot.isEmpty() || !canonical.startsWith(m_allowedRoot + QLatin1Char('/'))
        || QFileInfo(canonical).suffix().compare(QLatin1String("mp3"), Qt::CaseInsensitive) != 0) {
        m_error = QStringLiteral("This file is outside your files or is not an MP3.");
        emit statusChanged();
        return;
    }
    m_loading = true;
    emit statusChanged();
    const unsigned generation = m_generation.load();
    m_decodeThread = std::thread([this, canonical, generation] {
        DecodeResult result = decodeMp3(canonical, m_generation, generation);
        QMetaObject::invokeMethod(this, [this, canonical, generation, result = std::move(result)]() mutable {
            if (m_generation.load() != generation)
                return;
            m_loading = false;
            m_error = result.error;
            if (m_error.isEmpty()) {
                m_pcm = std::move(result.pcm);
                m_rate = result.rate;
                m_channels = result.channels;
                const qint64 frames = m_pcm.size() / (m_channels * 2);
                m_durationMs = frames * 1000 / m_rate;
                qInfo().noquote() << "Ocean Files decoded" << QFileInfo(canonical).fileName()
                                  << "duration(ms)" << m_durationMs;
                m_playing.store(true);
                if (!startAudio())
                    m_playing.store(false);
            }
            m_notifiedPlaying = m_playing.load();
            emit statusChanged();
        }, Qt::QueuedConnection);
    });
}

void MediaPlayerBackend::stop()
{
    m_generation.fetch_add(1);
    m_playing.store(false);
    stopAudio();
    if (m_decodeThread.joinable())
        m_decodeThread.join();
    m_pcm.clear();
    m_framePosition.store(0);
    m_positionMs = 0;
    m_durationMs = 0;
    m_rate = 0;
    m_channels = 0;
    m_loading = false;
    m_notifiedPlaying = false;
    m_error.clear();
    emit statusChanged();
}

void MediaPlayerBackend::toggle()
{
    if (m_loading || m_pcm.isEmpty())
        return;
    if (!m_stream && !startAudio()) {
        emit statusChanged();
        return;
    }
    const bool next = !m_playing.load();
    if (next && m_framePosition.load() >= m_pcm.size() / (m_channels * 2))
        m_framePosition.store(0);
    m_playing.store(next);
    m_notifiedPlaying = next;
    refreshPosition();
    emit statusChanged();
}

void MediaPlayerBackend::seek(qint64 positionMs)
{
    if (m_pcm.isEmpty() || m_rate <= 0)
        return;
    const qint64 totalFrames = m_pcm.size() / (m_channels * 2);
    const qint64 frames = std::clamp(positionMs * m_rate / 1000, qint64(0), totalFrames);
    m_framePosition.store(frames);
    refreshPosition();
}

void MediaPlayerBackend::refreshPosition()
{
    if (m_rate <= 0)
        return;
    const qint64 position = m_framePosition.load() * 1000 / m_rate;
    const bool playingNow = m_playing.load();
    if (position != m_positionMs || playingNow != m_notifiedPlaying) {
        m_positionMs = position;
        m_notifiedPlaying = playingNow;
        emit statusChanged();
    }
}

bool MediaPlayerBackend::startAudio()
{
    if (m_stream)
        return true;
    m_loop = pw_thread_loop_new("ocean-files-audio", nullptr);
    if (!m_loop || pw_thread_loop_start(m_loop) < 0) {
        m_error = QStringLiteral("Could not start the audio connection.");
        if (m_loop) { pw_thread_loop_destroy(m_loop); m_loop = nullptr; }
        return false;
    }
    static const pw_stream_events events = [] {
        pw_stream_events value{};
        value.version = PW_VERSION_STREAM_EVENTS;
        value.process = &MediaPlayerBackend::process;
        value.state_changed = &MediaPlayerBackend::stateChanged;
        return value;
    }();

    pw_thread_loop_lock(m_loop);
    m_stream = pw_stream_new_simple(pw_thread_loop_get_loop(m_loop), "Ocean Files preview",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                          PW_KEY_MEDIA_ROLE, "Music", nullptr), &events, this);
    if (!m_stream) {
        pw_thread_loop_unlock(m_loop);
        stopAudio();
        m_error = QStringLiteral("Could not create the audio stream.");
        return false;
    }
    spa_audio_info_raw format{};
    format.format = SPA_AUDIO_FORMAT_S16;
    format.rate = static_cast<uint32_t>(m_rate);
    format.channels = static_cast<uint32_t>(m_channels);
    format.position[0] = SPA_AUDIO_CHANNEL_FL;
    format.position[1] = SPA_AUDIO_CHANNEL_FR;
    std::array<std::byte, 1024> storage{};
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage.data(), storage.size());
    const spa_pod *parameter = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &format);
    const int connected = pw_stream_connect(m_stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS),
        &parameter, 1);
    pw_thread_loop_unlock(m_loop);
    if (connected < 0) {
        stopAudio();
        m_error = QStringLiteral("PipeWire could not connect the audio stream.");
        return false;
    }
    m_error.clear();
    qInfo("Ocean Files PipeWire audio stream connected");
    return true;
}

void MediaPlayerBackend::stopAudio()
{
    if (!m_loop)
        return;
    pw_thread_loop_lock(m_loop);
    if (m_stream) {
        pw_stream_destroy(m_stream);
        m_stream = nullptr;
    }
    pw_thread_loop_unlock(m_loop);
    pw_thread_loop_stop(m_loop);
    pw_thread_loop_destroy(m_loop);
    m_loop = nullptr;
}

void MediaPlayerBackend::process(void *userdata)
{
    auto *self = static_cast<MediaPlayerBackend *>(userdata);
    pw_buffer *queued = pw_stream_dequeue_buffer(self->m_stream);
    if (!queued)
        return;
    spa_buffer *buffer = queued->buffer;
    auto *output = static_cast<char *>(buffer->datas[0].data);
    if (!output) {
        pw_stream_queue_buffer(self->m_stream, queued);
        return;
    }
    const int frameBytes = self->m_channels * 2;
    const qint64 capacity = buffer->datas[0].maxsize / frameBytes;
    const qint64 frames = queued->requested > 0
        ? std::min(capacity, static_cast<qint64>(queued->requested)) : capacity;
    const qint64 total = self->m_pcm.size() / frameBytes;
    std::memset(output, 0, static_cast<size_t>(frames * frameBytes));
    if (self->m_playing.load()) {
        const qint64 position = self->m_framePosition.load();
        const qint64 count = std::min(frames, std::max(qint64(0), total - position));
        if (count > 0) {
            std::memcpy(output, self->m_pcm.constData() + position * frameBytes,
                        static_cast<size_t>(count * frameBytes));
            self->m_framePosition.store(position + count);
        }
        if (position + count >= total)
            self->m_playing.store(false);
    }
    buffer->datas[0].chunk->offset = 0;
    buffer->datas[0].chunk->stride = frameBytes;
    buffer->datas[0].chunk->size = static_cast<uint32_t>(frames * frameBytes);
    pw_stream_queue_buffer(self->m_stream, queued);
}

void MediaPlayerBackend::stateChanged(void *userdata, pw_stream_state,
                                      pw_stream_state state, const char *error)
{
    if (state != PW_STREAM_STATE_ERROR)
        return;
    auto *self = static_cast<MediaPlayerBackend *>(userdata);
    const unsigned generation = self->m_generation.load();
    const QString message = QStringLiteral("Audio output failed: %1")
        .arg(QString::fromLocal8Bit(error ? error : "Unknown PipeWire error"));
    QMetaObject::invokeMethod(self, [self, message, generation] {
        if (self->m_generation.load() != generation)
            return;
        self->m_playing.store(false);
        self->m_error = message;
        qWarning().noquote() << "Ocean Files" << message;
        emit self->statusChanged();
    }, Qt::QueuedConnection);
}
