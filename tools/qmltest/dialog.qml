// Opens the plugin's settings page (config.qml) in a plain window, the way
// Plasma's wallpaper settings dialog embeds it, and reports what its wallpaper
// list loaded. For checking the page still works after a change; see run.py.
//
// qml6 -I <plugin lib imports> dialog.qml -- '<json: {config, values, seconds}>'
import QtQuick
import QtQuick.Window

Window {
    id: win
    visible: true
    width: 1500
    height: 900
    title: "wallpaper-engine-kde settings test"
    flags: Qt.Window | Qt.WindowDoesNotAcceptFocus

    readonly property var spec: JSON.parse(Qt.application.arguments[Qt.application.arguments.length - 1])
    property double t0: Date.now()
    function say(text) { console.log(`TEST ${String(Date.now() - t0).padStart(6)} ${text}`); }

    Loader {
        id: loader
        anchors.fill: parent
        onStatusChanged: if (status === Loader.Error) { win.say("ERROR config.qml failed to load"); Qt.quit(); }
        onLoaded: win.say("loaded")
    }

    // any ListModel-like object a few levels down with a count, for the wallpaper grid
    function findListModel(item, depth) {
        if (!item || depth > 12) return null;
        if (item.countNoFilter !== undefined && item.model !== undefined) return item;
        const kids = (item.children || []).concat(item.resources || []);
        for (let i = 0; i < kids.length; i++) {
            const r = findListModel(kids[i], depth + 1);
            if (r) return r;
        }
        return null;
    }
    property int lastCount: -1
    Timer {
        interval: 100; repeat: true; running: true
        onTriggered: {
            const m = win.findListModel(loader.item, 0);
            if (!m) return;
            if (m.countNoFilter !== win.lastCount) {
                win.lastCount = m.countNoFilter;
                win.say(`wallpapers listed: ${m.countNoFilter}`);
            }
        }
    }
    Timer {
        interval: win.spec.seconds * 1000; running: true
        onTriggered: { win.say("quit"); Qt.quit(); }
    }
    Component.onCompleted: {
        const props = Object.assign({ wallpaperConfiguration: win.spec.values }, win.spec.cfg);
        loader.setSource(win.spec.config, props);
    }
}
