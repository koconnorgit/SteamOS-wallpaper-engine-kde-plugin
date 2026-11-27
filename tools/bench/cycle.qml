// Creates and destroys scene wallpapers in a window, the way a playlist with
// crossfade does inside plasmashell, so the Qt/OpenGL side of the plugin is
// exercised (texture import, paint node teardown). Driven by run.py, which
// reads the BENCH lines from stderr. See docs/benchmark.md.
//
// qml6 -I <import dir> cycle.qml -- '<json: {assets, scenes[], cycles, warmup, width, height}>'

import QtQuick
import QtQuick.Window
import com.github.catsout.wallpaperEngineKde 1.2

Window {
    id: win
    visible: true
    color: "black"
    title: "wallpaper-engine-kde benchmark"
    flags: Qt.Window | Qt.WindowDoesNotAcceptFocus

    readonly property var conf: JSON.parse(Qt.application.arguments[Qt.application.arguments.length - 1])
    width: conf.width
    height: conf.height

    property int cycle: -conf.warmup   // negative while warming up
    property var current: null
    property double createdAt: 0

    // frame pacing: longest gap between presented frames inside a watch window
    property double lastSwap: 0
    property double maxGap: 0
    property bool watching: false
    onFrameSwapped: {
        const now = Date.now();
        if (watching && lastSwap > 0 && now - lastSwap > maxGap) maxGap = now - lastSwap;
        lastSwap = now;
    }
    function watch() { maxGap = 0; lastSwap = 0; watching = true; }
    function unwatch() { watching = false; return maxGap; }

    // keeps the window rendering every refresh, so a stalled frame shows up as a gap
    Rectangle {
        width: 8; height: 8; color: "#202020"
        z: 10
        NumberAnimation on rotation { from: 0; to: 360; duration: 1000; loops: Animation.Infinite }
    }

    Component {
        id: sceneComponent
        SceneViewer {
            anchors.fill: parent
            fps: 15
            muted: true
            assets: "file://" + win.conf.assets
            onFirstFrame: win.loaded(true)
        }
    }

    function say(text) { console.log("BENCH " + text); }

    function next() {
        if (cycle === 0) {
            say("SAMPLE start");
            sampleTimer.then = begin;
            sampleTimer.start();
        } else if (cycle >= conf.cycles) {
            say("SAMPLE end");
            sampleTimer.then = () => Qt.quit();
            sampleTimer.start();
        } else {
            begin();
        }
    }
    function begin() {
        const scene = conf.scenes[((cycle % conf.scenes.length) + conf.scenes.length) % conf.scenes.length];
        watch();
        createdAt = Date.now();
        current = sceneComponent.createObject(win.contentItem, { source: "file://" + scene });
        loadTimeout.restart();
    }
    function loaded(ok) {
        if (!current || holdTimer.running) return;
        loadTimeout.stop();
        const gap = unwatch();
        if (cycle >= 0) say(`LOAD ${cycle} ${ok ? Date.now() - createdAt : -1} ${gap}`);
        holdTimer.start();
    }
    Timer { id: loadTimeout; interval: 20000; onTriggered: win.loaded(false) }
    Timer {
        id: holdTimer
        interval: 400
        onTriggered: {
            win.watch();
            win.current.destroy();
            win.current = null;
            dropTimer.start();
        }
    }
    Timer {
        id: dropTimer
        interval: 300
        onTriggered: {
            const gap = win.unwatch();
            if (win.cycle >= 0) win.say(`DROP ${win.cycle} ${gap}`);
            win.cycle++;
            win.next();
        }
    }
    // gives run.py time to sample the process after a SAMPLE line
    Timer {
        id: sampleTimer
        interval: 3000
        property var then
        onTriggered: then()
    }

    Component.onCompleted: next()
}
