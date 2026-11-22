// SM64COIN: helpers exposed to QML for the Mining and Leaderboard pages.
#ifndef SM64HELPERS_H
#define SM64HELPERS_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QNetworkAccessManager>

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
signals:
    void finished(int id, int status, const QString &text);
private:
    QNetworkAccessManager m_nam;
};

#endif
