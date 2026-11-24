// Copyright (c) 2014-2024, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "DaemonManager.h"
#include "main/sm64helpers.h" // SM64COIN
#include "common/util.h"
#include <QElapsedTimer>
#include <QFile>
#include <QMutexLocker>
#include <QThread>
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <QUrl>
#include <QtConcurrent/QtConcurrent>
#include <QApplication>
#include <QProcess>
#include <QStorageInfo>
#include <QVariantMap>
#include <QVariant>
#include <QMap>

namespace {
    static const int DAEMON_START_TIMEOUT_SECONDS = 120;
}

bool DaemonManager::start(const QString &flags, NetworkType::Type nettype, const QString &dataDir, const QString &bootstrapNodeAddress, bool noSync /* = false*/, bool pruneBlockchain /* = false*/)
{
    if (!QFileInfo(m_monerod).isFile())
    {
        emit daemonStartFailure("\"" + QDir::toNativeSeparators(m_monerod) + "\" " + tr("executable is missing"));
        return false;
    }

    // prepare command line arguments and pass to monerod
    QStringList arguments;

    // SM64COIN: no --detach: the node is a child of the wallet, so an early exit (missing ROM,
    // port in use, ...) is seen and its output shown. The wallet stops it on exit.

    if (nettype == NetworkType::TESTNET)
        arguments << "--testnet";
    else if (nettype == NetworkType::STAGENET)
        arguments << "--stagenet";

    // SM64COIN: the commands sent to this node (sync_info, exit) must use its RPC port.
    m_rpcArgs.clear();
    {
        const QStringList f = flags.split(" ", Qt::SkipEmptyParts);
        for (int i = 0; i < f.size(); ++i) {
            if (f[i] == "--rpc-bind-port" && i + 1 < f.size())
                m_rpcArgs << "--rpc-bind-port" << f[i + 1];
            else if (f[i].startsWith("--rpc-bind-port="))
                m_rpcArgs << "--rpc-bind-port" << f[i].mid(16);
        }
    }

    // Custom startup flags for daemon
    foreach (const QString &str, flags.split(" ")) {
          qDebug() << QString(" [%1] ").arg(str);
          if (!str.isEmpty())
            arguments << str;
    }

    // Custom data-dir
    if(!dataDir.isEmpty()) {
        arguments << "--data-dir" << dataDir;
    }

    // Bootstrap node address
    if(!bootstrapNodeAddress.isEmpty()) {
        arguments << "--bootstrap-daemon-address" << bootstrapNodeAddress;
    }

    if (pruneBlockchain) {
        if (!checkLmdbExists(dataDir)) { // check that DB has not already been created
            arguments << "--prune-blockchain";
        }
    }

    if (noSync) {
        arguments << "--no-sync";
    }

    arguments << "--check-updates" << "disabled";
    arguments << "--non-interactive";

    // SM64Coin: the website on GitHub Pages calls this node from the browser (the launcher
    // does the same; the RPC stays on 127.0.0.1).
    if (!flags.contains("--rpc-access-control-origins")) {
        arguments << "--rpc-access-control-origins" << "https://gumbo64.github.io";
    }
    // SM64Coin: every node replays the blocks it verifies, so it needs the ROM; the daemon itself only
    // looks in its data dir, $SM64COIN_ROM, ./ and ~/.sm64coin/, not in the bundle folder.
    const QString sm64Rom = flags.contains("--sm64-rom") ? QString() : sm64paths::findRom();
    if (!sm64Rom.isEmpty()) {
        arguments << "--sm64-rom" << sm64Rom;
    }
    // SM64Coin: the daemon knows no seed nodes (Monero's are removed); like the launcher, give it
    // seeds.txt's as priority peers, unless the user's flags choose the peers.
    if (!flags.contains("--add-exclusive-node") && !flags.contains("--add-priority-node")) {
        for (const QString &peer : sm64paths::seedPeers()) {
            arguments << "--add-peer" << peer << "--add-priority-node" << peer;
        }
    }

    // --max-concurrency based on threads available.
    int32_t concurrency = qMax(1, QThread::idealThreadCount() / 2);

    if(!flags.contains("--max-concurrency", Qt::CaseSensitive)){
        arguments << "--max-concurrency" << QString::number(concurrency);
    }

    qDebug() << "starting monerod " + m_monerod;
    qDebug() << "With command line arguments " << arguments;

    QMutexLocker locker(&m_daemonMutex);

    m_daemon.reset(new QProcess());
    m_exitedEarly = false;
    m_startConfirmed = false;
    m_tail.clear();

    // Connect output slots
    connect(m_daemon.get(), SIGNAL(readyReadStandardOutput()), this, SLOT(printOutput()));
    connect(m_daemon.get(), SIGNAL(readyReadStandardError()), this, SLOT(printError()));
    connect(m_daemon.get(), SIGNAL(stateChanged(QProcess::ProcessState)), this, SLOT(stateChanged(QProcess::ProcessState)));
    connect(m_daemon.get(), SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(onDaemonFinished(int, QProcess::ExitStatus)));

    // Start monerod (SM64COIN: as a child process)
    locker.unlock(); // its output slots take the mutex
    m_daemon->start(m_monerod, arguments);
    const bool started = m_daemon->waitForStarted(10000);

    if (!started) {
        qDebug() << "Daemon start error: " + m_daemon->errorString();
        emit daemonStartFailure(m_daemon->errorString());
        return false;
    }

    // Start start watcher
    m_scheduler.run([this, nettype, dataDir, noSync] {
        if (startWatcher(nettype, dataDir)) {
            m_startConfirmed = true;
            emit daemonStarted();
            m_noSync = noSync;
        } else if (m_exitedEarly) {
            // SM64COIN: onDaemonFinished already reported why
        } else {
            emit daemonStartFailure(tr("Timed out, local node is not responding after %1 seconds").arg(DAEMON_START_TIMEOUT_SECONDS));
        }
    });

    return true;
}

