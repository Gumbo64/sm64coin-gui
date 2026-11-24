// SM64COIN: helpers exposed to QML for the Mining and Leaderboard pages.
#include "sm64helpers.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QTimer>
#include <QRegularExpression>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QHostInfo>
#include <QHostAddress>
#include <QTextStream>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <sys/resource.h>
#endif
#include <QTcpSocket>
#include <QNetworkReply>
#include <QPointer>
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QUrl>

#if defined(Q_OS_LINUX)
#include <signal.h>
#include <sys/prctl.h>
#endif

namespace {
// A process the GUI starts (the leaderboard server): on Linux the kernel stops it when the GUI
// dies, even if the GUI is killed or crashes (then the destructor that terminates it never runs).
class GuiChildProcess : public QProcess
{
public:
    using QProcess::QProcess;

protected:
#if defined(Q_OS_LINUX) && QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    void setupChildProcess() override { ::prctl(PR_SET_PDEATHSIG, SIGTERM); }
#endif
};
}

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

QStringList seedPeers()
{
    QStringList entries;
    bool found = false;
    for (const QString &dir : {bundleDir(), dataDir()}) {
        QFile f(QDir(dir).filePath("seeds.txt"));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        found = true;
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine().section('#', 0, 0).simplified().remove(' ');
            if (!line.isEmpty())
                entries << line;
        }
    }
    if (!found)
        entries << "p2p.eboy.au:16480"; // as seeds.txt in the release
    QStringList out;
    for (const QString &e : entries) {
        const int colon = e.lastIndexOf(':');
        const QString host = colon > 0 ? e.left(colon) : e;
        const QString port = colon > 0 ? e.mid(colon + 1) : QStringLiteral("16480");
        QHostAddress ip(host);
        if (ip.isNull()) {
            // The daemon refuses to start on a peer it can't resolve: resolve here, skip failures.
            const QHostInfo info = QHostInfo::fromName(host);
            for (const QHostAddress &a : info.addresses())
                if (a.protocol() == QAbstractSocket::IPv4Protocol) { ip = a; break; }
        }
        if (!ip.isNull() && !out.contains(ip.toString() + ":" + port))
            out << ip.toString() + ":" + port;
    }
    return out;
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

QString nodeDir() { return QDir(dataDir()).filePath("node"); }

QString installRom(const QString &path)
{
    const QFileInfo fi(path);
    if (path.isEmpty() || !fi.isFile())
        return QCoreApplication::translate("sm64", "The file does not exist.");
    if (fi.size() != 8 * 1024 * 1024)
        return QCoreApplication::translate("sm64", "This is not the right file: a US Super Mario 64 ROM in .z64 format is exactly 8 MiB (this file is %1 bytes). Byte-swapped dumps (.v64, .n64) do not work, convert them to .z64 first.").arg(fi.size());
    if (!isUsRom(path))
        return QCoreApplication::translate("sm64", "This is not the right file: its SHA-1 is not %1 (the US version of Super Mario 64, .z64). Other regions and modified ROMs do not work.").arg(ROM_SHA1);
    QDir().mkpath(dataDir());
    const QString dest = QDir(dataDir()).filePath(ROM_NAME);
    if (QFileInfo(dest).absoluteFilePath() == fi.absoluteFilePath())
        return QString();
    QFile::remove(dest);
    if (!QFile::copy(path, dest))
        return QCoreApplication::translate("sm64", "Could not copy the ROM to %1.").arg(QDir::toNativeSeparators(dest));
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
    connect(&m_proc, &QProcess::stateChanged, this, [this]() {
        if (!running() && (m_fps != 0 || !m_samples.isEmpty())) { m_fps = 0; m_samples.clear(); emit speedChanged(); }
        emit runningChanged();
    });
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
    m_samples.clear();
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

QString MinerProcess::formatRate(double x)
{
    if (x >= 1e6) return QString::number(x / 1e6, 'f', x >= 1e7 ? 1 : 2) + QStringLiteral(" M");
    if (x >= 1e3) return QString::number(x / 1e3, 'f', x >= 1e5 ? 0 : 1) + QStringLiteral(" k");
    return QString::number(qRound(x));
}

QString MinerProcess::speedText() const
{
    return m_fps > 0 ? formatRate(m_fps) + QStringLiteral(" frames/s") : QString();
}

// A `progress frames=<total> fps=<x>` line: the speed is the frames gained over the last 8 s (a moving average).
void MinerProcess::onProgress(double frames)
{
    if (!m_clock.isValid()) m_clock.start();
    const qint64 t = m_clock.elapsed();
    m_samples.append({t, frames});
    while (m_samples.size() > 2 && m_samples[1].first < t - 8000) m_samples.removeFirst();
    const double dt = (m_samples.last().first - m_samples.first().first) / 1000.0;
    const double fps = dt > 0.5 ? (m_samples.last().second - m_samples.first().second) / dt : 0;
    if (fps != m_fps) { m_fps = fps; emit speedChanged(); }
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
        static const QRegularExpression progress(QStringLiteral("^progress frames=(\\d+)"));
        const QRegularExpressionMatch pm = progress.match(line);
        if (pm.hasMatch())
            onProgress(pm.captured(1).toDouble());
        else if (!line.isEmpty())
            emit output(line);
    }
}

