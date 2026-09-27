// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PlaybackController.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QDateTime>
#include <QMediaDevices>
#include <QtEndian>
#include <QTimer>
#include <algorithm>
#include <cstring>
#include <optional>

namespace {

// Parse a PCM WAV (RIFF) into format + interleaved sample bytes.
struct ParsedWav {
    QAudioFormat format;
    QByteArray pcm;
    qint64 durationMs = 0;
};

std::optional<ParsedWav> parseWavPcm(const QByteArray &wav)
{
    if (wav.size() < 44) {
        return std::nullopt;
    }
    const auto *d = reinterpret_cast<const uchar *>(wav.constData());
    if (std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0) {
        return std::nullopt;
    }

    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    int audioFormatTag = 0; // 1 = PCM
    QByteArray pcm;

    int offset = 12;
    while (offset + 8 <= wav.size()) {
        const char *id = wav.constData() + offset;
        const quint32 chunkSize = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(wav.constData() + offset + 4));
        const int dataStart = offset + 8;
        if (dataStart + static_cast<int>(chunkSize) > wav.size()) {
            break;
        }
        if (std::memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16) {
            audioFormatTag = qFromLittleEndian<quint16>(
                reinterpret_cast<const uchar *>(wav.constData() + dataStart));
            channels = qFromLittleEndian<quint16>(
                reinterpret_cast<const uchar *>(wav.constData() + dataStart + 2));
            sampleRate = qFromLittleEndian<quint32>(
                reinterpret_cast<const uchar *>(wav.constData() + dataStart + 4));
            bitsPerSample = qFromLittleEndian<quint16>(
                reinterpret_cast<const uchar *>(wav.constData() + dataStart + 14));
        } else if (std::memcmp(id, "data", 4) == 0) {
            pcm = QByteArray(wav.constData() + dataStart, int(chunkSize));
        }
        offset = dataStart + int(chunkSize);
        if (offset & 1) {
            ++offset; // word align
        }
    }

    if (pcm.isEmpty() || sampleRate <= 0 || channels <= 0 || bitsPerSample <= 0) {
        return std::nullopt;
    }
    // Piper emits PCM; refuse compressed WAV.
    if (audioFormatTag != 1 && audioFormatTag != 0xFFFE) {
        return std::nullopt;
    }

    QAudioFormat fmt;
    fmt.setSampleRate(sampleRate);
    fmt.setChannelCount(channels);
    if (bitsPerSample == 16) {
        fmt.setSampleFormat(QAudioFormat::Int16);
    } else if (bitsPerSample == 32) {
        fmt.setSampleFormat(QAudioFormat::Int32);
    } else if (bitsPerSample == 8) {
        fmt.setSampleFormat(QAudioFormat::UInt8);
    } else {
        return std::nullopt;
    }

    const int bytesPerSec = sampleRate * channels * (bitsPerSample / 8);
    ParsedWav out;
    out.format = fmt;
    out.pcm = std::move(pcm);
    out.durationMs = bytesPerSec > 0
        ? (qint64(out.pcm.size()) * 1000) / bytesPerSec
        : 0;
    return out;
}

// Duration of a PCM WAV from its header/data chunk (legacy helper for cache).
qint64 wavDurationMs(const QByteArray &wav)
{
    if (auto p = parseWavPcm(wav)) {
        return p->durationMs;
    }
    return 0;
}

} // namespace

PlaybackController::PlaybackController(PiperClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    m_positionTimer = new QTimer(this);
    m_positionTimer->setInterval(50);
    connect(m_positionTimer, &QTimer::timeout, this, &PlaybackController::onPositionTick);

    // Primary end-of-sentence signal: schedule from WAV duration (QAudioSink
    // Idle/processedUSecs is unreliable across backends).
    m_sentenceEndTimer = new QTimer(this);
    m_sentenceEndTimer->setSingleShot(true);
    connect(m_sentenceEndTimer, &QTimer::timeout, this, &PlaybackController::onSentenceEndTimer);

    connect(m_client, &PiperClient::audioReady, this, &PlaybackController::onAudioReady);
    connect(m_client, &PiperClient::synthesisError, this, &PlaybackController::onSynthesisError);
}

