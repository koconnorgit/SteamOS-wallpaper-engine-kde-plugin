// Stand-in for Plasma's WallpaperItem, so that plugin/contents/ui/main.qml can
// run in a plain window (see ../../../../../harness.qml). In Plasma `wallpaper`
// is the item itself and `configuration` a property map; here it is a Proxy
// around a plain object that announces every write, which is all main.qml needs.
import QtQuick

Item {
    id: root
    anchors.fill: parent
    readonly property var wallpaper: root
    // the initial values come from the harness's argument, like its steps
    property var configuration: makeConfiguration(
        JSON.parse(Qt.application.arguments[Qt.application.arguments.length - 1]).config)
    signal configWritten(string key, var value)

    function makeConfiguration(values) {
        return new Proxy(values, {
            set(target, key, value) {
                if (target[key] === value) return true;
                target[key] = value;
                root.configWritten(String(key), value);
                // re-evaluates every binding that reads wallpaper.configuration.X
                root.configurationChanged();
                return true;
            }
        });
    }
}
