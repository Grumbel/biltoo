// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "texttospeechcontroller.h"

#include "PiperServerManager.h"
#include "PlaybackController.h"
#include "SentenceSplitter.h"

#include <QTimer>
#include <QtGlobal>

namespace {
// First WAV should arrive well before this if piper-server and models are OK.
constexpr int kSynthWatchdogMs = 45000;
} // namespace

TextToSpeechController::TextToSpeechController(QObject *parent)
    : QObject(parent)
{
    m_client = new PiperClient(this);
    m_playback = new PlaybackController(m_client, this);
    m_connectTimer = new QTimer(this);
    m_connectTimer->setSingleShot(true);
    connect(m_connectTimer, &QTimer::timeout, this, &TextToSpeechController::tryConnectAttempt);

    m_synthWatchdog = new QTimer(this);
    m_synthWatchdog->setSingleShot(true);
    connect(m_synthWatchdog, &QTimer::timeout, this, &TextToSpeechController::onSynthWatchdog);

    connect(m_client, &PiperClient::ready, this, &TextToSpeechController::onServerReady);
    connect(m_client, &PiperClient::connectionError, this,
            &TextToSpeechController::onConnectionError);
    connect(m_client, &PiperClient::disconnected, this, &TextToSpeechController::onDisconnected);
    connect(m_client, &PiperClient::synthesisError, this,
            [this](int id, const QString &message) {
                setStatus(tr("Synthesis error (id %1): %2").arg(id).arg(message));
                emit errorOccurred(m_status);
            });

    connect(m_playback, &PlaybackController::playbackFinished, this,
            &TextToSpeechController::onPlaybackFinished);
    connect(m_playback, &PlaybackController::errorOccurred, this,
            &TextToSpeechController::onPlaybackError);
    connect(m_playback, &PlaybackController::preparingAudio, this,
            &TextToSpeechController::onPreparingAudio);
    connect(m_playback, &PlaybackController::sentenceStarted, this,
            &TextToSpeechController::onSentenceStarted);
    connect(m_playback, &PlaybackController::audioPositionChanged, this,
            &TextToSpeechController::audioPositionChanged);

    setStatus(tr("TTS idle"));
}

TextToSpeechController::~TextToSpeechController()
{
    clearConnectAttempts();
    disarmSynthWatchdog();
    if (m_client) {
        m_client->disconnectFromServer();
    }
    if (m_ownServer && m_serverManager) {
        m_serverManager->stop();
    }
}

void TextToSpeechController::setExternalSocketPath(const QString &path)
{
    m_externalSocket = path.trimmed();
}

void TextToSpeechController::setStatus(const QString &msg)
{
    if (m_status == msg) {
        return;
    }
    m_status = msg;
    emit statusChanged(m_status);
}

void TextToSpeechController::setSpeaking(bool on)
{
    if (m_speaking == on) {
        return;
    }
    m_speaking = on;
    emit speakingChanged(m_speaking);
}

void TextToSpeechController::clearConnectAttempts()
{
    m_connectAttemptsLeft = 0;
    if (m_connectTimer) {
        m_connectTimer->stop();
    }
}

void TextToSpeechController::armSynthWatchdog()
{
    m_awaitingFirstAudio = true;
    if (m_synthWatchdog) {
        m_synthWatchdog->start(kSynthWatchdogMs);
    }
}

void TextToSpeechController::disarmSynthWatchdog()
{
    m_awaitingFirstAudio = false;
    if (m_synthWatchdog) {
        m_synthWatchdog->stop();
    }
}

void TextToSpeechController::onSynthWatchdog()
{
    if (!m_awaitingFirstAudio) {
        return;
    }
    m_awaitingFirstAudio = false;
    if (m_playback) {
        m_playback->stop();
    }
    setSpeaking(false);
    const QString msg = tr(
        "No audio from piper-server within %1 s. Check that a voice model is "
        "installed (~/.local/share/piper/voices or TEXT2SPRECH_PIPER_MODELS) "
        "and that piper-server is not stuck.")
                            .arg(kSynthWatchdogMs / 1000);
    setStatus(msg);
    emit errorOccurred(msg);
}

void TextToSpeechController::beginConnectAttempts(const QString &socketPath)
{
    m_targetSocketPath = socketPath;
    // Match text2sprech: ~6s of retries at 200ms (Python startup + model load).
    m_connectAttemptsLeft = 30;
    m_connectPending = true;
    setStatus(tr("Connecting to Piper…"));
    tryConnectAttempt();
}

