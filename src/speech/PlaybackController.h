// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QBuffer>
#include <QHash>
#include <QMediaPlayer>
#include <QObject>
#include <QVector>
#include <memory>

#include "PiperClient.h"
#include "Sentence.h"

class QAudioOutput;

// Drives sentence-by-sentence playback:
//  - keeps `lookahead` sentences' worth of audio requested ahead of playback
//    so there's minimal gap between one sentence finishing and the next
//    starting (see PROTOCOL.md: requests are pipelined by id).
//  - plays each sentence's WAV via QMediaPlayer from an in-memory QBuffer.
//  - emits sentenceStarted(id) so DocumentView can highlight it.
//
// Not responsible for: talking to the socket directly (that's PiperClient),
// or rendering (that's DocumentView). Kept this way so playback logic is
// unit-testable without a real Piper server -- feed it a fake PiperClient
// signal sequence and assert on sentenceStarted order.
class PlaybackController : public QObject {
    Q_OBJECT
public:
    // Piper maps UI speed to length_scale = 1/speed; no hard upper bound in
    // the backend — these are practical UI/config limits only.
    static constexpr double kMinSpeed = 0.5;
    static constexpr double kMaxSpeed = 5.0;

    explicit PlaybackController(PiperClient *client, QObject *parent = nullptr);

    void loadSentences(const QVector<Sentence> &sentences);
    // Starts or resumes: if pause() left us mid-sentence, continues from the
    // same playback position; otherwise starts the current sentence from 0.
    void play();
    // Freezes the current sentence in place (resume with play()).
    void pause();
    // Halts playback and forgets mid-sentence position so the next play()
    // restarts the current sentence from the beginning.
    void stop();
    void next();
    void previous();
    // Jump to a sentence. fractionWithin (0..1) seeks into that sentence's
    // audio once it is loaded — 0 starts at the beginning (default).
    void seekToSentence(int sentenceIndex, double fractionWithin = 0.0);
    int currentIndex() const { return m_currentIndex; }
    // Current media timeline (ms). Prefers the live player clock; falls back
    // to the WAV-header duration when the backend has not reported one yet.
    qint64 currentPositionMs() const;
    qint64 currentDurationMs() const;
    // 0..1 through the current sentence (position / duration).
    double currentFraction() const;
    // Known WAV-header duration for a sentence id after synthesis (0 if unknown).
    // Kept after the PCM buffer is dropped so progress labels can prefer real
    // length once lookahead has filled in.
    qint64 cachedDurationMs(int sentenceId) const;
    // Speed is baked into synthesis (Piper length_scale), not playback rate.
    // Cancels in-flight work and drops cached audio so upcoming sentences
    // are re-synthesized at the new speed (same pattern as setVoice).
    void setSpeed(double speed);
    // Cancels in-flight synthesis and discards any cached-but-unplayed audio
    // before switching voices -- without this, sentences already queued
    // ahead (lookahead prefetch) or mid-synthesis when the user switches
    // keep arriving in the old voice, so playback audibly "sticks" with the
    // old voice for a sentence or two after the change. The one sentence
    // already actively playing right now is unaffected (that audio is
    // already flowing to the audio device) -- only upcoming ones.
    void setVoice(const QString &voice);
    void setLookahead(int count) { m_lookahead = count; }

    // Linear playback gain on QAudioOutput (0.0 silent … 1.0 full). Independent
    // of synthesis speed/voice; applies immediately to the current sentence.
    void setVolume(float volume);
    float volume() const;
    void setMuted(bool muted);
    bool isMuted() const;

    bool isPlaying() const;

    // While true, ignore audioReady / synthesisError so a long export can
    // monopolize PiperClient without polluting the playback cache.
    void setSuspended(bool suspended);
    bool isSuspended() const { return m_suspended; }

signals:
    void sentenceStarted(int sentenceId, int start, int end);
    // Emitted when playback wants to start a sentence but its audio hasn't
    // arrived yet -- lets the UI distinguish "buffering" from "playing"
    // instead of looking stuck with no feedback during the gap.
    void preparingAudio(int sentenceId);
    // Fires as QMediaPlayer advances through the current sentence WAV so the
    // UI can move the seek bar inside a segment, not only on sentence edges.
    void audioPositionChanged(qint64 positionMs, qint64 durationMs);
    void playbackFinished();
    void errorOccurred(QString message);

private slots:
    void onAudioReady(int id, int sampleRate, const QByteArray &wav);
    void onSynthesisError(int id, const QString &message);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onPlayerPositionChanged(qint64 position);
    void onPlayerDurationChanged(qint64 duration);
    void onPlayerError(QMediaPlayer::Error error, const QString &errorString);

private:
    void requestLookahead();
    void playCurrentIfReady();
    void advanceToNext();
    // Apply m_pendingSeekFraction once a usable duration is known (player or WAV).
    bool applyPendingSeekIfPossible();
    qint64 durationForCurrentSentence() const;

    PiperClient *m_client; // not owned
    QMediaPlayer m_player;
    QAudioOutput *m_audioOutput = nullptr; // owned by m_player as child
    std::unique_ptr<QBuffer> m_currentAudioBuffer;

    QVector<Sentence> m_sentences;
    int m_currentIndex = -1;
    int m_lookahead = 2;

    // audio for sentences we've already requested but not yet played
    QHash<int, QByteArray> m_audioCache; // key: sentence id (== index into m_sentences for v1)
    // WAV-header durations (ms), filled when audio is synthesized. Kept after
    // m_audioCache drops the PCM so progress estimates can prefer real length
    // over the word-rate fallback. Cleared on load / voice / speed change.
    QHash<int, qint64> m_audioDurationMs;
    bool m_waitingForAudio = false;
    bool m_playing = false;
    bool m_suspended = false;
    // True after pause() while the current sentence's media is still loaded;
    // play() resumes instead of reloading. Cleared by stop()/seek/new media.
    bool m_paused = false;
    // Pending 0..1 offset into the current sentence's audio. Applied as soon
    // as duration is known. Negative means "no pending seek".
    double m_pendingSeekFraction = -1.0;
};
