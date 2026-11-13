#include "instance.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "shell.h"

// One per profile: a throwaway data dir (shots, engine checks) never meets the real browser's socket.
QString Instance::socketPath(const QString &dataDir, const QString &kind) {
    const QByteArray profile = QCryptographicHash::hash(dataDir.toUtf8(), QCryptographicHash::Sha1).toHex().left(12);
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/browser-") + kind
           + QString::fromLatin1(profile) + QStringLiteral(".sock");
}

Instance::Instance(const QString &dataDir, QObject *parent)
    : QObject(parent), m_path(socketPath(dataDir)), m_lock(std::make_unique<QLockFile>(m_path + QStringLiteral(".lock"))) {
    // held for the browser's life: only a lock whose process is gone (a crash, a kill) is stale, never an old one
    m_lock->setStaleLockTime(0);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server.nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] { read(socket); });
        }
    });
}

// The pid in the lock of a browser that has not answered, if that process runs this very executable (Chromium's
// IsChromeProcess check, process_singleton_posix.cc), else 0: a lock left by something else is never a reason to end it,
// nor to ask about it. /proc/<pid>/exe of a binary replaced by an update reads "<path> (deleted)".
static qint64 holderOf(QLockFile &lock) {
    qint64 pid = 0;
    QString host, app;
    if (!lock.getLockInfo(&pid, &host, &app) || pid <= 1 || pid == QCoreApplication::applicationPid())
        return 0;
    QString exe = QFile::symLinkTarget(QStringLiteral("/proc/%1/exe").arg(pid));
    if (exe.endsWith(u" (deleted)"))
        exe.chop(10);
    return !exe.isEmpty() && exe == QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath() ? pid : 0;
}

// Ends the browser `pid`: SIGTERM, 3 s to go, then SIGKILL.
static bool endHolder(qint64 pid) {
    const QString link = QStringLiteral("/proc/%1/exe").arg(pid);
    // a zombie keeps its pid but has no exe
    const auto gone = [&link] { return QFile::symLinkTarget(link).isEmpty(); };
    ::kill(pid_t(pid), SIGTERM);
    for (QDeadlineTimer deadline(3000); !gone() && !deadline.hasExpired();)
        QThread::msleep(50);
    if (!gone()) {
        ::kill(pid_t(pid), SIGKILL);
        for (QDeadlineTimer deadline(1000); !gone() && !deadline.hasExpired();)
            QThread::msleep(50);
    }
    return gone();
}

// The notch's "Thyris isn't answering" with Wait and Close it: the shell's prompt for an app that stopped answering
// (IslandController.notAnswering), asked as its scripts/activity/hyprland-dialog asks it. The answer comes back on a
// fifo as the button's word, "close" or "wait"; the prompt is this process's (its pid), so it goes when this one does, and
// it names the browser's process, whose Details Activity shows.
namespace {
class Prompt {
public:
    ~Prompt() { drop(); }
    bool shown() const { return m_shown; }
    // "shown"; "no" when the shell shows no prompt now (its notch off, or held back); "" with no shell to ask
    QString ask(qint64 pid) {
        if (m_fd < 0) {
            m_fifo = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/thyris-anr-") + id();
            QFile::remove(m_fifo);
            if (::mkfifo(QFile::encodeName(m_fifo).constData(), 0600) != 0)
                return {};
            // not waiting for a writer: the shell opens the other end when it answers
            m_fd = ::open(QFile::encodeName(m_fifo).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (m_fd < 0) {
                QFile::remove(m_fifo);
                return {};
            }
        }
        const QJsonObject request{{QStringLiteral("id"), id()}, {QStringLiteral("cls"), QStringLiteral("thyris")},
                                  {QStringLiteral("title"), QString()}, {QStringLiteral("pid"), pid}, {QStringLiteral("fifo"), m_fifo},
                                  {QStringLiteral("buttons"), QJsonArray{QStringLiteral("close"), QStringLiteral("wait")}}};
        QProcess qs;
        qs.start(QStringLiteral("qs"), shellIpc() + QStringList{QStringLiteral("island"), QStringLiteral("notAnswering"),
                                                                QString::fromUtf8(QJsonDocument(request).toJson(QJsonDocument::Compact))});
        const bool ran = qs.waitForFinished(5000) && qs.exitStatus() == QProcess::NormalExit && qs.exitCode() == 0;
        m_shown = ran && qs.readAllStandardOutput().trimmed() == "shown";
        return m_shown ? QStringLiteral("shown") : ran ? QStringLiteral("no") : QString();
    }
    // the button's word once the prompt is answered (the shell has let it go then), else ""
    QString answer() {
        char buffer[64];
        for (ssize_t n; m_fd >= 0 && (n = ::read(m_fd, buffer, sizeof buffer)) > 0;)
            m_got.append(buffer, n);
        const qsizetype end = m_got.indexOf('\n');
        if (end < 0)
            return {};
        const QString word = QString::fromUtf8(m_got.left(end)).trimmed();
        m_got.clear();
        m_shown = false;
        return word;
    }
    // the prompt leaves the notch unanswered (the browser answered, or went), and the fifo goes
    void drop() {
        if (m_shown)
            QProcess::startDetached(QStringLiteral("qs"), shellIpc() + QStringList{QStringLiteral("island"), QStringLiteral("notAnsweringGone"), id()});
        m_shown = false;
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
            QFile::remove(m_fifo);
        }
    }

private:
    static QString id() { return QString::number(QCoreApplication::applicationPid()); }
    QString m_fifo;
    int m_fd = -1;
    QByteArray m_got;
    bool m_shown = false;
};
}

