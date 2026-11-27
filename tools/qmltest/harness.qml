// Runs the plugin's main.qml in a plain window, outside Plasma, and plays a
// list of config changes into it. Driven by run.py; see docs/benchmark.md's
// neighbour, docs/code-review.md (Q5).
//
// qml6 -I imports -I <plugin lib imports> harness.qml -- '<json: {main, config, steps, width, height}>'
import QtQuick
import QtQuick.Window

Window {
    id: win
    visible: true
    color: "black"
    title: "wallpaper-engine-kde qml test"
    flags: Qt.Window | Qt.WindowDoesNotAcceptFocus

    readonly property var spec: JSON.parse(Qt.application.arguments[Qt.application.arguments.length - 1])
    width: spec.width
    height: spec.height
    property double t0: Date.now()
    function say(text) { console.log(`TEST ${String(Date.now() - t0).padStart(6)} ${text}`); }

    Item {
        id: host
        anchors.fill: parent
        // main.qml reads wallpaper.parent.screenGeometry
        property rect screenGeometry: Qt.rect(0, 0, win.width, win.height)
        Loader {
            id: loader
            anchors.fill: parent
            onStatusChanged: if (status === Loader.Error) { win.say("ERROR main.qml failed to load"); Qt.quit(); }
            onLoaded: {
                item.configWritten.connect((key, value) => win.say(`config ${key} = ${String(value).slice(-60)}`));
                win.say("loaded");
            }
        }
    }

    // the Item in main.qml that holds the backends
    function backendLoader() {
        const bg = loader.item ? loader.item.children[0] : null;
        if (!bg) return null;
        for (let i = 0; i < bg.children.length; i++)
            if (bg.children[i].hiddenOpacity !== undefined) return bg.children[i];
        return null;
    }
    property string lastState: ""
    Timer {
        interval: 50; repeat: true; running: true
        onTriggered: {
            const bl = win.backendLoader();
            if (!bl) return;
            const holders = [];
            for (let i = 0; i < bl.children.length; i++) {
                const c = bl.children[i];
                if (c.opacity !== undefined && c.color !== undefined) holders.push(c.opacity >= 1 ? "1" : c.opacity <= 0.01 ? "0" : "~");
            }
            const state = `holders=[${holders.join(",")}] waiting=${bl.waiting} item=${bl.item ? "yes" : "no"} old=${bl.oldItem ? "yes" : "no"}`;
            if (state !== win.lastState) { win.lastState = state; win.say(state); }
        }
    }

    property int step: 0
    Timer {
        id: stepper
        onTriggered: {
            const s = win.spec.steps[win.step++];
            if (s.quit) { win.say("quit"); Qt.quit(); return; }
            win.say(`step: ${s.note || ""}`);
            for (const key in (s.set || {})) loader.item.configuration[key] = s.set[key];
            win.next();
        }
    }
    function next() {
        if (step >= spec.steps.length) return;
        stepper.interval = Math.max(1, spec.steps[step].at - (Date.now() - t0));
        stepper.start();
    }
    Component.onCompleted: {
        loader.source = spec.main;
        next();
    }
}
