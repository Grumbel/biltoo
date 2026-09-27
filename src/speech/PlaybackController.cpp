// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PlaybackController.h"

#include <QAudioOutput>
#include <QtEndian>
#include <QtGlobal>

namespace {

// Duration of a PCM WAV from its header/data chunk. Used so mid-sentence
// seeks do not depend on QMediaPlayer having finished probing the buffer.
qint64 wavDurationMs(const QByteArray &wav)
{
    if (wav.size() < 44) {
        return 0;
    }
    if (!(wav[0] == 'R' && wav[1] == 'I' && wav[2] == 'F' && wav[3] == 'F'
          && wav[8] == 'W' && wav[9] == 'A' && wav[10] == 'V' && wav[11] == 'E')) {
        return 0;
    }

    int sampleRate = 0;
    int channels = 0;
    int bitsPerSample = 0;
    int dataBytes = 0;

    int offset = 12;
    while (offset + 8 <= wav.size()) {
        const char *chunkId = wav.constData() + offset;
        const quint32 chunkSize = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(wav.constData() + offset + 4));
        const int dataStart = offset + 8;
        if (chunkId[0] == 'f' && chunkId[1] == 'm' && chunkId[2] == 't' && chunkId[3] == ' ') {
            if (dataStart + 16 <= wav.size()) {
                channels = qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar *>(wav.constData() + dataStart + 2));
                sampleRate = int(qFromLittleEndian<quint32>(
                    reinterpret_cast<const uchar *>(wav.constData() + dataStart + 4)));
                bitsPerSample = qFromLittleEndian<quint16>(
                    reinterpret_cast<const uchar *>(wav.constData() + dataStart + 14));
            }
        } else if (chunkId[0] == 'd' && chunkId[1] == 'a' && chunkId[2] == 't' && chunkId[3] == 'a') {
            dataBytes = int(chunkSize);
            break;
        }
        // Chunk sizes are word-aligned.
        offset = dataStart + int(chunkSize) + (int(chunkSize) & 1);
    }

    if (sampleRate <= 0 || channels <= 0 || bitsPerSample <= 0 || dataBytes <= 0) {
        return 0;
    }
    const qint64 bytesPerSec = qint64(sampleRate) * channels * (bitsPerSample / 8);
    if (bytesPerSec <= 0) {
        return 0;
    }
    return (qint64(dataBytes) * 1000) / bytesPerSec;
}

} // namespace

PlaybackController::PlaybackController(PiperClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{
    m_audioOutput = new QAudioOutput(&m_player);
    m_audioOutput->setVolume(1.0f);
    m_player.setAudioOutput(m_audioOutput);

    connect(m_client, &PiperClient::audioReady, this, &PlaybackController::onAudioReady);
    connect(m_client, &PiperClient::synthesisError, this, &PlaybackController::onSynthesisError);
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, &PlaybackController::onMediaStatusChanged);
    connect(&m_player, &QMediaPlayer::positionChanged, this, &PlaybackController::onPlayerPositionChanged);
    connect(&m_player, &QMediaPlayer::durationChanged, this, &PlaybackController::onPlayerDurationChanged);
    connect(&m_player, &QMediaPlayer::errorOccurred, this, &PlaybackController::onPlayerError);
}

qint64 PlaybackController::currentPositionMs() const
{
    return m_player.position();
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
    return qBound(0.0, double(m_player.position()) / double(duration), 1.0);
}

qint64 PlaybackController::durationForCurrentSentence() const
{
    const qint64 playerDuration = m_player.duration();
    if (playerDuration > 0) {
        return playerDuration;
    }
    if (m_currentIndex >= 0 && m_currentIndex < m_sentences.size()) {
        return m_audioDurationMs.value(m_sentences[m_currentIndex].id, 0);
    }
    return 0;
}

qint64 PlaybackController::cachedDurationMs(int sentenceId) const
{
    return m_audioDurationMs.value(sentenceId, 0);
}

void PlaybackController::setVolume(float volume)
{
    if (!m_audioOutput) {
        return;
    }
    m_audioOutput->setVolume(qBound(0.0f, volume, 1.0f));
}

float PlaybackController::volume() const
{
    return m_audioOutput ? m_audioOutput->volume() : 1.0f;
}

