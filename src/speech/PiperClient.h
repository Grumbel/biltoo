// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QStringList>
#include <optional>

// Client for the text2sprech<->piper-server protocol described in PROTOCOL.md:
// 4-byte big-endian length prefix + UTF-8 JSON, over a Unix domain socket
// (QLocalSocket maps to AF_UNIX on Linux/macOS).
//
// This class only speaks the protocol; it doesn't own playback ordering or
// the sentence queue -- that's PlaybackController's job. Keeping the two
// separate means the wire protocol can be tested (see tests/) without a
// QApplication event loop pulling in Qt Multimedia.
struct PiperServerInfo {
    QStringList voices;
    QString currentVoice;
    QString backend;   // piper-python | piper-cli | silent-test (may be empty on old servers)
    bool realAudio = true; // false => silent-test or no models; do not pretend TTS works
    QString modelDir;
    QString warning;   // non-empty human message when audio is unusable
};

class PiperClient : public QObject {
    Q_OBJECT
public:
    explicit PiperClient(QObject *parent = nullptr);

    void connectToServer(const QString &socketPath);
    // Drop the socket immediately (used on app quit so piper-server is
    // not left waiting on an open client while we SIGTERM it).
    void disconnectFromServer();
    bool isConnected() const;

    void requestSpeak(int id, const QString &text);
    void cancel(int id);
    void cancelAll();
    void setVoice(const QString &voice);
    void setSpeed(double speed);
    void requestVoiceList();

signals:
    void ready(const PiperServerInfo &info);
    // sampleRate in Hz, wavData is a complete RIFF/WAV byte buffer for one sentence
    void audioReady(int id, int sampleRate, const QByteArray &wavData);
    void synthesisError(int id, const QString &message);
    void voiceListReceived(const QStringList &voices);
    void connectionError(const QString &message);
    void disconnected();

private slots:
    void onReadyRead();
    void onSocketError(QLocalSocket::LocalSocketError error);
    void onDisconnected();

private:
    void sendMessage(const QJsonObject &msg);
    void dispatchMessage(const QJsonObject &msg);
    // Tries to pull one complete length-prefixed frame out of m_recvBuffer.
    // Returns std::nullopt if a full frame isn't buffered yet.
    std::optional<QByteArray> tryExtractFrame();

    QLocalSocket m_socket;
    QByteArray m_recvBuffer;
    quint32 m_pendingFrameLength = 0;
    bool m_havePendingLength = false;
};