void DaemonManager::stopAsync(NetworkType::Type nettype, const QString &dataDir, const QJSValue& callback)
{
    const auto feature = m_scheduler.run([this, nettype, dataDir] {
        QString message;
        sendCommand({"exit"}, nettype, dataDir, message);

        return QJSValueList{QJSValue(stopWatcher(nettype, dataDir))};
    }, callback);

    if (!feature.first)
    {
        QJSValue(callback).call(QJSValueList{QJSValue(false)});
    }
}

bool DaemonManager::startWatcher(NetworkType::Type nettype, const QString &dataDir) const
{
    // Check if daemon is started every 2 seconds
    QElapsedTimer timer;
    timer.start();
    while(true && !m_app_exit && !m_exitedEarly && timer.elapsed() / 1000 < DAEMON_START_TIMEOUT_SECONDS  ) {
        QThread::sleep(2);
        if (m_exitedEarly) return false; // SM64COIN
        if(!running(nettype, dataDir)) {
            qDebug() << "daemon not running. checking again in 2 seconds.";
        } else {
            qDebug() << "daemon is started. Waiting 5 seconds to let daemon catch up";
            QThread::sleep(5);
            return true;
        }
    }
    return false;
}

bool DaemonManager::stopWatcher(NetworkType::Type nettype, const QString &dataDir) const
{
    // Check if daemon is running every 2 seconds. Kill if still running after 10 seconds
    int counter = 0;
    while(true && !m_app_exit) {
        QThread::sleep(2);
        counter++;
        if(running(nettype, dataDir)) {
            qDebug() << "Daemon still running.  " << counter;
            if(counter >= 5) {
                qDebug() << "Killing it! ";
#ifdef Q_OS_WIN
                QProcess::execute("taskkill",  {"/F", "/IM", "monerod.exe"});
#else
                QProcess::execute("pkill", {"monerod"});
#endif
            }

        } else
            return true;
    }
    return false;
}


