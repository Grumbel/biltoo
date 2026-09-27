// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "texttospeechcontroller.h"

#include "PiperServerManager.h"
#include "PlaybackController.h"
#include "SentenceSplitter.h"

#include <QCoreApplication>

TextToSpeechController::TextToSpeechController(QObject *parent)
    : QObject(parent)
{
    m_client = new PiperClient(this);
    m_playback = new PlaybackController(m_client, this);

    connect(m_client, &PiperClient::ready, this, &TextToSpeechController::onServerReady);
    connect(m_client, &PiperClient::connectionError, this,
            &TextToSpeechController::onConnectionError);
    connect(m_client, &PiperClient::disconnected, this, &TextToSpeechController::onDisconnected);

    connect(m_playback, &PlaybackController::playbackFinished, this,
            &TextToSpeechController::onPlaybackFinished);
    connect(m_playback, &PlaybackController::errorOccurred, this,
            &TextToSpeechController::onPlaybackError);

    setStatus(tr("TTS idle"));
}

TextToSpeechController::~TextToSpeechController()
{
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

void TextToSpeechController::ensureConnected()
{
    if (m_client->isConnected() || m_connectPending) {
        return;
    }

    if (!m_externalSocket.isEmpty()) {
        m_connectPending = true;
        setStatus(tr("Connecting to Piper…"));
        m_client->connectToServer(m_externalSocket);
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
                        qDebug("piper-server: %s", qPrintable(t));
                    }
                });
    }

    m_connectPending = true;
    setStatus(tr("Starting Piper…"));
    const QString socketPath = m_serverManager->startWithUniqueSocket();
    if (socketPath.isEmpty()) {
        m_connectPending = false;
        return;
    }
    m_ownServer = true;
    m_client->connectToServer(socketPath);
}

void TextToSpeechController::speakText(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        setStatus(tr("No text to speak"));
        emit errorOccurred(m_status);
        return;
    }

    if (m_ready && m_realAudio && m_client->isConnected()) {
        m_pendingSpeak.clear();
        const QVector<Sentence> sentences = SentenceSplitter::split(trimmed, /*startId=*/0);
        if (sentences.isEmpty()) {
            setStatus(tr("No sentences to speak"));
            return;
        }
        m_playback->stop();
        m_playback->loadSentences(sentences);
        m_playback->play();
        setSpeaking(true);
        setStatus(tr("Speaking…"));
        return;
    }

    m_pendingSpeak = trimmed;
    ensureConnected();
}

void TextToSpeechController::stop()
{
    m_pendingSpeak.clear();
    if (m_playback) {
        m_playback->stop();
    }
    setSpeaking(false);
    if (m_ready) {
        setStatus(tr("Stopped"));
    }
}

void TextToSpeechController::onServerReady(const PiperServerInfo &info)
{
    m_connectPending = false;
    m_ready = true;
    m_realAudio = info.realAudio;
    m_voices = info.voices;
    m_voice = info.currentVoice;

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
        m_pendingSpeak.clear();
        speakText(text);
    } else if (!m_pendingSpeak.isEmpty() && !isReady()) {
        m_pendingSpeak.clear();
        emit errorOccurred(m_status);
    }
}

void TextToSpeechController::onConnectionError(const QString &message)
{
    m_connectPending = false;
    m_ready = false;
    m_realAudio = false;
    setSpeaking(false);
    setStatus(message.isEmpty() ? tr("Piper connection failed") : message);
    emit readyChanged(false);
    emit errorOccurred(m_status);
    m_pendingSpeak.clear();
}

void TextToSpeechController::onDisconnected()
{
    m_connectPending = false;
    m_ready = false;
    setSpeaking(false);
    emit readyChanged(false);
    if (m_status.isEmpty() || m_status == tr("Speaking…")) {
        setStatus(tr("Piper disconnected"));
    }
}

void TextToSpeechController::onPlaybackFinished()
{
    setSpeaking(false);
    setStatus(tr("Finished"));
}

void TextToSpeechController::onPlaybackError(const QString &message)
{
    setSpeaking(false);
    setStatus(message.isEmpty() ? tr("Playback error") : message);
    emit errorOccurred(m_status);
}

void TextToSpeechController::onFailedToStart(const QString &reason)
{
    m_connectPending = false;
    m_ownServer = false;
    setStatus(reason.isEmpty() ? tr("Could not start piper-server") : reason);
    emit errorOccurred(m_status);
}
