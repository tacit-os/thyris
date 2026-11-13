#pragma once

#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <qqmlintegration.h>

// Other apps' links (PLAN.md 3.8 "Open in <app>?"): the default app for a scheme, as
// `xdg-mime query default x-scheme-handler/<scheme>` names it, and handing a link to it. Also a downloaded
// file's Open and Show in Files (8d), and the desktop's notification of a download that landed or failed out of sight.
class Apps : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString lastOpened READ lastOpened NOTIFY opened)
    Q_PROPERTY(QString lastRevealed READ lastRevealed NOTIFY revealed)
    // Settings > Start and windows (PLAN.md 3.15): this browser takes web links and pages (xdg-mime); unknown until
    // lookUpDefault() answers
    Q_PROPERTY(QString isDefault READ isDefault NOTIFY defaultFound)
    // --check-engine: the xdg-mime call makeDefault() would have run
    Q_PROPERTY(QString lastDefaulted READ lastDefaulted NOTIFY defaultFound)
    // --check-engine: the notification notify() would have sent, { key, summary, body, actions, resident, transient,
    // count (how many so far) }, and the key withdraw() was last given
    Q_PROPERTY(QVariantMap lastNotice READ lastNotice NOTIFY noticed)
    Q_PROPERTY(QString lastWithdrawn READ lastWithdrawn NOTIFY noticed)
public:
    using QObject::QObject;

    // --check-engine: open(), reveal() and notify() record what they were given and launch or send nothing
    static inline bool dryRun = false;

    // Answers with found(); the name is the desktop file's Name, "" when no app takes the scheme.
    Q_INVOKABLE void lookUp(const QString &scheme);
    Q_INVOKABLE void open(const QString &url);
    // The shell's Files window at the file's folder with the file selected (`files reveal` over Quickshell
    // IPC); without the shell, the folder in the default file manager.
    Q_INVOKABLE void reveal(const QString &path);
    // "yes" when this build's desktop file (apps.cpp, desktopId) is the default for http and https links, "no", or "" until known
    Q_INVOKABLE void lookUpDefault();
    // `xdg-mime default <this build's desktop file>` for the web types; --check-engine: only recorded
    Q_INVOKABLE void makeDefault();
    // A notification on the desktop (org.freedesktop.Notifications: the shell's on Tacit) for `key`, taking the place of
    // the one sent for it before: `actions` as the specification lists them (name, label, name, label); `resident` stays
    // until it is dealt with; `transient` is left out of the shell's list (a private window's). An action chosen there
    // comes back as noticeAction.
    Q_INVOKABLE void notify(const QString &key, const QString &summary, const QString &body, const QStringList &actions,
                            bool resident, bool transient);
    // The notification sent for `key` goes, if it is still there.
    Q_INVOKABLE void withdraw(const QString &key);
    QVariantMap lastNotice() const { return m_lastNotice; }
    QString lastWithdrawn() const { return m_lastWithdrawn; }
    QString isDefault() const { return m_default; }
    QString lastDefaulted() const { return m_lastDefaulted; }
    QString lastOpened() const { return m_lastOpened; }
    QString lastRevealed() const { return m_lastRevealed; }

signals:
    void found(const QString &scheme, const QString &name);
    void opened();
    void revealed();
    void defaultFound();
    void noticed();
    void noticeAction(const QString &key, const QString &action);

private slots:
    void noticeChanged(const QDBusMessage &message);

private:
    QHash<QString, uint> m_notices;     // the notification ids, by key
    bool m_listening = false;
    QVariantMap m_lastNotice;
    QString m_lastWithdrawn;
    QString m_lastOpened;
    QString m_lastRevealed;
    QString m_default;
    QString m_lastDefaulted;
};
