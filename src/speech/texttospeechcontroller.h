// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTTOSPEECHCONTROLLER_H
#define TEXTTOSPEECHCONTROLLER_H

#include "PiperClient.h"
#include "PlaybackController.h"

#include <QObject>
#include <QString>
#include <QStringList>

class PiperServerManager;
class QTimer;

/**
 * Owns piper-server lifecycle (optional), PiperClient, and PlaybackController.
 * UI: speakText/stop, voice, tempo (speed), volume (0–150%).
 */
class TextToSpeechController : public QObject
{
    Q_OBJECT
public:
    explicit TextToSpeechController(QObject *parent = nullptr);
    ~TextToSpeechController() override;

    void setExternalSocketPath(const QString &path);

    bool isReady() const { return m_ready && m_realAudio; }
    bool isSpeaking() const { return m_speaking; }
    QString statusMessage() const { return m_status; }
    QStringList voices() const { return m_voices; }
    QString currentVoice() const { return m_voice; }
    double speed() const { return m_speed; }
    /** Linear 0..1.5 */
    float volume() const { return m_volume; }

public slots:
    /** @p startSentence 0-based index into SentenceSplitter output. */
    void speakText(const QString &text, int startSentence = 0);
    void pause();
    void resume();
    void stop();
    void setVoice(const QString &voice);
    void setSpeed(double speed);
    void setVolume(float volume);

    bool isPaused() const;

signals:
    void readyChanged(bool ready);
    void speakingChanged(bool speaking);
    void pausedChanged(bool paused);
    void statusChanged(const QString &message);
    void errorOccurred(const QString &message);
    void voicesChanged(const QStringList &voices);
    void voiceChanged(const QString &voice);
    void speedChanged(double speed);
    void volumeChanged(float volume);
    /** Current sentence char range in the last speakText() string + progress. */
    void sentenceStarted(int sentenceId, int start, int end);
    void audioPositionChanged(qint64 positionMs, qint64 durationMs);

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
    PiperServerManager *m_serverManager = nullptr;
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
    int m_pendingStartSentence = 0;
    QString m_status;
    QStringList m_voices;
    QString m_voice;
    double m_speed = 1.0;
    float m_volume = 1.0f;
};

#endif
