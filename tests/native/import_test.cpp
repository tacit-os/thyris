// Tests of the tabs the imports read from another browser's session file, on the fixtures in tests/fixtures/import:
// zen/sessionstore.jsonlz4 (one window, two workspaces: an essential, pinned tabs, an about:blank pin, two others, and a
// closed window's pin), firefox/sessionstore-backups/recovery.jsonlz4 (two windows, a pin in each) and
// chrome/Sessions/Session_13370000000000000 (SNSS 3 as Chrome writes it: each tab's pinned state, one pinned later, one
// unpinned again, a closed pin and a popup's).
#include "chromeimport.h"
#include "zen.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace Qt::StringLiterals;

namespace {

// "url title space pinned" for each tab, the scheme and the slash left out of the address
QStringList lines(const QVariantList &tabs, const QString &place) {
    QStringList out;
    for (const QVariant &v : tabs) {
        const QVariantMap t = v.toMap();
        out << t.value(u"url"_s).toString().remove(u"https://"_s).remove(u'/') + u' ' + t.value(u"title"_s).toString() + u" ["
                + t.value(place).toString() + u"] " + (t.value(u"pinned"_s).toBool() ? u"pinned"_s : u"-"_s);
    }
    return out;
}

}

class ImportTest : public QObject {
    Q_OBJECT
private slots:
    void zenPinsAndEssentials() {
        QTemporaryDir into;
        bool ok = false;
        const QVariantList tabs = Zen::readTabs(SRCDIR u"/zen"_s, into.path(), ok);
        QVERIFY(ok);
        QCOMPARE(lines(tabs, u"space"_s), QStringList({u"ess.test Essential [] pinned"_s, u"pin.test Pinned [Work] pinned"_s,
                                                       u"pin2.test Pinned two [Home] pinned"_s, u"one.test One [Work] -"_s,
                                                       u"two.test Two [Home] -"_s}));
    }
    void firefoxPins() {
        QTemporaryDir into;
        bool ok = false;
        const QVariantList tabs = Zen::readTabs(SRCDIR u"/firefox"_s, into.path(), ok);
        QVERIFY(ok);
        QCOMPARE(lines(tabs, u"space"_s), QStringList({u"ff-pin.test Firefox pin [] pinned"_s, u"ff-pin2.test Second window's pin [] pinned"_s,
                                                       u"ff.test Firefox tab [] -"_s}));
    }
    void chromePins() {
        QCOMPARE(lines(ChromeImport::tabsOf(SRCDIR u"/chrome"_s), u"group"_s),
                 QStringList({u"cr-pin.test Pin [] pinned"_s, u"cr-pin2.test Pin two [] pinned"_s, u"cr-tab.test Tab [] -"_s,
                              u"cr-unpinned.test Unpinned [] -"_s}));
    }
};

QTEST_GUILESS_MAIN(ImportTest)
#include "import_test.moc"
