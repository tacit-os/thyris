import QtQuick
import QtTest
import Browser
import "../qml"
import "../qml/Downloads.js" as Downloads

// Downloader.qml: a row's actions on its record (PLAN.md 3.8 8d).
TestCase {
    name: "Downloader"

    Component { id: sessionComponent; Session {} }
    Component { id: downloaderComponent; Downloader {} }

    function init() {
        Store.files = {};
        Disk.removed = [];
        Apps.withdrawn = [];
        Apps.lookedUp = [];
        Apps.lastOpened = "";
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

    // A notification's action is its row's (Main.downloadEnded), and only the session that has the file takes it; dealing
    // with a failure, there or in the rail, withdraws its notification, which stays until then
    function test_aNotificationsActionIsTheRowsAndDealtWithItGoes() {
        const s = createTemporaryObject(sessionComponent, this), other = createTemporaryObject(sessionComponent, this);
        const downloader = createTemporaryObject(downloaderComponent, this, { session: s });
        createTemporaryObject(downloaderComponent, this, { session: other });
        const done = s.addDownload({ url: "https://example.org/a.pdf", file: "a.pdf", path: Disk.downloads + "/a.pdf", state: "done" });
        Apps.noticeAction(done, "show");
        compare(Apps.lastRevealed, Disk.downloads + "/a.pdf");
        Apps.noticeAction(done, "open");
        Apps.fileFound(Disk.downloads + "/a.pdf", true, "Papers");
        compare(Apps.lastOpened, Disk.downloads + "/a.pdf");
        verify(s.downloadById(done).opened > 0);
        compare(Apps.withdrawn, [], "a finished file's notification is left to the shell");
        const failed = s.addDownload({ url: "https://example.org/b.iso", file: "b.iso", path: Disk.downloads + "/b.iso", state: "failed", reason: "network" });
        Apps.noticeAction(failed, "retry");
        compare(Apps.withdrawn, [failed]);
        compare(s.downloadById(failed).state, "running", "Try again fetches it again");
        const gone = s.addDownload({ url: "https://example.org/c.iso", file: "c.iso", path: Disk.downloads + "/c.iso", state: "failed", reason: "network" });
        downloader.act(gone, "remove");
        compare(Apps.withdrawn, [failed, gone], "Remove in the rail deals with it too");
        verify(!s.downloadById(gone));
    }

    // Open looks first (Apps.lookUpFile): a file that is gone, or one no app opens, opens nothing, says why in its row and is
    // not counted as opened; once an app takes it, it opens and the row's words go
    function test_openSaysWhyNothingOpened() {
        const s = createTemporaryObject(sessionComponent, this);
        const downloader = createTemporaryObject(downloaderComponent, this, { session: s });
        const zip = Disk.downloads + "/camss-v3.zip", gone = Disk.downloads + "/notes.txt", bare = Disk.downloads + "/README";
        const add = (file, path) => s.addDownload({ url: "https://example.org/" + file, file: file, path: path, state: "done", total: 10, received: 10 });
        const z = add("camss-v3.zip", zip), g = add("notes.txt", gone), b = add("README", bare);
        downloader.act(z, "open");
        compare(Apps.lookedUp, [zip]);
        compare(Apps.lastOpened, "", "nothing opens before the answer");
        Apps.fileFound(zip, true, "");
        compare(Apps.lastOpened, "");
        verify(!s.downloadById(z).opened);
        compare(Downloads.parts(s.downloadById(z), Date.now()).note, "No app opens .zip files");
        downloader.act(g, "open");
        Apps.fileFound(gone, false, "");
        verify(!s.downloadById(g).opened);
        compare(Downloads.parts(s.downloadById(g), Date.now()).note, "The file is gone");
        downloader.act(b, "open");
        Apps.fileFound(bare, true, "");
        compare(Downloads.parts(s.downloadById(b), Date.now()).note, "No app opens this file");
        Apps.fileFound(zip, true, "File Roller");
        compare(Apps.lastOpened, "", "an answer nobody asked for opens nothing");
        downloader.act(z, "open");
        Apps.fileFound(zip, true, "File Roller");
        compare(Apps.lastOpened, zip);
        verify(s.downloadById(z).opened > 0);
        compare(Downloads.parts(s.downloadById(z), Date.now()).note, "");
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