static QString contentType(const QString &f)
{
    static const QHash<QString, const char *> t = {
        {"html", "text/html; charset=utf-8"}, {"js", "text/javascript; charset=utf-8"}, {"mjs", "text/javascript; charset=utf-8"},
        {"css", "text/css; charset=utf-8"}, {"json", "application/json"}, {"wasm", "application/wasm"},
        {"svg", "image/svg+xml"}, {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"gif", "image/gif"},
        {"ico", "image/x-icon"}, {"webp", "image/webp"}, {"woff", "font/woff"}, {"woff2", "font/woff2"}, {"ttf", "font/ttf"},
        {"txt", "text/plain; charset=utf-8"}, {"map", "application/json"}, {"webmanifest", "application/manifest+json"}};
    return QString::fromLatin1(t.value(QFileInfo(f).suffix().toLower(), "application/octet-stream"));
}

Sm64WebServer::Sm64WebServer(QObject *parent) : QTcpServer(parent) {}

Sm64WebServer::~Sm64WebServer()
{
    if (m_lb && m_lb->state() != QProcess::NotRunning) {
        m_lb->terminate();
        if (!m_lb->waitForFinished(2000)) { m_lb->kill(); m_lb->waitForFinished(1000); }
    }
}

QString Sm64WebServer::findWebDir()
{
    QStringList c;
    const QString env = qEnvironmentVariable("SM64COIN_WEB_DIR");
    if (!env.isEmpty()) c << env;
    const QString b = sm64paths::bundleDir();
    c << b + "/web" << QCoreApplication::applicationDirPath() + "/web"
      << b + "/../web/dist" << b + "/../../../web/dist" << b + "/../../../../web/dist"; // a source checkout: <repo>/gui/build/bin
    for (const QString &d : c)
        if (QFileInfo(QDir(d).filePath("index.html")).isFile())
            return QDir::cleanPath(QDir(d).absolutePath());
    return QString();
}

QString Sm64WebServer::startAll()
{
    m_dir = findWebDir();
    if (m_dir.isEmpty())
        return m_error = tr("the website is missing: no web/index.html next to the wallet (%1)").arg(QDir::toNativeSeparators(sm64paths::bundleDir()));
    bool okPort = false;
    int port = qEnvironmentVariable("SM64COIN_WEB_PORT").toInt(&okPort);
    if (!okPort || port <= 0 || port > 65535) port = 5181;
    bool bound = false;
    for (int i = 0; i <= 20 && !bound; ++i)
        bound = listen(QHostAddress::LocalHost, quint16(port + i < 65536 ? port + i : 0));
    if (!bound)
        bound = listen(QHostAddress::LocalHost, 0);
    if (!bound)
        return m_error = tr("cannot start the local site: %1").arg(errorString());

    // The leaderboard server of the bundle (the site's /leaderboard-api and the Leaderboard page):
    // unless one already answers on its port (the sm64coin-node script's, a server's).
    m_leaderboard = QStringLiteral("http://127.0.0.1:16490");
    const QString lb = sm64paths::program("sm64coin-leaderboard");
    if (!lb.isEmpty()) {
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, 16490);
        if (!probe.waitForConnected(300)) {
            const QString dd = QDir(sm64paths::dataDir()).filePath("leaderboard");
            QDir().mkpath(dd);
            m_lb = new GuiChildProcess(this);
            m_lb->setProcessChannelMode(QProcess::MergedChannels);
            m_lb->setStandardOutputFile(QDir(sm64paths::dataDir()).filePath("leaderboard.log"), QIODevice::Append);
            QStringList a{"--daemon", m_rpc, "--bind", "127.0.0.1:16490", "--poll-secs", "10", "--data-dir", dd, "--peer-port", "16490"};
            for (const QString &f : {QDir(sm64paths::bundleDir()).filePath("leaderboard-peers.txt"), QDir(sm64paths::dataDir()).filePath("leaderboard-peers.txt")})
                if (QFileInfo(f).isFile()) a << "--peers-file" << f;
            m_lb->start(lb, a);
        }
    }
    return QString();
}

