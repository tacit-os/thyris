pragma Singleton
import QtQuick

// Stand-in for the C++ Apps (src/apps.h): what a download's notification and its row's Open and Show in Files would do,
// recorded; noticeAction is called as the desktop would send it.
QtObject {
    property var withdrawn: []
    property string lastOpened: ""
    property string lastRevealed: ""
    signal noticeAction(string key, string action)
    function notify(key, summary, body, actions, resident, transient) {}
    function withdraw(key) { withdrawn = withdrawn.concat([key]) }
    function open(url) { lastOpened = url }
    function reveal(path) { lastRevealed = path }
}
