// SM64COIN: helpers exposed to QML for the Mining and Leaderboard pages.
#ifndef SM64HELPERS_H
#define SM64HELPERS_H

#include <QDir>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QTcpServer>
#include <QUrl>

// Where the release bundle's files are. Nothing here is hardcoded to a machine: the launcher
// (sm64coin) passes SM64COIN_ROM and SM64COIN_DATA_DIR to the wallet it starts; without them the
// same places the launcher searches are used.
namespace sm64paths
{
    // The bundle folder: the wallet's own folder (macOS: the folder holding SM64Coin-Wallet.app).
    QString bundleDir();
    // $SM64COIN_DATA_DIR, else ~/.sm64coin (the launcher's --data-dir default).
    QString dataDir();
    // A bundled program (name without .exe), in the bundle folder or next to the wallet; empty if none.
    QString program(const QString &name);
    // The US ROM (SHA-1 checked): $SM64COIN_ROM, then baserom.us.z64 / any .z64 in the bundle
    // folder, the data dir and the current folder. Empty if none.
    QString findRom();
    // Checks `path` (size, SHA-1) and copies it to <data dir>/baserom.us.z64. Empty string on
    // success, else a message for the user.
    QString installRom(const QString &path);
    // The wallet's own node's data: <data dir>/node.
    QString nodeDir();
    // The network's seed nodes as "ip:port" (hostnames resolved, unresolvable ones skipped):
    // seeds.txt next to the bundle and in the data dir, as the launcher reads them; if there is
    // none, the seeds the release ships with. For nodes the wallet starts itself.
    QStringList seedPeers();
}

// Runs the automated miner (sm64coin-automine) as a child process.
class MinerProcess : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(qint64 pid READ pid NOTIFY runningChanged)
public:
    explicit MinerProcess(QObject *parent = nullptr);
    ~MinerProcess() override;
    bool running() const;
    qint64 pid() const;
    Q_INVOKABLE bool fileExists(const QString &path) const;
    // Defaults of the Mining page (used when its fields are left empty).
    Q_INVOKABLE QString defaultMinerPath() const { return sm64paths::program("sm64coin-automine"); }
    Q_INVOKABLE QString defaultRomPath() const { return sm64paths::findRom(); }
    Q_INVOKABLE QString installRom(const QString &path) const { return sm64paths::installRom(path); }
    Q_INVOKABLE QString defaultNodeDir() const { return QDir::toNativeSeparators(sm64paths::nodeDir()); }
    Q_INVOKABLE QString defaultSaveDir() const;     // <data dir>/mined
    Q_INVOKABLE QString defaultScratchDir() const;  // <data dir>/miner (shared with the launcher's --mine)
    Q_INVOKABLE QString defaultRoute() const;       // <bundle>/routes/block1-human.m64, empty if missing
    Q_INVOKABLE int defaultThreads() const;         // all cores but one
    // Returns an empty string on success, else an error message.
    Q_INVOKABLE QString start(const QString &program, const QStringList &args, int threads);
    Q_INVOKABLE void stop();
signals:
    void runningChanged();
    void output(const QString &line);
    void finished(int exitCode);
private slots:
    void onReadyRead();
private:
    QProcess m_proc;
    QByteArray m_buf;
};

// The website the release ships next to the wallet (web/), served by the wallet itself on
// 127.0.0.1 so nothing else has to be started: static files (single-page app), plus the same-origin
// endpoints the site uses: /json_rpc -> this computer's node, /leaderboard-api -> the leaderboard server.
class Sm64WebServer : public QTcpServer
{
    Q_OBJECT
public:
    explicit Sm64WebServer(QObject *parent = nullptr);
    ~Sm64WebServer() override;
    // Finds the website folder, listens (port $SM64COIN_WEB_PORT or 5181, else the next free ones),
    // starts the bundled leaderboard server if there is one. Returns an empty string on success,
    // else why the local site is not available.
    QString startAll();
    QString error() const { return m_error; }
    QString baseUrl() const { return isListening() ? QStringLiteral("http://127.0.0.1:%1").arg(serverPort()) : QString(); }
    QString webDir() const { return m_dir; }
    // Node the site's /json_rpc goes to ("host:port" or URL); follows the wallet's node setting.
    void setRpc(const QString &addr);
    static QString findWebDir();
protected:
    void incomingConnection(qintptr handle) override;
private:
    void onData(QTcpSocket *s);
    void respond(QTcpSocket *s, int code, const QByteArray &type, const QByteArray &body, bool head = false);
    void serve(QTcpSocket *s, const QByteArray &method, const QByteArray &target, const QByteArray &ctype, const QByteArray &body);
    void proxy(QTcpSocket *s, const QString &base, const QString &rest, const QByteArray &ctype, const QByteArray &body, bool isPost);
    QString m_dir, m_error;
    QString m_rpc = QStringLiteral("http://127.0.0.1:16481");
    QString m_leaderboard;
    QProcess *m_lb = nullptr;
    QNetworkAccessManager m_nam;
};

// Minimal asynchronous HTTP client (the QML network access manager is deliberately blocked).
class Sm64Http : public QObject
{
    Q_OBJECT
public:
    explicit Sm64Http(QObject *parent = nullptr);
    void setServer(Sm64WebServer *s) { m_server = s; }
    // The wallet's current node, so the site's /json_rpc reaches the same one.
    Q_INVOKABLE void setNode(const QString &addr) { if (m_server) m_server->setRpc(addr); }
    // "Update available" check: GitHub's latest release (SM64COIN_UPDATE_URL overrides it, for tests)
    // and SM64COIN_VERSION_OVERRIDE (the version to pretend to be, for tests).
    Q_INVOKABLE QString updateUrl() const { const QString e = qEnvironmentVariable("SM64COIN_UPDATE_URL"); return e.isEmpty() ? QStringLiteral("https://api.github.com/repos/Gumbo64/sm64coin/releases/latest") : e; }
    Q_INVOKABLE QString versionOverride() const { return qEnvironmentVariable("SM64COIN_VERSION_OVERRIDE"); }
    Q_INVOKABLE QString localUrl() const { return m_server ? m_server->baseUrl() : QString(); }
    Q_INVOKABLE void request(int id, const QString &method, const QString &url, const QString &jsonBody);
    // Opens a page of the website (`path`, e.g. "/mine?address=..."): `custom` (a site URL from the
    // settings) if set; else the local site (the wallet's own server (Sm64WebServer),
    // which uses this computer's node) after a health check; GitHub Pages only as the last resort,
    // with siteError(reason). Calls back siteOpened(url).
    Q_INVOKABLE void openSite(const QString &custom, const QString &path);
signals:
    void siteOpened(const QString &url);
    // The local site is not available (reason); the GitHub Pages site is opened instead.
    void siteError(const QString &message);
    void finished(int id, int status, const QString &text);
private:
    QNetworkAccessManager m_nam;
    Sm64WebServer *m_server = nullptr;
};

#endif