void TextToSpeechController::tryConnectAttempt()
{
    if (m_ready || m_targetSocketPath.isEmpty()) {
        return;
    }
    m_client->connectToServer(m_targetSocketPath);
}

void TextToSpeechController::ensureConnected()
{
    if (m_client->isConnected() && m_ready) {
        return;
    }
    if (m_connectPending && m_connectAttemptsLeft > 0) {
        return;
    }

    if (!m_externalSocket.isEmpty()) {
        beginConnectAttempts(m_externalSocket);
        return;
    }

    // Prefer reconnect to the socket we already own (server may still be up).
    if (m_ownServer && !m_targetSocketPath.isEmpty()) {
        beginConnectAttempts(m_targetSocketPath);
        return;
    }

    if (!m_serverManager) {
        m_serverManager = new PiperServerManager(this);
        connect(m_serverManager, &PiperServerManager::failedToStart, this,
                &TextToSpeechController::onFailedToStart);
        connect(m_serverManager, &PiperServerManager::serverOutput, this,
                [](const QString &text) {
                    const QString t = text.trimmed();
                    if (!t.isEmpty()) {
                        qWarning("piper-server: %s", qPrintable(t));
                    }
                });
    } else {
        // Previous owned process is gone or unusable; stop before respawn.
        m_serverManager->stop();
        m_ownServer = false;
    }

    setStatus(tr("Starting Piper…"));
    const QString socketPath = m_serverManager->startWithUniqueSocket();
    if (socketPath.isEmpty()) {
        m_connectPending = false;
        return;
    }
    m_ownServer = true;
    beginConnectAttempts(socketPath);
}

void TextToSpeechController::speakText(const QString &text, int startSentence)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        setStatus(tr("No text to speak"));
        emit errorOccurred(m_status);
        return;
    }

    m_pendingStartSentence = qMax(0, startSentence);

    if (m_ready && m_realAudio && m_client->isConnected()) {
        m_pendingSpeak.clear();
        const QVector<Sentence> sentences = SentenceSplitter::split(trimmed, /*startId=*/0);
        if (sentences.isEmpty()) {
            setStatus(tr("No sentences to speak"));
            return;
        }
        m_playback->stop();
        m_playback->loadSentences(sentences);
        const int idx = qBound(0, m_pendingStartSentence, sentences.size() - 1);
        m_pendingStartSentence = 0;
        if (idx > 0) {
            m_playback->seekToSentence(idx);
        }
        setSpeaking(true);
        emit pausedChanged(false);
        setStatus(tr("Synthesizing…"));
        armSynthWatchdog();
        m_playback->play();
        return;
    }

    m_pendingSpeak = trimmed;
    ensureConnected();
}

void TextToSpeechController::pause()
{
    if (!m_playback || !m_speaking) {
        return;
    }
    m_playback->pause();
    if (m_playback->isPaused()) {
        disarmSynthWatchdog();
        setStatus(tr("Paused"));
        emit pausedChanged(true);
    }
}

void TextToSpeechController::resume()
{
    if (!m_playback || !m_speaking || !m_playback->isPaused()) {
        return;
    }
    setStatus(tr("Speaking…"));
    emit pausedChanged(false);
    m_playback->play();
}

bool TextToSpeechController::isPaused() const
{
    return m_playback && m_playback->isPaused();
}

void TextToSpeechController::stop()
{
    m_pendingSpeak.clear();
    m_pendingStartSentence = 0;
    disarmSynthWatchdog();
    if (m_playback) {
        m_playback->stop();
    }
    setSpeaking(false);
    emit pausedChanged(false);
    if (m_ready || m_connectPending) {
        setStatus(tr("Stopped"));
    }
}

void TextToSpeechController::onPreparingAudio(int /*sentenceId*/)
{
    if (m_speaking) {
        setStatus(tr("Synthesizing…"));
    }
}

void TextToSpeechController::onSentenceStarted(int sentenceId, int start, int end)
{
    disarmSynthWatchdog();
    setStatus(tr("Speaking…"));
    emit sentenceStarted(sentenceId, start, end);
}

