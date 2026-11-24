// SM64COIN: helpers exposed to QML for the Mining and Leaderboard pages.
#ifndef SM64HELPERS_H
#define SM64HELPERS_H

#include <QDir>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QNetworkAccessManager>

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

// Minimal asynchronous HTTP client (the QML network access manager is deliberately blocked).
class Sm64Http : public QObject
{
    Q_OBJECT
public:
    explicit Sm64Http(QObject *parent = nullptr);
    Q_INVOKABLE void request(int id, const QString &method, const QString &url, const QString &jsonBody);
    // Opens a page of the website (`path`, e.g. "/mine?address=..."): `custom` (a site URL from the
    // settings) if set; else the local site (the launcher's, $SM64COIN_WEB_URL, default
    // http://localhost:5181) when it answers, which uses this computer's node; GitHub Pages only as
    // the last resort. Calls back siteOpened(url).
    Q_INVOKABLE void openSite(const QString &custom, const QString &path);
signals:
    void siteOpened(const QString &url);
    void finished(int id, int status, const QString &text);
private:
    QNetworkAccessManager m_nam;
};

#endif
