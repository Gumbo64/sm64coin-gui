// SM64COIN: helpers exposed to QML for the Mining and Leaderboard pages.
#include "sm64helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/resource.h>
#endif
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QUrl>

namespace sm64paths
{
static const char *ROM_SHA1 = "9bef1128717f958171a4afac3ed78ee2bb4e86ce";
static const char *ROM_NAME = "baserom.us.z64";

QString bundleDir()
{
    QDir d(QCoreApplication::applicationDirPath());
#ifdef Q_OS_MACOS
    // .../SM64Coin-Wallet.app/Contents/MacOS -> the folder holding the app
    if (d.absolutePath().endsWith(".app/Contents/MacOS"))
        d.cd("../../..");
#endif
    return d.absolutePath();
}

QString dataDir()
{
    const QString e = qEnvironmentVariable("SM64COIN_DATA_DIR");
    return e.isEmpty() ? QDir::home().filePath(".sm64coin") : QDir(e).absolutePath();
}

QString program(const QString &name)
{
#ifdef Q_OS_WIN
    const QString file = name + ".exe";
#else
    const QString file = name;
#endif
    for (const QString &dir : {bundleDir(), QCoreApplication::applicationDirPath()}) {
        const QString p = QDir(dir).filePath(file);
        if (QFileInfo(p).isExecutable())
            return QDir::toNativeSeparators(p);
    }
    return QString();
}

static bool isUsRom(const QString &path)
{
    QFile f(path);
    if (f.size() != 8 * 1024 * 1024 || !f.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    return h.result().toHex() == ROM_SHA1;
}

QString findRom()
{
    QStringList files;
    const QString env = qEnvironmentVariable("SM64COIN_ROM");
    if (!env.isEmpty())
        files << env;
    const QStringList dirs = {bundleDir(), dataDir(), QDir::currentPath()};
    for (const QString &d : dirs)
        files << QDir(d).filePath(ROM_NAME);
    for (const QString &d : dirs)
        for (const QFileInfo &fi : QDir(d).entryInfoList({"*.z64", "*.Z64"}, QDir::Files, QDir::Name))
            files << fi.absoluteFilePath();
    for (const QString &f : files)
        if (QFileInfo(f).isFile() && isUsRom(f))
            return QDir::toNativeSeparators(QFileInfo(f).absoluteFilePath());
    return QString();
}
}

QString MinerProcess::defaultSaveDir() const { return QDir::toNativeSeparators(QDir(sm64paths::dataDir()).filePath("mined")); }
QString MinerProcess::defaultScratchDir() const { return QDir::toNativeSeparators(QDir(sm64paths::dataDir()).filePath("miner")); }
QString MinerProcess::defaultRoute() const
{
    const QString p = QDir(sm64paths::bundleDir()).filePath("routes/block1-human.m64");
    return QFileInfo(p).isFile() ? QDir::toNativeSeparators(p) : QString();
}
int MinerProcess::defaultThreads() const { return qMax(1, QThread::idealThreadCount() - 1); }

MinerProcess::MinerProcess(QObject *parent) : QObject(parent)
{
    m_proc.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_proc, &QProcess::readyRead, this, &MinerProcess::onReadyRead);
    connect(&m_proc, &QProcess::stateChanged, this, [this]() { emit runningChanged(); });
    connect(&m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int code, QProcess::ExitStatus) { onReadyRead(); emit finished(code); });
}

MinerProcess::~MinerProcess() { stop(); }

bool MinerProcess::running() const { return m_proc.state() != QProcess::NotRunning; }
qint64 MinerProcess::pid() const { return running() ? m_proc.processId() : 0; }
bool MinerProcess::fileExists(const QString &path) const { return QFileInfo(path).isFile(); }

QString MinerProcess::start(const QString &program, const QStringList &args, int threads)
{
    if (running())
        return QStringLiteral("already running");
    if (!QFileInfo(program).isExecutable())
        return QStringLiteral("not an executable file: ") + program;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("RAYON_NUM_THREADS", QString::number(threads > 0 ? threads : 4));
    m_proc.setProcessEnvironment(env);
    m_buf.clear();
    // Low CPU priority: the miner uses every core but one, and at normal priority it starved the
    // node and the website (pages took long to load while mining). Its search children inherit it.
#ifdef Q_OS_WIN
    m_proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= BELOW_NORMAL_PRIORITY_CLASS; });
#endif
    m_proc.start(program, args);
    if (!m_proc.waitForStarted(3000))
        return QStringLiteral("failed to start: ") + m_proc.errorString();
#ifndef Q_OS_WIN
    setpriority(PRIO_PROCESS, static_cast<id_t>(m_proc.processId()), 10);
#endif
    return QString();
}

void MinerProcess::stop()
{
    if (!running())
        return;
    m_proc.terminate();
    if (!m_proc.waitForFinished(3000)) {
        m_proc.kill();
        m_proc.waitForFinished(2000);
    }
}

void MinerProcess::onReadyRead()
{
    m_buf += m_proc.readAll();
    int i;
    while ((i = m_buf.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_buf.left(i)).trimmed();
        m_buf.remove(0, i + 1);
        if (!line.isEmpty())
            emit output(line);
    }
}

Sm64Http::Sm64Http(QObject *parent) : QObject(parent) {}

void Sm64Http::openSite(const QString &custom, const QString &path)
{
    static const QString pages = QStringLiteral("https://gumbo64.github.io/sm64coin");
    auto base = [](QString u) { u = u.trimmed(); while (u.endsWith('/')) u.chop(1); if (u.endsWith("/mine")) u.chop(5); return u; };
    auto open = [this](const QString &u) { QDesktopServices::openUrl(QUrl(u)); emit siteOpened(u); };
    // Old defaults (before 0.2.2) count as "automatic".
    const QString c = base(custom);
    if (!c.isEmpty() && c != pages && c != QLatin1String("http://localhost:5181"))
        return open(c + path);
    QString local = base(qEnvironmentVariable("SM64COIN_WEB_URL"));
    if (local.isEmpty())
        local = QStringLiteral("http://localhost:5181");
    QNetworkReply *reply = m_nam.get(QNetworkRequest(QUrl(local + "/")));
    QTimer::singleShot(2500, reply, [reply]() { reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [reply, local, path, open]() {
        const bool up = reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        reply->deleteLater();
        open((up ? local : pages) + path);
    });
}

void Sm64Http::request(int id, const QString &method, const QString &url, const QString &jsonBody)
{
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply *reply = method.toUpper() == "POST" ? m_nam.post(req, jsonBody.toUtf8()) : m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString text = QString::fromUtf8(reply->readAll());
        if (status == 0)
            text = reply->errorString();
        reply->deleteLater();
        emit finished(id, status, text);
    });
}
