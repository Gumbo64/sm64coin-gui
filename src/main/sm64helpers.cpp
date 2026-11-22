// SM64COIN: helpers exposed to QML for the Mining and Leaderboard pages.
#include "sm64helpers.h"

#include <QFileInfo>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QUrl>

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
    return startImpl(program, args, threads, QStringList());
}

QString MinerProcess::startWithEnv(const QString &program, const QStringList &args, int threads, const QStringList &extraEnv)
{
    return startImpl(program, args, threads, extraEnv);
}

QString MinerProcess::startImpl(const QString &program, const QStringList &args, int threads, const QStringList &extraEnv)
{
    if (running())
        return QStringLiteral("already running");
    if (!QFileInfo(program).isExecutable())
        return QStringLiteral("not an executable file: ") + program;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("RAYON_NUM_THREADS", QString::number(threads > 0 ? threads : 4));
    for (const QString &kv : extraEnv) {
        int eq = kv.indexOf('=');
        if (eq > 0)
            env.insert(kv.left(eq), kv.mid(eq + 1));
    }
    m_proc.setProcessEnvironment(env);
    m_buf.clear();
    m_proc.start(program, args);
    if (!m_proc.waitForStarted(3000))
        return QStringLiteral("failed to start: ") + m_proc.errorString();
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
