pragma Singleton
import QtQuick

// Stand-in for the C++ Disk (src/disk.h): the downloads folder, and the files asked to go, which nothing removes.
QtObject {
    property string downloads: "/home/you/Downloads"
    property var removed: []
    function remove(path) { removed = removed.concat([path]); return true }
    function mark(path, attributes) {}
}
