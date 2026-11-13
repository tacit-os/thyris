#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

ThemeSource::ThemeSource(QObject *parent) : QObject(parent) {
    const QString generated = QDir::homePath() + QStringLiteral("/.local/state/quickshell/user/generated");
    m_colorsPath = generated + QStringLiteral("/colors.json");
    m_terminalPath = generated + QStringLiteral("/terminal/sequences.txt");
    m_configPath = QDir::homePath() + QStringLiteral("/.config/illogical-impulse/config.json");

    // matugen replaces files, which drops them from the watcher; the debounce re-adds them.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &ThemeSource::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_debounce, qOverload<>(&QTimer::start));
    reload();
}

void ThemeSource::reload() {
    // a file read while it is being written does not parse: what was read before stays
    QFile colors(m_colorsPath);
    if (colors.open(QIODevice::ReadOnly))
        if (const QJsonObject read = QJsonDocument::fromJson(colors.readAll()).object(); !read.isEmpty())
            m_colors = read.toVariantMap();

    QFile terminal(m_terminalPath);
    if (terminal.open(QIODevice::ReadOnly)) {
        static const QRegularExpression osc11(QStringLiteral("\\]11;(#[0-9a-fA-F]{6})"));
        const auto match = osc11.match(QString::fromUtf8(terminal.readAll()));
        m_terminal = match.hasMatch() ? QColor(match.captured(1)) : QColor();
    }
    if (!m_terminal.isValid())
        m_terminal = QColor(m_colors.value(QStringLiteral("surface_container_low")).toString());

    QFile config(m_configPath);
    const QJsonObject root = config.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(config.readAll()).object() : QJsonObject();
    if (!root.isEmpty()) {
        const QJsonObject appearance = root.value(u"appearance").toObject();
        m_fonts = appearance.value(u"fonts").toObject().toVariantMap();
        const QJsonObject transparency = appearance.value(u"transparency").toObject();
        m_contentTransparency = transparency.value(u"contentTransparency").toDouble(0.55);
        const QString wallpaper = root.value(u"background").toObject().value(u"wallpaperPath").toString();
        m_wallpaper = wallpaper.isEmpty() ? QUrl() : QUrl::fromLocalFile(wallpaper);
    }

    // the files and their folders: a file the shell writes for the first time (Thyris started before it) or makes again
    // after a while is read then too; a folder not made yet is watched through the nearest one that is, inside home
    const QString home = QDir::homePath();
    for (const QString &path : {m_colorsPath, m_terminalPath, m_configPath}) {
        if (!m_watcher.files().contains(path) && QFile::exists(path))
            m_watcher.addPath(path);
        QString dir = QFileInfo(path).absolutePath();
        while (!QFileInfo::exists(dir) && dir.size() > home.size())
            dir = QFileInfo(dir).absolutePath();
        if (dir != home && !m_watcher.directories().contains(dir))
            m_watcher.addPath(dir);
    }
    emit changed();
}
