// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PiperServerManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

namespace {

QString defaultUserVoicesDir()
{
    // Matches piper_server/synth.py default_user_voices_dir().
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("text2sprech/voices"));
}

QString legacyPiperVoicesDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("piper/voices"));
}

QStringList voiceModelSearchPath()
{
    QStringList dirs;
    QProcessEnvironment sysEnv = QProcessEnvironment::systemEnvironment();
    const QString fromEnv = sysEnv.value(QStringLiteral("TEXT2SPRECH_PIPER_MODELS"));
    if (!fromEnv.isEmpty()) {
        dirs = fromEnv.split(QDir::listSeparator(), Qt::SkipEmptyParts);
    } else {
        dirs << defaultUserVoicesDir();
        dirs << legacyPiperVoicesDir();
    }

    QSettings settings;
    settings.beginGroup(QStringLiteral("voices"));
    const QStringList extra = settings.value(QStringLiteral("extraDirs")).toStringList();
    settings.endGroup();
    for (const QString &path : extra) {
        const QString trimmed = path.trimmed();
        if (!trimmed.isEmpty() && !dirs.contains(trimmed)) {
            dirs << trimmed;
        }
    }
    return dirs;
}

} // namespace

PiperServerManager::PiperServerManager(QObject *parent) : QObject(parent)
{
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        emit serverOutput(QString::fromUtf8(m_process.readAllStandardError()));
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        emit serverOutput(QString::fromUtf8(m_process.readAllStandardOutput()));
    });
}

PiperServerManager::~PiperServerManager()
{
    stop();
}

QString PiperServerManager::generateUniqueSocketPath()
{
    // PID alone isn't quite enough -- a stale socket file left behind by a
    // crashed previous run (or a reused PID after a reboot) could collide.
    // A UUID guarantees no two instances, even started in the same
    // millisecond, ever pick the same path.
    QString name = QStringLiteral("text2sprech-piper-%1-%2.sock")
        .arg(QCoreApplication::applicationPid())
        .arg(QUuid::createUuid().toString(QUuid::Id128));
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(name);
}

QString PiperServerManager::findServerCommand(QStringList *argsOut)
{
    // 1. Explicit override -- e.g. set by a packaging wrapper, or by a user
    //    who has their own preferred piper-server binary.
    QString envBin = QString::fromLocal8Bit(qgetenv("TEXT2SPRECH_PIPER_SERVER_BIN"));
    if (!envBin.isEmpty() && QFileInfo::exists(envBin)) {
        return envBin;
    }

    // 2. An installed `piper-server` launcher on PATH -- this is what the
    //    flake's piper-server package provides, and flake.nix wraps the
    //    `text2sprech` binary's PATH to include it (see qtWrapperArgs there).
    QString onPath = QStandardPaths::findExecutable(QStringLiteral("piper-server"));
    if (!onPath.isEmpty()) {
        return onPath;
    }

    // 3. Packaged layout next to the binary:
    //    <prefix>/bin/text2sprech → <prefix>/share/text2sprech/piper_server/server.py
    //    (cmake install places the Python modules there for non-Nix installs;
    //    the flake still prefers PATH via option 2).
    {
        const QString packaged = QDir(QCoreApplication::applicationDirPath())
                                     .filePath(QStringLiteral("../share/text2sprech/piper_server/server.py"));
        if (QFileInfo::exists(packaged)) {
            QString python3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
            if (!python3.isEmpty()) {
                *argsOut << QFileInfo(packaged).absoluteFilePath();
                return python3;
            }
        }
    }

    // 4. Dev-tree fallback: source path baked in at configure time
    //    (CMakeLists.txt) so `cmake --build && ./build/text2sprech` works
    //    without installing.
#ifdef PIPER_SERVER_SCRIPT_PATH
    QString script = QStringLiteral(PIPER_SERVER_SCRIPT_PATH);
    if (QFileInfo::exists(script)) {
        QString python3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
        if (!python3.isEmpty()) {
            *argsOut << script;
            return python3;
        }
    }
#endif
    return QString();
}

QString PiperServerManager::startWithUniqueSocket()
{
    QString socketPath = generateUniqueSocketPath();
    QStringList args;
    QString command = findServerCommand(&args);
    if (command.isEmpty()) {
        emit failedToStart(tr(
            "No piper-server executable or script found. Install the piper-server "
            "package (see flake.nix), set TEXT2SPRECH_PIPER_SERVER_BIN, or start one "
            "yourself and pass --piper-socket."));
        return QString();
    }

    args << "--socket" << socketPath;
    m_process.setProgram(command);
    m_process.setArguments(args);

    // Tell the server where to look for *.onnx voices (defaults + Preferences extras).
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("TEXT2SPRECH_PIPER_MODELS"),
               voiceModelSearchPath().join(QDir::listSeparator()));
    m_process.setProcessEnvironment(env);

    // Ensure the primary drop-in directory exists so users have somewhere obvious.
    QDir().mkpath(defaultUserVoicesDir());

    m_process.start();
    if (!m_process.waitForStarted(3000)) {
        emit failedToStart(tr("piper-server failed to start (%1): %2")
                                .arg(command, m_process.errorString()));
        return QString();
    }
    m_started = true;
    return socketPath;
}

void PiperServerManager::stop()
{
    if (!m_started || m_process.state() == QProcess::NotRunning) {
        return;
    }
    // SIGTERM first (server.py registers a handler). Keep the wait short so
    // the GUI close path does not sit blocked for seconds -- if the process
    // does not exit promptly, escalate to SIGKILL.
    m_process.terminate();
    if (!m_process.waitForFinished(400)) {
        m_process.kill();
        m_process.waitForFinished(200);
    }
    m_started = false;
}
