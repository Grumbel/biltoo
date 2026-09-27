// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTTOSPEECHCONTROLLER_H
#define TEXTTOSPEECHCONTROLLER_H

#include "PiperClient.h"

#include <QObject>
#include <QString>

class PiperServerManager;
class PlaybackController;
class QTimer;

/**
 * Owns piper-server lifecycle (optional), PiperClient, and PlaybackController.
 * UI calls speakText() / stop(); synthesis stays in the external server process.
 *
 * Protocol: text2sprech PROTOCOL.md (do not fork framing).
 */
class TextToSpeechController : public QObject
{
    Q_OBJECT
public:
    explicit TextToSpeechController(QObject *parent = nullptr);
    ~TextToSpeechController() override;

    /** Use an already-running server; we never stop it on destroy. */
    void setExternalSocketPath(const QString &path);

    bool isReady() const { return m_ready && m_realAudio; }
    bool isSpeaking() const { return m_speaking; }
    QString statusMessage() const { return m_status; }
    QStringList voices() const { return m_voices; }
    QString currentVoice() const { return m_voice; }

public slots:
    /** Split @p text, ensure server, play. Empty text is a no-op with status. */
    void speakText(const QString &text);
    void stop();

signals:
    void readyChanged(bool ready);
    void speakingChanged(bool speaking);
    void statusChanged(const QString &message);
    void errorOccurred(const QString &message);

private slots:
    void onServerReady(const PiperServerInfo &info);
    void onConnectionError(const QString &message);
    void onDisconnected();
    void onPlaybackFinished();
    void onPlaybackError(const QString &message);
    void onPreparingAudio(int sentenceId);
    void onSentenceStarted(int sentenceId, int start, int end);
    void onFailedToStart(const QString &reason);
    void tryConnectAttempt();
    void onSynthWatchdog();

private:
    void ensureConnected();
    void beginConnectAttempts(const QString &socketPath);
    void setStatus(const QString &msg);
    void setSpeaking(bool on);
    void clearConnectAttempts();
    void armSynthWatchdog();
    void disarmSynthWatchdog();

    PiperClient *m_client = nullptr;
    PiperServerManager *m_serverManager = nullptr; // null when using external socket
    PlaybackController *m_playback = nullptr;
    QTimer *m_connectTimer = nullptr;
    QTimer *m_synthWatchdog = nullptr;

    QString m_externalSocket;
    QString m_targetSocketPath;
    int m_connectAttemptsLeft = 0;
    bool m_ownServer = false;
    bool m_ready = false;
    bool m_realAudio = false;
    bool m_speaking = false;
    bool m_connectPending = false;
    bool m_awaitingFirstAudio = false;
    QString m_pendingSpeak;
    QString m_status;
    QStringList m_voices;
    QString m_voice;
};

#endif