void PlaybackController::setMuted(bool muted)
{
    if (m_audioOutput) {
        m_audioOutput->setMuted(muted);
    }
}

bool PlaybackController::isMuted() const
{
    return m_audioOutput && m_audioOutput->isMuted();
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
        // Drop any in-flight playback synthesis; export owns the client now.
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

    // Resume mid-sentence after pause(). Reloading the WAV (playCurrentIfReady)
    // would restart the segment from the beginning -- that's what stop() is for.
    if (m_paused) {
        m_paused = false;
        m_player.play();
        return;
    }

    playCurrentIfReady();
}

void PlaybackController::pause()
{
    if (!m_playing && m_player.playbackState() != QMediaPlayer::PlayingState) {
        return;
    }
    m_playing = false;
    // Only mark resumable if audio is actually flowing; pausing while still
    // waiting for synthesis should just cancel the "playing" intent.
    if (m_player.playbackState() == QMediaPlayer::PlayingState
        || m_player.playbackState() == QMediaPlayer::PausedState) {
        m_paused = true;
        m_player.pause();
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
    m_player.stop();
    // Drop the current media so the next play() reloads from the start.
    m_player.setSource(QUrl());
    m_currentAudioBuffer.reset();
    m_client->cancelAll();
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
            emit playbackFinished();
        }
        return;
    }

    fractionWithin = qBound(0.0, fractionWithin, 1.0);
    const bool sameSentence = (index == m_currentIndex);
    const int sentenceId = m_sentences[index].id;
    const bool audioCached = m_audioCache.contains(sentenceId);

    // Fast path: same sentence with WAV already on hand — move the playhead
    // (reloading the buffer if the player was stopped) without resynthesis.
    if (sameSentence && audioCached) {
        m_pendingSeekFraction = fractionWithin;
        m_currentIndex = index;
        if (!m_currentAudioBuffer) {
            // Player was stopped/cleared but cache still has the WAV.
            m_currentAudioBuffer = std::make_unique<QBuffer>();
            m_currentAudioBuffer->setData(m_audioCache.value(sentenceId));
            m_currentAudioBuffer->open(QIODevice::ReadOnly);
            m_player.setSourceDevice(m_currentAudioBuffer.get(), QUrl("wav-audio://sentence"));
            if (m_playing) {
                m_player.play();
            }
        }
        applyPendingSeekIfPossible();
        const qint64 duration = durationForCurrentSentence();
        const qint64 position = duration > 0
            ? qint64(fractionWithin * double(duration) + 0.5)
            : m_player.position();
        emit audioPositionChanged(position, duration);
        return;
    }

    const bool wasPlaying = m_playing;
    m_paused = false;
    m_pendingSeekFraction = fractionWithin;
    m_player.stop();
    m_player.setSource(QUrl());
    m_currentAudioBuffer.reset();
    m_currentIndex = index;

    if (wasPlaying) {
        play();
    } else {
        // Prefetch so Play can start mid-segment without an extra wait.
        requestLookahead();
        const qint64 duration = durationForCurrentSentence();
        const qint64 position = (duration > 0 && m_pendingSeekFraction >= 0.0)
            ? qint64(m_pendingSeekFraction * double(duration) + 0.5)
            : 0;
        // Report the *intended* position so the UI fraction is not reset to 0
        // while audio is still loading.
        emit audioPositionChanged(position, duration);
    }
}

void PlaybackController::setSpeed(double speed)
{
    // Same cancel/clear/re-request pattern as setVoice: length_scale is
    // baked into the WAV, so cached lookahead audio would keep the old
    // speed. The sentence already playing is left alone.
    m_client->cancelAll();
    m_client->setSpeed(speed);
    m_player.setPlaybackRate(1.0); // never resample; avoid pitch distortion
    m_audioCache.clear();
    m_audioDurationMs.clear();
    if (m_playing) {
        requestLookahead();
    }
}