void DaemonManager::stateChanged(QProcess::ProcessState state)
{
    qDebug() << "STATE CHANGED: " << state;
    if (state == QProcess::NotRunning) {
        emit daemonStopped();
    }
}

// SM64COIN
void DaemonManager::rememberOutput(const QString &line)
{
    const QString t = line.trimmed();
    if (t.isEmpty()) return;
    m_tail << t;
    while (m_tail.size() > 40) m_tail.removeFirst();
}

// SM64COIN: the node exited. Before it answered: tell the user why.
void DaemonManager::onDaemonFinished(int exitCode, QProcess::ExitStatus status)
{
    if (m_startConfirmed || m_app_exit) return;
    m_exitedEarly = true;
    // flush what is left of its output
    printOutput();
    printError();
    QString msg = status == QProcess::CrashExit ? tr("The node crashed right after starting.")
                                                 : tr("The node exited right after starting (exit code %1).").arg(exitCode);
    const QString all = m_tail.join("\n");
    if (all.contains("Address already in use") || all.contains("Failed to bind") || all.contains("bind: "))
        msg += "\n\n" + tr("A network port it needs is already in use, probably by another SM64Coin node that is already running. Close it (or use other ports in the daemon flags) and try again.");
    else if (all.contains("ROM", Qt::CaseInsensitive))
        msg += "\n\n" + tr("The node needs the US Super Mario 64 ROM (baserom.us.z64).");
    else if (all.contains("lock", Qt::CaseInsensitive) && all.contains("lmdb", Qt::CaseInsensitive))
        msg += "\n\n" + tr("The node's database is in use by another node.");
    QStringList last = m_tail.mid(qMax(0, m_tail.size() - 6));
    if (!last.isEmpty())
        msg += "\n\n" + tr("The node said:") + "\n" + last.join("\n");
    emit daemonStartFailure(msg);
}

void DaemonManager::printOutput()
{
    QByteArray byteArray = [this]() {
        QMutexLocker locker(&m_daemonMutex);
        return m_daemon->readAllStandardOutput();
    }();
    QStringList strLines = QString(byteArray).split("\n");

    foreach (QString line, strLines) {
        emit daemonConsoleUpdated(line);
        rememberOutput(line); // SM64COIN
        qDebug() << "Daemon: " + line;
    }
}

void DaemonManager::printError()
{
    QByteArray byteArray = [this]() {
        QMutexLocker locker(&m_daemonMutex);
        return m_daemon->readAllStandardError();
    }();
    QStringList strLines = QString(byteArray).split("\n");

    foreach (QString line, strLines) {
        emit daemonConsoleUpdated(line);
        rememberOutput(line); // SM64COIN
        qDebug() << "Daemon ERROR: " + line;
    }
}

bool DaemonManager::running(NetworkType::Type nettype, const QString &dataDir) const
{ 
    QString status;
    sendCommand({"sync_info"}, nettype, dataDir, status);
    qDebug() << status;
    return status.contains("Height:");
}

bool DaemonManager::noSync() const noexcept
{
    return m_noSync;
}

void DaemonManager::runningAsync(NetworkType::Type nettype, const QString &dataDir, const QJSValue& callback) const
{ 
    m_scheduler.run([this, nettype, dataDir] {
        return QJSValueList{QJSValue(running(nettype, dataDir))};
    }, callback);
}

bool DaemonManager::sendCommand(const QStringList &cmd, NetworkType::Type nettype, const QString &dataDir, QString &message) const
{
    QProcess p;
    QStringList external_cmd(cmd);

    // Add network type flag if needed
    if (nettype == NetworkType::TESTNET)
        external_cmd << "--testnet";
    else if (nettype == NetworkType::STAGENET)
        external_cmd << "--stagenet";

    // Custom data-dir
    if (!dataDir.isEmpty()) {
        external_cmd << "--data-dir" << dataDir;
    }

    external_cmd << m_rpcArgs; // SM64COIN

    qDebug() << "sending external cmd: " << external_cmd;


    p.start(m_monerod, external_cmd);

    bool started = p.waitForFinished(-1);
    message = p.readAllStandardOutput();
    emit daemonConsoleUpdated(message);
    return started;
}