void Sm64WebServer::setRpc(const QString &addr)
{
    QString a = addr.trimmed();
    if (a.isEmpty()) return;
    if (!a.contains("://")) a = "http://" + a;
    while (a.endsWith('/')) a.chop(1);
    m_rpc = a;
}

void Sm64WebServer::incomingConnection(qintptr handle)
{
    auto *s = new QTcpSocket(this);
    if (!s->setSocketDescriptor(handle)) { delete s; return; }
    connect(s, &QTcpSocket::readyRead, this, [this, s]() { onData(s); });
    connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    QTimer::singleShot(150000, s, [s]() { s->abort(); }); // a stuck client never holds the socket forever
}

void Sm64WebServer::onData(QTcpSocket *s)
{
    // One request per connection (Connection: close). Wait for the whole header and body.
    QByteArray buf = s->property("buf").toByteArray() + s->readAll();
    s->setProperty("buf", buf);
    const int he = buf.indexOf("\r\n\r\n");
    if (he < 0) {
        if (buf.size() > 65536) respond(s, 431, "text/plain", "headers too large");
        return;
    }
    const QList<QByteArray> lines = buf.left(he).split('\n');
    const QList<QByteArray> rl = lines.value(0).trimmed().split(' ');
    if (rl.size() < 2) return respond(s, 400, "text/plain", "bad request");
    qint64 len = 0;
    QByteArray ctype;
    for (int i = 1; i < lines.size(); ++i) {
        const int k = lines[i].indexOf(':');
        if (k < 0) continue;
        const QByteArray name = lines[i].left(k).trimmed().toLower(), val = lines[i].mid(k + 1).trimmed();
        if (name == "content-length") len = val.toLongLong();
        else if (name == "content-type") ctype = val;
    }
    if (len < 0 || len > 16 * 1024 * 1024) return respond(s, 413, "text/plain", "too large");
    if (buf.size() - (he + 4) < len) return;
    s->disconnect(SIGNAL(readyRead()));
    s->setProperty("buf", QVariant());
    serve(s, rl[0].toUpper(), rl[1], ctype, buf.mid(he + 4, int(len)));
}

void Sm64WebServer::respond(QTcpSocket *s, int code, const QByteArray &type, const QByteArray &body, bool head)
{
    if (!s || s->state() != QAbstractSocket::ConnectedState) return;
    static const QHash<int, const char *> txt = {{200, "OK"}, {204, "No Content"}, {400, "Bad Request"}, {404, "Not Found"}, {405, "Method Not Allowed"},
                                                 {413, "Payload Too Large"}, {431, "Request Header Fields Too Large"}, {502, "Bad Gateway"}, {503, "Service Unavailable"}};
    QByteArray h = "HTTP/1.1 " + QByteArray::number(code) + " " + txt.value(code, "Status") + "\r\nContent-Type: " + type
                 + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
    s->write(h);
    if (!head) s->write(body);
    s->disconnectFromHost();
}