int Instance::start(const QUrl &url, const QString &from, const QString &context) {
    QJsonObject message{{QStringLiteral("url"), url.toString(QUrl::FullyEncoded)}, {QStringLiteral("from"), from},
                        {QStringLiteral("context"), context}};
    // the launcher's Wayland activation token, so the compositor lets the running browser take the focus
    const QString token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    if (!token.isEmpty())
        message.insert(QStringLiteral("token"), token);
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    // One still starting listens moments after taking the lock; one quitting keeps it, listening no more, until its
    // Chromium has shut down, and then this one is the browser. The link waits in one connection until it is answered.
    QLocalSocket socket;
    Prompt prompt;
    // the browser that is ended (Close it, or Chromium's way with no shell to ask) leaves this one the profile
    const auto takeOver = [this](qint64 pid) {
        if (!endHolder(pid))
            return int(NoAnswer);
        if (!m_lock->tryLock(0)) {
            m_lock->removeStaleLockFile();
            m_lock->tryLock(0);
        }
        QLocalServer::removeServer(m_path);
        return int(m_server.listen(m_path) ? Listening : Failed);
    };
    for (QDeadlineTimer deadline(20000);;) {
        // No browser on this profile (a lock that cannot be written at all counts so too): this one is it. The socket
        // file of one that did not quit cleanly goes first.
        if (m_lock->isLocked() || m_lock->tryLock(0) || m_lock->error() != QLockFile::LockFailedError) {
            QLocalServer::removeServer(m_path);
            return m_server.listen(m_path) ? Listening : Failed;
        }
        const HandOver result = handOver(socket, line);
        if (result == Taken)
            return HandedOff;
        if (prompt.shown()) {
            const QString answer = prompt.answer();
            // Close it: a holder gone meanwhile leaves the lock to take above
            if (const qint64 pid = answer == u"close" ? holderOf(*m_lock) : 0)
                return takeOver(pid);
            // Wait (Esc too): asked again if it still has not answered in 20 s
            if (answer == u"wait")
                deadline.setRemainingTime(20000);
        } else if (deadline.hasExpired()) {
            // The browser holds the profile and has not taken the link in 20 s, as long as Chromium waits (its
            // kTimeoutInSeconds, process_singleton_posix.cc): the notch asks whether to wait or close it (the owner,
            // 2026-11-13), where it used to be ended after 4 s, and with it a form being filled or an upload. Only with no
            // shell to ask is it ended unasked, as Chromium's second process ends it; this one opens the link then.
            const qint64 pid = holderOf(*m_lock);
            if (pid == 0)
                return result == Held ? Queued : NoAnswer;
            const QString said = prompt.ask(pid);
            if (said.isEmpty())
                return takeOver(pid);
            if (said != u"shown")
                deadline.setRemainingTime(20000);
        }
        if (result == Refused)
            QThread::msleep(100);
    }
}

bool Instance::running() {
    if (m_lock->tryLock(0)) {
        m_lock->unlock();
        return false;
    }
    return m_lock->error() == QLockFile::LockFailedError;
}

