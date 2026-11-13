import QtQuick
import QtTest
import Browser
import "../qml"

// Downloader.qml: a row's actions on its record (PLAN.md 3.8 8d).
TestCase {
    name: "Downloader"

    Component { id: sessionComponent; Session {} }
    Component { id: downloaderComponent; Downloader {} }

    function init() {
        Store.files = {};
        Disk.removed = [];
    }

    // Try again on a file Thyris quit before: its old part-file goes, so the name is free again and nothing is left behind
    function test_retryAfterAQuitTakesThePartFileLeft() {
        const s = createTemporaryObject(sessionComponent, this);
        const downloader = createTemporaryObject(downloaderComponent, this, { session: s });
        const path = Disk.downloads + "/movie.mkv";
        const failed = () => s.addDownload({ url: "https://example.org/movie.mkv", file: "movie.mkv", path: path, state: "failed", reason: "browser closed" });
        const one = failed(), two = failed();
        downloader.act(one, "retry");
        compare(Disk.removed, [path + ".download"]);
        compare(downloader.fetchUrl, "https://example.org/movie.mkv", "then the fetcher loads it");
        compare(s.downloadById(one).state, "running");
        downloader.act(two, "retry");
        compare(Disk.removed, [path + ".download"], "the first is coming: the part may be its");
    }

    // what Ctrl+Q counts in its ask: the files coming, not one paused or done
    function test_runningCountsWhatIsComing() {
        const s = createTemporaryObject(sessionComponent, this);
        const downloader = createTemporaryObject(downloaderComponent, this, { session: s });
        const add = state => s.addDownload({ url: "https://example.org/" + state, file: state, path: Disk.downloads + "/" + state, state: state });
        compare(downloader.running, 0);
        verify(!downloader.coming);
        add("running"); add("paused"); add("done");
        compare(downloader.running, 1);
        verify(downloader.coming);
        add("running");
        compare(downloader.running, 2);
    }
}