PlaybackController::~PlaybackController()
{
    stopSink();
}

void PlaybackController::stopSink()
{
    if (m_positionTimer) {
        m_positionTimer->stop();
    }
    if (m_sentenceEndTimer) {
        m_sentenceEndTimer->stop();
    }
    if (m_sink) {
        QObject::disconnect(m_sink, nullptr, this, nullptr);
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    m_pcmBuffer.reset();
    m_sinkReachedActive = false;
}

qint64 PlaybackController::currentPositionMs() const
{
    if (!m_playing || m_paused) {
        return m_seekOffsetMs;
    }
    const qint64 dur = durationForCurrentSentence();
    if (m_playStartMs <= 0) {
        return m_seekOffsetMs;
    }
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_playStartMs;
    return qBound(qint64(0), m_seekOffsetMs + elapsed, dur > 0 ? dur : m_seekOffsetMs + elapsed);
}

qint64 PlaybackController::currentDurationMs() const
{
    return durationForCurrentSentence();
}

double PlaybackController::currentFraction() const
{
    const qint64 duration = durationForCurrentSentence();
    if (duration <= 0) {
        return 0.0;
    }
    return qBound(0.0, double(currentPositionMs()) / double(duration), 1.0);
}

qint64 PlaybackController::durationForCurrentSentence() const
{
    if (m_currentIndex >= 0 && m_currentIndex < m_sentences.size()) {
        return m_audioDurationMs.value(m_sentences[m_currentIndex].id, 0);
    }
    return 0;
}

void PlaybackController::setVolume(float volume)
{
    m_volume = qBound(0.0f, volume, 1.0f);
    if (m_sink) {
        m_sink->setVolume(m_muted ? 0.0 : double(m_volume));
    }
}

float PlaybackController::volume() const
{
    return m_volume;
}

void PlaybackController::setMuted(bool muted)
{
    m_muted = muted;
    if (m_sink) {
        m_sink->setVolume(m_muted ? 0.0 : double(m_volume));
    }
}

bool PlaybackController::isMuted() const
{
    return m_muted;
}

void PlaybackController::loadSentences(const QVector<Sentence> &sentences)
{
    stop();
    m_sentences = sentences;
    m_audioCache.clear();
    m_audioDurationMs.clear();
    m_currentIndex = sentences.isEmpty() ? -1 : 0;
}

bool PlaybackController::isPlaying() const
{
    return m_playing;
}

void PlaybackController::setSuspended(bool suspended)
{
    m_suspended = suspended;
    if (suspended) {
        stop();
        m_client->cancelAll();
        m_audioCache.clear();
        m_audioDurationMs.clear();
        m_waitingForAudio = false;
    }
}

void PlaybackController::play()
{
    if (m_currentIndex < 0 || m_sentences.isEmpty()) {
        return;
    }
    m_playing = true;
    requestLookahead();

    if (m_paused && m_sink) {
        m_paused = false;
        m_playStartMs = QDateTime::currentMSecsSinceEpoch();
        m_sink->resume();
        m_positionTimer->start();
        return;
    }

    playCurrentIfReady();
}

void PlaybackController::pause()
{
    if (!m_playing) {
        return;
    }
    m_playing = false;
    if (m_sink && m_sink->state() == QAudio::ActiveState) {
        m_paused = true;
        m_seekOffsetMs = currentPositionMs();
        m_sink->suspend();
        m_positionTimer->stop();
    } else {
        m_paused = false;
    }
}

void PlaybackController::stop()
{
    m_playing = false;
    m_paused = false;
    m_waitingForAudio = false;
    m_pendingSeekFraction = -1.0;
    m_seekOffsetMs = 0;
    m_playStartMs = 0;
    stopSink();
    if (m_client) {
        m_client->cancelAll();
    }
}

void PlaybackController::next()
{
    if (m_currentIndex < 0) {
        return;
    }
    seekToSentence(m_currentIndex + 1);
}

void PlaybackController::previous()
{
    if (m_currentIndex < 0) {
        return;
    }
    seekToSentence(m_currentIndex - 1);
}

void PlaybackController::seekToSentence(int index, double fractionWithin)
{
    if (index < 0 || index >= m_sentences.size()) {
        if (index >= m_sentences.size()) {
            m_playing = false;
            m_paused = false;
            m_pendingSeekFraction = -1.0;
            stopSink();
            emit playbackFinished();
        }
        return;
    }
    stopSink();
    m_currentIndex = index;
    m_pendingSeekFraction = qBound(0.0, fractionWithin, 1.0);
    m_seekOffsetMs = 0;
    m_paused = false;
    if (m_playing) {
        requestLookahead();
        playCurrentIfReady();
    }
}

void PlaybackController::setSpeed(double speed)
{
    m_client->cancelAll();
    m_client->setSpeed(speed);
    m_audioCache.clear();
    m_audioDurationMs.clear();
    if (m_playing) {
        requestLookahead();
    }
}

void PlaybackController::setVoice(const QString &voice)
{
    m_client->cancelAll();
    m_client->setVoice(voice);
    m_audioCache.clear();
    m_audioDurationMs.clear();
    if (m_playing) {
        requestLookahead();
    }
}

void PlaybackController::requestLookahead()
{
    if (m_currentIndex < 0) {
        return;
    }
    int end = std::min(m_currentIndex + m_lookahead, static_cast<int>(m_sentences.size()) - 1);
    for (int i = m_currentIndex; i <= end; ++i) {
        const Sentence &s = m_sentences[i];
        if (!m_audioCache.contains(s.id)) {
            m_client->requestSpeak(s.id, s.text);
        }
    }
}

void PlaybackController::playCurrentIfReady()
{
    if (m_currentIndex < 0 || m_currentIndex >= m_sentences.size()) {
        return;
    }
    const Sentence &s = m_sentences[m_currentIndex];
    auto it = m_audioCache.find(s.id);
    if (it == m_audioCache.end()) {
        m_waitingForAudio = true;
        m_paused = false;
        emit preparingAudio(s.id);
        return;
    }
    m_waitingForAudio = false;
    m_paused = false;

    auto parsed = parseWavPcm(it.value());
    if (!parsed) {
        emit errorOccurred(QStringLiteral("Invalid WAV for sentence %1").arg(s.id));
        advanceToNext();
        return;
    }

    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) {
        emit errorOccurred(tr("No audio output device"));
        m_playing = false;
        return;
    }
    if (!device.isFormatSupported(parsed->format)) {
        // Try nearest — QAudioSink may still accept Int16 mono/stereo at rate.
        emit errorOccurred(tr("Audio device does not support this WAV format"));
        // Still try; some devices report false negatives.
    }

    stopSink();

    m_sink = new QAudioSink(device, parsed->format, this);
    m_sink->setVolume(m_muted ? 0.0 : double(m_volume));
    connect(m_sink, &QAudioSink::stateChanged, this, &PlaybackController::onSinkStateChanged);

    // Optional mid-sentence seek via byte offset into PCM.
    QByteArray pcm = parsed->pcm;
    m_seekOffsetMs = 0;
    if (m_pendingSeekFraction > 0.0 && parsed->durationMs > 0) {
        const qint64 seekMs = qint64(m_pendingSeekFraction * double(parsed->durationMs));
        const int bps = parsed->format.bytesPerFrame();
        const int frame = int((seekMs * parsed->format.sampleRate()) / 1000);
        int byteOffset = frame * bps;
        byteOffset = qBound(0, byteOffset, qMax(0, pcm.size() - bps));
        pcm = pcm.mid(byteOffset);
        m_seekOffsetMs = seekMs;
        m_pendingSeekFraction = -1.0;
    } else {
        m_pendingSeekFraction = -1.0;
    }

    m_pcmBuffer = std::make_unique<QBuffer>();
    m_pcmBuffer->setData(pcm);
    m_pcmBuffer->open(QIODevice::ReadOnly);

    m_playStartMs = QDateTime::currentMSecsSinceEpoch();
    m_sink->start(m_pcmBuffer.get());
    m_positionTimer->start();
    // Remaining duration after optional seek into the sentence.
    const qint64 remainMs = qMax(qint64(1), parsed->durationMs - m_seekOffsetMs);
    m_sentenceEndTimer->start(int(remainMs) + 40);

    emit sentenceStarted(s.id, s.start, s.end);
    emit audioPositionChanged(m_seekOffsetMs, parsed->durationMs);
    requestLookahead();
}