// Taken once the running browser answers "ok"; Held while it keeps the connection open with no answer yet (the line
// waits in its socket); Refused when nothing listens or the connection closes unanswered, and the line goes again in a
// new one. The line is written once a connection, and the answer waited for 100 ms at a time.
Instance::HandOver Instance::handOver(QLocalSocket &socket, const QByteArray &line) const {
    if (socket.state() != QLocalSocket::ConnectedState) {
        socket.abort();
        socket.connectToServer(m_path);
        if (!socket.waitForConnected(1000))
            return Refused;
        socket.write(line);
        socket.waitForBytesWritten(1000);
    }
    if (!socket.canReadLine())
        socket.waitForReadyRead(100);
    if (socket.canReadLine())
        return socket.readLine() == "ok\n" ? Taken : Refused;
    return socket.state() == QLocalSocket::ConnectedState ? Held : Refused;
}

void Instance::close() {
    m_server.close();
    for (QLocalSocket *socket : m_server.findChildren<QLocalSocket *>(Qt::FindDirectChildrenOnly))
        if (!socket->property("answered").toBool())
            socket->abort();
}

void Instance::setHolding(bool on) {
    m_holding = on;
    if (!on)
        for (const QPointer<QLocalSocket> &socket : std::exchange(m_held, {}))
            if (socket)
                read(socket);
}

void Instance::read(QLocalSocket *socket) {
    if (m_holding) {
        if (!m_held.contains(socket))
            m_held.append(socket);
        return;
    }
    if (!socket->canReadLine()) {
        // a link is one line; this much without one is not a browser handing a link over
        if (socket->bytesAvailable() > 8 << 20)
            socket->abort();
        return;
    }
    const QJsonObject message = QJsonDocument::fromJson(socket->readLine()).object();
    const QString token = message.value(QStringLiteral("token")).toString();
    // Qt's Wayland plugin hands XDG_ACTIVATION_TOKEN to the compositor on the next requestActivate
    if (!token.isEmpty())
        qputenv("XDG_ACTIVATION_TOKEN", token.toUtf8());
    emit opened(QUrl(message.value(QStringLiteral("url")).toString()), message.value(QStringLiteral("from")).toString(),
                message.value(QStringLiteral("context")).toString());
    socket->write("ok\n");
    socket->setProperty("answered", true);
    socket->disconnectFromServer();
}

// The parent of process `pid` from /proc/<pid>/stat (read whole: a /proc file reports atEnd at 0), 0 if unknown.
// The name field can hold spaces and parentheses, so the fields are counted from its last ')'.
static qint64 parentOf(qint64 pid) {
    QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!stat.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray line = stat.readAll();
    return line.mid(line.lastIndexOf(')') + 2).split(' ').value(1).toLongLong();
}

QString Instance::caller(QString *title) {
    QByteArray clients;
    // before the window system (the CEF build's hand-off runs ahead of it, main.cpp) the platform Qt would take
    const QString platform = QGuiApplication::platformName().isEmpty() ? qEnvironmentVariable("QT_QPA_PLATFORM") : QGuiApplication::platformName();
    if (platform == u"offscreen") {
        clients = qgetenv("BROWSER_CHECK_CLIENTS");
    } else {
        const QString signature = qEnvironmentVariable("HYPRLAND_INSTANCE_SIGNATURE");
        if (signature.isEmpty())
            return {};
        QLocalSocket hyprland;
        hyprland.connectToServer(qEnvironmentVariable("XDG_RUNTIME_DIR") + u"/hypr/" + signature + u"/.socket.sock");
        if (!hyprland.waitForConnected(300))
            return {};
        // Hyprland's main loop waits for the request once it has accepted (src/backdrop.cpp): written at once. It
        // closes the connection when it has answered.
        hyprland.write("j/clients");
        hyprland.flush();
        while (hyprland.waitForReadyRead(500))
            clients += hyprland.readAll();
        clients += hyprland.readAll();
    }
    QHash<qint64, QJsonObject> windows;
    for (const QJsonValue &client : QJsonDocument::fromJson(clients).array())
        windows.insert(client[u"pid"].toInteger(), client.toObject());
    if (windows.isEmpty())
        return {};
    qint64 pid = parentOf(QCoreApplication::applicationPid());
    for (int steps = 0; pid > 1 && steps < 64; ++steps, pid = parentOf(pid))
        if (windows.contains(pid)) {
            if (title)
                *title = windows.value(pid).value(u"title").toString();
            return windows.value(pid).value(u"class").toString().section(u'.', -1);
        }
    return {};
}

QString Instance::context(const QString &title) {
    QString dir = QDir::currentPath();
    const QString home = QDir::homePath();
    if (dir == home || dir == u"/")
        dir.clear();
    else if (dir.startsWith(home + u'/'))
        dir = u'~' + dir.mid(home.size());
    return QStringList{dir, title}.filter(QRegularExpression(QStringLiteral("\\S"))).join(u' ');
}
