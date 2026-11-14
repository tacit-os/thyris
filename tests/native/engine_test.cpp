// Tests of Engine's favicon copies (keepIcon, keptIcon) that need no engine: a stand-in favicon provider answers in place
// of Qt WebEngine's or CEF's, a QObject marked offTheRecord stands in for a private window's profile, and the cache is a
// folder of the test's own.
#include "engine.h"

#include <QBuffer>
#include <QDirIterator>
#include <QGuiApplication>
#include <QImage>
#include <QPointer>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

class Response : public QQuickImageResponse {
public:
    explicit Response(QImage image) : m_image(std::move(image)) {}
    QQuickTextureFactory *textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    void finish() { emit finished(); }
private:
    QImage m_image;
};

// Answers each ask on the next turn of the event loop (or when let go, with `hold`), with 32 px of noise that differs by
// address: each copy about as big as a photo-like favicon's
class Provider : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &) override {
        asked << id;
        QImage image(32, 32, QImage::Format_ARGB32);
        QRandomGenerator noise(qHash(id));
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
                image.setPixel(x, y, noise.generate() | 0xff000000);
        auto *response = new Response(image);
        if (hold)
            waiting << response;
        else
            QMetaObject::invokeMethod(response, &Response::finish, Qt::QueuedConnection);
        return response;
    }
    QStringList asked;
    bool hold = false;
    QList<QPointer<Response>> waiting;
};

// a frame of a page that draws its favicon each frame (a canvas's toDataURL()): 16 px, frame `n` in its first pixels
QString frame(int n) {
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(0xff2a9d5c);
    image.setPixel(0, 0, 0xff000000 | uint(n));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return u"image://favicon/data:image/png;base64,"_s + QString::fromLatin1(png.toBase64());
}

QString site(int n) {
    return u"image://favicon/https://site%1.test/favicon.ico"_s.arg(n);
}

}

class EngineTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_cache;
    std::unique_ptr<QQmlEngine> m_qml;
    std::unique_ptr<Engine> m_engine;
    Provider *m_provider = nullptr;

    // every file under the cache
    QStringList files() const {
        QStringList out;
        for (QDirIterator it(m_cache.path(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories); it.hasNext();)
            out << it.next();
        return out;
    }
    std::unique_ptr<QObject> privateProfile() {
        auto profile = std::make_unique<QObject>();
        profile->setProperty("offTheRecord", true);
        m_engine->attach(profile.get());
        return profile;
    }
    // asked and answered, as BrowserView asks on iconChanged
    bool keep(const QString &address, bool offTheRecord) {
        QSignalSpy kept(m_engine.get(), &Engine::iconKept);
        m_engine->keepIcon(address, offTheRecord);
        return kept.wait(1000);
    }

private slots:
    void initTestCase() {
        QVERIFY(m_cache.isValid());
        qputenv("XDG_CACHE_HOME", QFile::encodeName(m_cache.path()));
    }
    void init() {
        m_qml = std::make_unique<QQmlEngine>();
        m_provider = new Provider;
        m_qml->addImageProvider(u"favicon"_s, m_provider);
        m_engine = std::make_unique<Engine>();
        QQmlEngine::setContextForObject(m_engine.get(), m_qml->rootContext());
    }
    void cleanup() {
        m_engine.reset();
        m_qml.reset();
        QDir(m_cache.path()).removeRecursively();
        QDir().mkpath(m_cache.path());
    }

    void privateCopyInMemory() {
        const auto profile = privateProfile();
        QVERIFY(keep(site(0), true));
        QVERIFY(m_engine->keptIcon(site(0)).startsWith(u"data:image/png;base64,"));
        QCOMPARE(files(), QStringList());
    }

    // A page in a private window whose favicon changes every frame: none of the frames is fetched, saved or held; each is
    // drawn from its own address, and the copy of another page's favicon stays
    void framesHoldNothing() {
        const auto profile = privateProfile();
        QVERIFY(keep(site(0), true));
        for (int n = 0; n < 2000; ++n) {
            m_engine->keepIcon(frame(n), true);
            if (n % 50 == 0)
                QCoreApplication::processEvents();
        }
        QTest::qWait(200);
        QCOMPARE(m_provider->asked, QStringList{site(0).mid(16)});
        for (int n = 0; n < 2000; n += 97)
            QCOMPARE(m_engine->keptIcon(frame(n)), frame(n).mid(16));
        QVERIFY(m_engine->keptIcon(site(0)).startsWith(u"data:image/png;base64,"));
        QCOMPARE(files(), QStringList());
    }

    // Favicons of 600 sites in a private window: their copies take at most 2 MiB, the one drawn longest ago going first;
    // one drawn throughout stays. A copy that went is not to be had: never asked for again (that would be in the main
    // profile), never saved.
    void copiesCapped() {
        const auto profile = privateProfile();
        for (int n = 0; n < 600; ++n) {
            QVERIFY(keep(site(n), true));
            m_engine->keptIcon(site(0));
        }
        qsizetype bytes = 0;
        int held = 0;
        for (int n = 0; n < 600; ++n) {
            const QString kept = m_engine->keptIcon(site(n));
            if (!kept.isEmpty()) {
                bytes += (site(n).size() + kept.size()) * qsizetype(sizeof(QChar));
                ++held;
            }
        }
        qInfo("%d copies held, %lld kB", held, qlonglong(bytes / 1024));
        QVERIFY(bytes <= 2 << 20);
        QVERIFY(held > 100 && held < 600);
        QVERIFY(!m_engine->keptIcon(site(0)).isEmpty());
        QVERIFY(!m_engine->keptIcon(site(599)).isEmpty());
        QCOMPARE(m_engine->keptIcon(site(1)), QString());
        QTest::qWait(200);
        QCOMPARE(m_provider->asked.size(), 600);
        QCOMPARE(files(), QStringList());
    }

    // What private windows showed goes with the last of them: then no copy is held, and the address is the main profile's
    // like any other (asked for by a main window, it is saved)
    void emptiedWithLastPrivateWindow() {
        auto first = privateProfile(), second = privateProfile();
        QVERIFY(keep(site(0), true));
        first.reset();
        QTest::qWait(50);
        QVERIFY(m_engine->keptIcon(site(0)).startsWith(u"data:image/png;base64,"));
        second.reset();
        QTest::qWait(50);
        QVERIFY(keep(site(0), false));
        QVERIFY(m_engine->keptIcon(site(0)).startsWith(u"file://"));
        QCOMPARE(files().size(), 1);
    }

    // An ask for a private window still under way when the last one closes: nothing of its answer is kept, on disk or not
    void answerAfterTheLastPrivateWindow() {
        auto profile = privateProfile();
        m_provider->hold = true;
        QSignalSpy kept(m_engine.get(), &Engine::iconKept);
        m_engine->keepIcon(site(0), true);
        profile.reset();
        QTest::qWait(50);
        QCOMPARE(m_provider->waiting.size(), 1);
        m_provider->waiting.first()->finish();
        QTest::qWait(50);
        QCOMPARE(kept.size(), 0);
        QCOMPARE(files(), QStringList());
    }

    // A main window's favicons are saved as before; one that changes every frame there is drawn from each frame's own
    // address too, none asked for or saved
    void mainWindows() {
        QVERIFY(keep(site(0), false));
        QVERIFY(m_engine->keptIcon(site(0)).startsWith(u"file://"));
        for (int n = 0; n < 500; ++n)
            m_engine->keepIcon(frame(n), false);
        QTest::qWait(200);
        QCOMPARE(m_provider->asked, QStringList{site(0).mid(16)});
        QCOMPARE(m_engine->keptIcon(frame(499)), frame(499).mid(16));
        QCOMPARE(files().size(), 1);
    }
};

int main(int argc, char **argv) {
    // Qt Quick's texture factories need a GUI application, and nothing here needs a screen
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    EngineTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "engine_test.moc"