void Sm64WebServer::proxy(QTcpSocket *s, const QString &base, const QString &rest, const QByteArray &ctype, const QByteArray &body, bool isPost)
{
    QNetworkRequest req{QUrl(base + rest)};
    if (!ctype.isEmpty()) req.setHeader(QNetworkRequest::ContentTypeHeader, ctype);
    QNetworkReply *reply = isPost ? m_nam.post(req, body) : m_nam.get(req);
    QPointer<QTcpSocket> sock(s);
    QTimer::singleShot(120000, reply, [reply]() { reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, sock]() {
        const QByteArray data = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray ct = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
        const QString err = reply->errorString();
        reply->deleteLater();
        if (!sock) return;
        if (status == 0) respond(sock, 502, "text/plain", "upstream unavailable: " + err.toUtf8());
        else respond(sock, status, ct.isEmpty() ? QByteArray("application/json") : ct, data);
    });
}

void Sm64WebServer::serve(QTcpSocket *s, const QByteArray &method, const QByteArray &target, const QByteArray &ctype, const QByteArray &body)
{
    if (method == "OPTIONS") return respond(s, 204, "text/plain", QByteArray());
    const bool head = method == "HEAD";
    if (method != "GET" && !head && method != "POST") return respond(s, 405, "text/plain", "method not allowed");
    const QString url = QString::fromUtf8(QByteArray::fromPercentEncoding(target));
    const int q = target.indexOf('?');
    const QString path = QString::fromUtf8(QByteArray::fromPercentEncoding(q < 0 ? target : target.left(q)));
    const QString query = q < 0 ? QString() : QString::fromUtf8(target.mid(q));
    Q_UNUSED(url);
    if (path == "/json_rpc")
        return proxy(s, m_rpc, "/json_rpc", ctype, body, method == "POST");
    if (path.startsWith("/leaderboard-api")) {
        QString rest = path.mid(16);
        return proxy(s, m_leaderboard, (rest.isEmpty() ? QString("/") : rest) + query, ctype, body, method == "POST");
    }
    // Static files of the site; never outside its folder.
    const QStringList parts = path.split('/', QString::SkipEmptyParts);
    for (const QString &p : parts)
        if (p == ".." || p.contains(':') || p.contains('\\')) return respond(s, 400, "text/plain", "bad path");
    QString rel = parts.join('/');
    QFileInfo fi(QDir(m_dir).filePath(rel.isEmpty() ? "index.html" : rel));
    if (fi.isDir()) fi.setFile(QDir(fi.absoluteFilePath()).filePath("index.html"));
    // Single-page app: unknown paths without an extension are routes of the site.
    if (!fi.isFile() && QFileInfo(rel).suffix().isEmpty()) fi.setFile(QDir(m_dir).filePath("index.html"));
    QFile f(fi.absoluteFilePath());
    if (!fi.isFile() || !fi.absoluteFilePath().startsWith(m_dir) || !f.open(QIODevice::ReadOnly))
        return respond(s, 404, "text/plain", "not found", head);
    respond(s, 200, contentType(fi.fileName()).toLatin1(), f.readAll(), head);
}

Sm64Http::Sm64Http(QObject *parent) : QObject(parent) {}

void Sm64Http::openSite(const QString &custom, const QString &path)
{
    static const QString pages = QStringLiteral("https://gumbo64.github.io/sm64coin");
    auto base = [](QString u) { u = u.trimmed(); while (u.endsWith('/')) u.chop(1); if (u.endsWith("/mine")) u.chop(5); return u; };
    auto open = [this](const QString &u) { QDesktopServices::openUrl(QUrl(u)); emit siteOpened(u); };
    // Old defaults (before 0.2.2, and the launcher's port) count as "automatic".
    const QString c = base(custom);
    if (!c.isEmpty() && c != pages && c != QLatin1String("http://localhost:5181") && c != QLatin1String("http://127.0.0.1:5181"))
        return open(c + path);
    const QString local = m_server ? m_server->baseUrl() : QString();
    if (local.isEmpty()) {
        emit siteError(m_server && !m_server->error().isEmpty() ? m_server->error() : tr("the local site is not running"));
        return open(pages + path);
    }
    // Health check at the moment of use: the site's index.html must come back.
    QNetworkReply *reply = m_nam.get(QNetworkRequest(QUrl(local + "/")));
    QTimer::singleShot(2500, reply, [reply]() { reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, local, path, open]() {
        const bool up = reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        const QString why = reply->errorString();
        reply->deleteLater();
        if (!up)
            emit siteError(tr("the local site on %1 does not answer (%2)").arg(local, why));
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
