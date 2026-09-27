// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

// Owns a locally-spawned piper-server child process for the common case
// where the user hasn't pointed us at an already-running one via
// --piper-socket. Only ever used in that "we own the process" case --
// MainWindow skips this entirely when an external socket was given, since
// we should never kill a server we didn't start.
class PiperServerManager : public QObject {
    Q_OBJECT
public:
    explicit PiperServerManager(QObject *parent = nullptr);
    ~PiperServerManager() override;

    // Picks a unique socket path (safe for multiple app instances running
    // at once) and starts the bundled piper-server pointed at it. Returns
    // the socket path on success, or an empty string if no usable
    // piper-server executable/script could be located (failedToStart is
    // emitted with details in that case).
    QString startWithUniqueSocket();

    // Best-effort graceful shutdown: SIGTERM (which server.py already
    // handles cleanly via its signal handler), escalating to SIGKILL if it
    // hasn't exited after a short grace period. Safe to call even if
    // startWithUniqueSocket() was never called or failed.
    void stop();

signals:
    // Passthrough of the child's stdout/stderr, for diagnostics -- this is
    // intentionally not surfaced anywhere in the UI (the process should be
    // invisible to the user); MainWindow just forwards it to qDebug.
    void serverOutput(const QString &text);
    void failedToStart(const QString &reason);

private:
    static QString generateUniqueSocketPath();
    // Locates a command to run the server; appends any extra args needed
    // (e.g. a script path when falling back to invoking python3 directly).
    static QString findServerCommand(QStringList *argsOut);

    QProcess m_process;
    bool m_started = false;
};
