// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PiperClient.h"

#include <QDataStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtEndian>

PiperClient::PiperClient(QObject *parent) : QObject(parent)
{
    connect(&m_socket, &QLocalSocket::readyRead, this, &PiperClient::onReadyRead);
    connect(&m_socket, &QLocalSocket::disconnected, this, &PiperClient::onDisconnected);
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    connect(&m_socket, QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::error),
            this, &PiperClient::onSocketError);
#else
    connect(&m_socket, &QLocalSocket::errorOccurred, this, &PiperClient::onSocketError);
#endif
}

void PiperClient::connectToServer(const QString &socketPath)
{
    m_recvBuffer.clear();
    m_havePendingLength = false;
    m_socket.connectToServer(socketPath);
}

void PiperClient::disconnectFromServer()
{
    // abort() closes without the graceful shutdown handshake -- fine here;
    // we are tearing the whole session down.
    m_socket.abort();
    m_recvBuffer.clear();
    m_havePendingLength = false;
}

bool PiperClient::isConnected() const
{
    return m_socket.state() == QLocalSocket::ConnectedState;
}

void PiperClient::requestSpeak(int id, const QString &text)
{
    sendMessage({{"type", "speak"}, {"id", id}, {"text", text}});
}

void PiperClient::cancel(int id)
{
    sendMessage({{"type", "stop"}, {"id", id}});
}

void PiperClient::cancelAll()
{
    sendMessage({{"type", "stop_all"}});
}

void PiperClient::setVoice(const QString &voice)
{
    sendMessage({{"type", "set_voice"}, {"voice", voice}});
}

void PiperClient::setSpeed(double speed)
{
    sendMessage({{"type", "set_speed"}, {"speed", speed}});
}

void PiperClient::requestVoiceList()
{
    sendMessage({{"type", "list_voices"}});
}

void PiperClient::sendMessage(const QJsonObject &msg)
{
    if (!isConnected()) {
        emit connectionError(QStringLiteral("not connected to piper-server"));
        return;
    }
    QByteArray body = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    QByteArray frame;
    frame.reserve(4 + body.size());
    quint32 len = qToBigEndian<quint32>(static_cast<quint32>(body.size()));
    frame.append(reinterpret_cast<const char *>(&len), sizeof(len));
    frame.append(body);
    m_socket.write(frame);
}

std::optional<QByteArray> PiperClient::tryExtractFrame()
{
    if (!m_havePendingLength) {
        if (m_recvBuffer.size() < 4) {
            return std::nullopt;
        }
        quint32 len = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(m_recvBuffer.constData()));
        m_recvBuffer.remove(0, 4);
        m_pendingFrameLength = len;
        m_havePendingLength = true;
    }
    if (static_cast<quint32>(m_recvBuffer.size()) < m_pendingFrameLength) {
        return std::nullopt;
    }
    QByteArray body = m_recvBuffer.left(m_pendingFrameLength);
    m_recvBuffer.remove(0, m_pendingFrameLength);
    m_havePendingLength = false;
    return body;
}

void PiperClient::onReadyRead()
{
    m_recvBuffer.append(m_socket.readAll());
    while (auto frame = tryExtractFrame()) {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(*frame, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            emit connectionError(QStringLiteral("malformed frame from piper-server: %1").arg(err.errorString()));
            continue;
        }
        dispatchMessage(doc.object());
    }
}

void PiperClient::dispatchMessage(const QJsonObject &msg)
{
    const QString type = msg.value("type").toString();
    if (type == "ready") {
        PiperServerInfo info;
        for (const auto &v : msg.value("voices").toArray()) {
            info.voices << v.toString();
        }
        info.currentVoice = msg.value("voice").toString();
        info.backend = msg.value("backend").toString();
        info.modelDir = msg.value("model_dir").toString();
        info.warning = msg.value("warning").toString();
        // Older servers omit "audio"; infer from backend name / voices.
        if (msg.contains(QStringLiteral("audio"))) {
            info.realAudio = msg.value("audio").toBool();
        } else if (info.backend == QLatin1String("silent-test")) {
            info.realAudio = false;
        } else {
            info.realAudio = !info.voices.isEmpty();
        }
        emit ready(info);
    } else if (type == "audio") {
        int id = msg.value("id").toInt();
        int sampleRate = msg.value("sample_rate").toInt();
        QByteArray wav = QByteArray::fromBase64(msg.value("data").toString().toLatin1());
        emit audioReady(id, sampleRate, wav);
    } else if (type == "error") {
        emit synthesisError(msg.value("id").toInt(-1), msg.value("message").toString());
    } else if (type == "voices") {
        QStringList voices;
        for (const auto &v : msg.value("voices").toArray()) {
            voices << v.toString();
        }
        emit voiceListReceived(voices);
    }
    // "ack" messages are currently informational only; add a signal if the
    // UI needs to confirm voice/speed changes took effect.
}

void PiperClient::onSocketError(QLocalSocket::LocalSocketError /*error*/)
{
    emit connectionError(m_socket.errorString());
}

void PiperClient::onDisconnected()
{
    emit disconnected();
}