void PlaybackController::onSinkStateChanged()
{
    if (!m_sink || m_advancing) {
        return;
    }
    const QAudio::State st = m_sink->state();
    if (st == QAudio::ActiveState) {
        m_sinkReachedActive = true;
        return;
    }
    if (!m_playing || m_paused) {
        return;
    }
    // Errors still force advance; normal end is driven by m_sentenceEndTimer.
    if (st == QAudio::StoppedState && m_sink->error() != QAudio::NoError) {
        emit errorOccurred(tr("Audio output error (%1)").arg(int(m_sink->error())));
        advanceToNext();
    }
}

void PlaybackController::onSentenceEndTimer()
{
    if (!m_playing || m_paused || m_advancing) {
        return;
    }
    advanceToNext();
}

void PlaybackController::onPositionTick()
{
    if (!m_playing || m_paused || m_advancing) {
        return;
    }
    emit audioPositionChanged(currentPositionMs(), durationForCurrentSentence());
}

void PlaybackController::advanceToNext()
{
    if (m_advancing) {
        return;
    }
    m_advancing = true;

    if (m_currentIndex < 0 || m_currentIndex >= m_sentences.size()) {
        m_playing = false;
        stopSink();
        m_advancing = false;
        emit playbackFinished();
        return;
    }

    const int finishedIndex = m_currentIndex;
    const qint64 finishedDuration = m_audioDurationMs.value(m_sentences[finishedIndex].id, 0);
    m_audioCache.remove(m_sentences[finishedIndex].id);
    m_pendingSeekFraction = -1.0;
    m_seekOffsetMs = 0;
    stopSink();

    if (finishedIndex + 1 >= m_sentences.size()) {
        m_playing = false;
        m_currentIndex = m_sentences.size() - 1;
        if (finishedDuration > 0) {
            emit audioPositionChanged(finishedDuration, finishedDuration);
        }
        m_advancing = false;
        emit playbackFinished();
        return;
    }

    m_currentIndex = finishedIndex + 1;
    // Prefetch from the new index before starting playback of this sentence.
    requestLookahead();
    playCurrentIfReady();
    m_advancing = false;
}

void PlaybackController::onAudioReady(int id, int /*sampleRate*/, const QByteArray &wav)
{
    if (m_suspended) {
        return;
    }
    m_audioCache.insert(id, wav);
    const qint64 duration = wavDurationMs(wav);
    if (duration > 0) {
        m_audioDurationMs.insert(id, duration);
    }
    if (m_playing && m_waitingForAudio && m_currentIndex >= 0
        && m_currentIndex < m_sentences.size() && m_sentences[m_currentIndex].id == id) {
        playCurrentIfReady();
    }
}

void PlaybackController::onSynthesisError(int id, const QString &message)
{
    if (m_suspended) {
        return;
    }
    emit errorOccurred(QStringLiteral("Synthesis failed for sentence %1: %2").arg(id).arg(message));
    if (m_playing && m_currentIndex >= 0 && m_currentIndex < m_sentences.size()
        && m_sentences[m_currentIndex].id == id) {
        advanceToNext();
    }
}