void PlaybackController::setVoice(const QString &voice)
{
    // Order matters: cancel server-side in-flight synthesis for the *old*
    // voice first (asyncio.CancelledError there means no stale audio
    // message ever arrives for those requests), then update the session's
    // voice, then drop anything we already cached under the old voice, and
    // finally re-request what we still need so it comes back in the new
    // voice.
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
        m_waitingForAudio = true; // onAudioReady will resume playback when it arrives
        m_paused = false;
        emit preparingAudio(s.id);
        return;
    }
    m_waitingForAudio = false;
    m_paused = false;

    m_currentAudioBuffer = std::make_unique<QBuffer>();
    m_currentAudioBuffer->setData(it.value());
    m_currentAudioBuffer->open(QIODevice::ReadOnly);

    m_player.setSourceDevice(m_currentAudioBuffer.get(), QUrl("wav-audio://sentence"));
    // Seek before play when we already know the WAV duration so playback
    // does not audibly start at 0 and then jump.
    applyPendingSeekIfPossible();
    m_player.play();
    // Retry after play — some backends only become seekable once started.
    applyPendingSeekIfPossible();

    emit sentenceStarted(s.id, s.start, s.end);
    emit audioPositionChanged(m_player.position(), durationForCurrentSentence());
    requestLookahead();
}

bool PlaybackController::applyPendingSeekIfPossible()
{
    if (m_pendingSeekFraction < 0.0) {
        return true; // nothing pending
    }
    const qint64 duration = durationForCurrentSentence();
    if (duration <= 0) {
        return false;
    }
    const qint64 target = static_cast<qint64>(m_pendingSeekFraction * double(duration) + 0.5);
    m_player.setPosition(qBound(qint64(0), target, qMax(qint64(0), duration - 1)));
    m_pendingSeekFraction = -1.0;
    return true;
}

void PlaybackController::advanceToNext()
{
    const int finishedIndex = m_currentIndex;
    // Capture duration before dropping the cache entry — needed to report
    // EOF as the end of the last segment, not position 0.
    const qint64 finishedDuration = qMax(m_player.duration(),
        m_audioDurationMs.value(m_sentences[finishedIndex].id, qint64(0)));

    // Drop PCM to free memory; keep duration so progress labels can use real
    // lengths for sentences already spoken (and for lookahead still cached).
    m_audioCache.remove(m_sentences[finishedIndex].id);
    m_pendingSeekFraction = -1.0;
    ++m_currentIndex;
    m_paused = false;
    if (m_currentIndex >= m_sentences.size()) {
        m_playing = false;
        // Stay parked on the last sentence at the end so the seek bar and
        // highlight remain at EOF, not the start of the last segment
        // (player.stop() would otherwise report position 0).
        m_currentIndex = m_sentences.size() - 1;
        m_player.stop();
        m_player.setSource(QUrl());
        m_currentAudioBuffer.reset();
        if (finishedDuration > 0) {
            emit audioPositionChanged(finishedDuration, finishedDuration);
        }
        emit playbackFinished();
        return;
    }
    playCurrentIfReady();
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
    // Skip the broken sentence rather than stalling playback entirely.
    if (m_playing && m_currentIndex >= 0 && m_currentIndex < m_sentences.size()
        && m_sentences[m_currentIndex].id == id) {
        advanceToNext();
    }
}

void PlaybackController::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    if (status == QMediaPlayer::EndOfMedia && m_playing) {
        // Only advance when we were not still trying to land a seek near the end.
        m_pendingSeekFraction = -1.0;
        advanceToNext();
        return;
    }
    if (status == QMediaPlayer::BufferedMedia || status == QMediaPlayer::LoadedMedia
        || status == QMediaPlayer::BufferingMedia) {
        if (applyPendingSeekIfPossible()) {
            emit audioPositionChanged(m_player.position(), durationForCurrentSentence());
        }
    }
}

void PlaybackController::onPlayerPositionChanged(qint64 position)
{
    // If a seek is still pending and we now have duration, land it.
    applyPendingSeekIfPossible();
    emit audioPositionChanged(position, durationForCurrentSentence());
}

void PlaybackController::onPlayerDurationChanged(qint64 /*duration*/)
{
    applyPendingSeekIfPossible();
    emit audioPositionChanged(m_player.position(), durationForCurrentSentence());
}

void PlaybackController::onPlayerError(QMediaPlayer::Error error, const QString &errorString)
{
    if (error == QMediaPlayer::NoError) {
        return;
    }
    const QString msg = errorString.isEmpty()
        ? QStringLiteral("Media playback error (%1)").arg(int(error))
        : errorString;
    emit errorOccurred(msg);
    // Skip this sentence rather than stalling forever on a bad buffer.
    if (m_playing) {
        advanceToNext();
    }
}