void DaemonManager::sendCommandAsync(const QStringList &cmd, NetworkType::Type nettype, const QString &dataDir, const QJSValue& callback) const
{
    m_scheduler.run([this, cmd, nettype, dataDir] {
        QString message;
        return QJSValueList{QJSValue(sendCommand(cmd, nettype, dataDir, message))};
    }, callback);
}

void DaemonManager::exit()
{
    qDebug("DaemonManager: exit()");
    m_app_exit = true;
}

QVariantMap DaemonManager::validateDataDir(const QString &dataDir, const int estimatedBlockchainSize) const
{
    QVariantMap result;
    bool valid = true;
    bool readOnly = false;
    int  storageAvailable = 0;
    bool lmdbExists = true;

    QStorageInfo storage(dataDir);
    if (storage.isValid() && storage.isReady()) {
        if (storage.isReadOnly()) {
            readOnly = true;
            valid = false;
        }

        storageAvailable = storage.bytesAvailable()/1000/1000/1000;
        if (storageAvailable < estimatedBlockchainSize) {
            valid = false;
        }
    } else {
        valid = false;
    }


    if (!QDir(dataDir+"/lmdb").exists()) {
        lmdbExists = false;
        valid = false;
    }

    result.insert("valid", valid);
    result.insert("lmdbExists", lmdbExists);
    result.insert("readOnly", readOnly);
    result.insert("storageAvailable", storageAvailable);

    return result;
}

bool DaemonManager::checkLmdbExists(QString datadir) {
    if (datadir.isEmpty() || datadir.isNull()) {
        datadir = QString::fromStdString(tools::get_default_data_dir());
    }
    return QDir(datadir + "/lmdb").exists();
}

QString DaemonManager::getArgs(const QString &dataDir) {
    if (!running(NetworkType::MAINNET, dataDir)) {
        return args;
    }
    QProcess p;
    QStringList tempArgs;
    #ifdef Q_OS_WIN
        //powershell
        tempArgs << "Get-CimInstance Win32_Process -Filter \"name = 'monerod.exe'\" | select -ExpandProperty CommandLine ";
        p.setProgram("powershell");
        p.setArguments(tempArgs);
        p.start();
        p.waitForFinished();
        args = p.readAllStandardOutput().simplified().trimmed();

    #elif defined(Q_OS_UNIX)
        //pgrep
        tempArgs << "monerod";
        p.setProgram("pgrep");
        p.setArguments(tempArgs);
        p.start();
        p.waitForFinished();
        QString pid = p.readAllStandardOutput().trimmed();
        if (pid.isEmpty()) {
            return args;
        }

        tempArgs.clear();

        //ps
        tempArgs << "-o";
        tempArgs << "args=";
        tempArgs << "-fp";
        tempArgs << pid;
        p.setProgram("ps");
        p.setArguments(tempArgs);
        p.start();
        p.waitForFinished();
        args = p.readAllStandardOutput().trimmed();

    #endif
    if (args.contains("--")) {
        int index = args.indexOf("--");
        args.remove(0, index);
    }
    else {
        args = "";
    }
    return args;
}

DaemonManager::DaemonManager(QObject *parent)
    : QObject(parent)
    , m_scheduler(this)
{

    // Platform depetent path to monerod
#ifdef Q_OS_WIN
    m_monerod = QApplication::applicationDirPath() + "/monerod.exe";
#elif defined(Q_OS_UNIX)
    m_monerod = QApplication::applicationDirPath() + "/monerod";
#endif
    if (!sm64paths::program("sm64coind").isEmpty()) m_monerod = sm64paths::program("sm64coind"); // SM64COIN: the bundle's daemon (macOS: next to the .app)

    if (m_monerod.length() == 0) {
        qCritical() << "no daemon binary defined for current platform";
    }
}

DaemonManager::~DaemonManager()
{
    m_scheduler.shutdownWaitForFinished();
}