void TextToSpeechController::onServerReady(const PiperServerInfo &info)
{
    clearConnectAttempts();
    m_connectPending = false;
    m_ready = true;
    m_realAudio = info.realAudio;
    m_voices = info.voices;
    m_voice = info.currentVoice;
    emit voicesChanged(m_voices);
    emit voiceChanged(m_voice);

    // Re-apply session tempo/volume after (re)connect.
    if (m_playback) {
        m_playback->setSpeed(m_speed);
        m_playback->setVolume(m_volume);
        if (!m_voice.isEmpty()) {
            m_playback->setVoice(m_voice);
        }
    }

    if (!info.warning.isEmpty()) {
        setStatus(info.warning);
    } else if (!info.realAudio) {
        setStatus(tr("No Piper voices available"));
    } else {
        setStatus(tr("TTS ready (%1)").arg(info.currentVoice.isEmpty()
                                               ? tr("default voice")
                                               : info.currentVoice));
    }

    emit readyChanged(isReady());

    if (!m_pendingSpeak.isEmpty() && isReady()) {
        const QString text = m_pendingSpeak;
        const int start = m_pendingStartSentence;
        m_pendingSpeak.clear();
        m_pendingStartSentence = 0;
        speakText(text, start);
    } else if (!m_pendingSpeak.isEmpty() && !isReady()) {
        m_pendingSpeak.clear();
        m_pendingStartSentence = 0;
        emit errorOccurred(m_status);
    }
}

void TextToSpeechController::onConnectionError(const QString &message)
{
    // Fresh piper-server needs a moment before the socket exists; retry quietly.
    if (m_connectAttemptsLeft > 0) {
        --m_connectAttemptsLeft;
        m_connectTimer->start(200);
        return;
    }
    m_connectPending = false;
    m_ready = false;
    m_realAudio = false;
    disarmSynthWatchdog();
    setSpeaking(false);
    // Owned server is unreachable: tear it down so the next Speak can respawn.
    if (m_ownServer && m_serverManager) {
        m_serverManager->stop();
        m_ownServer = false;
        m_targetSocketPath.clear();
    }
    setStatus(message.isEmpty() ? tr("Piper connection failed") : message);
    emit readyChanged(false);
    emit errorOccurred(m_status);
    m_pendingSpeak.clear();
}

void TextToSpeechController::onDisconnected()
{
    // Disconnect during intentional retry is expected.
    if (m_connectAttemptsLeft > 0) {
        return;
    }
    m_connectPending = false;
    m_ready = false;
    disarmSynthWatchdog();
    setSpeaking(false);
    emit readyChanged(false);
    if (m_status.isEmpty() || m_status == tr("Speaking…") || m_status == tr("Synthesizing…")) {
        setStatus(tr("Piper disconnected"));
    }
}

void TextToSpeechController::onPlaybackFinished()
{
    disarmSynthWatchdog();
    setSpeaking(false);
    emit pausedChanged(false);
    setStatus(tr("Finished"));
}

void TextToSpeechController::onPlaybackError(const QString &message)
{
    disarmSynthWatchdog();
    setSpeaking(false);
    setStatus(message.isEmpty() ? tr("Playback error") : message);
    emit errorOccurred(m_status);
}

void TextToSpeechController::onFailedToStart(const QString &reason)
{
    clearConnectAttempts();
    disarmSynthWatchdog();
    m_connectPending = false;
    m_ownServer = false;
    setStatus(reason.isEmpty() ? tr("Could not start piper-server") : reason);
    emit errorOccurred(m_status);
    m_pendingSpeak.clear();
}

void TextToSpeechController::setVoice(const QString &voice)
{
    const QString v = voice.trimmed();
    if (v.isEmpty() || v == m_voice) {
        return;
    }
    m_voice = v;
    if (m_playback) {
        m_playback->setVoice(m_voice);
    }
    emit voiceChanged(m_voice);
}

void TextToSpeechController::setSpeed(double speed)
{
    const double s = qBound(PlaybackController::kMinSpeed, speed, PlaybackController::kMaxSpeed);
    if (qFuzzyCompare(s, m_speed)) {
        return;
    }
    m_speed = s;
    if (m_playback) {
        m_playback->setSpeed(m_speed);
    }
    emit speedChanged(m_speed);
}

void TextToSpeechController::setVolume(float volume)
{
    const float v = qBound(PlaybackController::kMinVolume, volume, PlaybackController::kMaxVolume);
    if (qAbs(v - m_volume) < 0.0001f) {
        return;
    }
    m_volume = v;
    if (m_playback) {
        m_playback->setVolume(m_volume);
    }
    emit volumeChanged(m_volume);
}
