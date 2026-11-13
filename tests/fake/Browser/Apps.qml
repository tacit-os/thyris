pragma Singleton
import QtQuick

// Stand-in for the C++ Apps (src/apps.h): what a download's notification and its row's Open and Show in Files would do,
// recorded; noticeAction is called as the desktop would send it, and fileFound as lookUpFile would answer.
QtObject {
    property var withdrawn: []
    property string lastOpened: ""
    property string lastRevealed: ""
    property var lookedUp: []
    signal noticeAction(string key, string action)
    signal fileFound(string path, bool exists, string name)
    function lookUpFile(path) { lookedUp = lookedUp.concat([path]) }
    function notify(key, summary, body, actions, resident, transient) {}
    function withdraw(key) { withdrawn = withdrawn.concat([key]) }
    function open(url) { lastOpened = url }
    function reveal(path) { lastRevealed = path }
}
