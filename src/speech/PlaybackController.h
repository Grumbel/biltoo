// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QBuffer>
#include <QHash>
#include <QObject>
#include <QVector>
#include <memory>

#include "PiperClient.h"
#include "Sentence.h"

class QAudioSink;
class QTimer;

// Drives sentence-by-sentence playback:
//  - keeps `lookahead` sentences' worth of audio requested ahead of playback
//  - plays each sentence's WAV PCM via QAudioSink (works when QMediaPlayer
//    has no multimedia backend — common under Nix without pipewire plugins)
//  - emits sentenceStarted(id) for UI highlight
//
// Not responsible for: socket I/O (PiperClient) or document rendering.
class PlaybackController : public QObject {
    Q_OBJECT
public:
    static constexpr double kMinSpeed = 0.5;
    static constexpr double kMaxSpeed = 5.0;

    explicit PlaybackController(PiperClient *client, QObject *parent = nullptr);
    ~PlaybackController() override;

    void loadSentences(const QVector<Sentence> &sentences);
    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void seekToSentence(int sentenceIndex, double fractionWithin = 0.0);
    int currentIndex() const { return m_currentIndex; }
    qint64 currentPositionMs() const;
    qint64 currentDurationMs() const;
    double currentFraction() const;

    void setSpeed(double speed);
    void setVoice(const QString &voice);
    void setLookahead(int count) { m_lookahead = count; }

    void setVolume(float volume);
    float volume() const;
    void setMuted(bool muted);
    bool isMuted() const;

    bool isPlaying() const;

    void setSuspended(bool suspended);
    bool isSuspended() const { return m_suspended; }

signals:
    void sentenceStarted(int sentenceId, int start, int end);
    void preparingAudio(int sentenceId);
    void audioPositionChanged(qint64 positionMs, qint64 durationMs);
    void playbackFinished();
    void errorOccurred(QString message);

private slots:
    void onAudioReady(int id, int sampleRate, const QByteArray &wav);
    void onSynthesisError(int id, const QString &message);
    void onSinkStateChanged();
    void onPositionTick();

private:
    void requestLookahead();
    void playCurrentIfReady();
    void advanceToNext();
    void stopSink();
    qint64 durationForCurrentSentence() const;

    PiperClient *m_client = nullptr; // not owned

    QAudioSink *m_sink = nullptr;
    std::unique_ptr<QBuffer> m_pcmBuffer;
    QTimer *m_positionTimer = nullptr;

    QVector<Sentence> m_sentences;
    int m_currentIndex = -1;
    int m_lookahead = 2;

    QHash<int, QByteArray> m_audioCache; // full WAV bytes by sentence id
    QHash<int, qint64> m_audioDurationMs;

    bool m_waitingForAudio = false;
    bool m_playing = false;
    bool m_suspended = false;
    bool m_paused = false;
    float m_volume = 1.0f;
    bool m_muted = false;
    double m_pendingSeekFraction = -1.0;
    qint64 m_playStartMs = 0; // elapsedRealtime at sink start (for position)
    qint64 m_seekOffsetMs = 0;
};

