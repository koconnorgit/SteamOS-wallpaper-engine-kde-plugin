import QtQuick 2.5
import com.github.catsout.wallpaperEngineKde 1.2
import ".."

Item{
    id: sceneItem
    anchors.fill: parent
    property alias source: player.source
    property string assets: "assets"
    // this wallpaper's own options (see optionsComponent in main.qml)
    property var opts
    property int displayMode: sceneItem.opts.displayMode
    property string userPropsJson: sceneItem.opts.userPropsJson
    property var volumeFade: Common.createVolumeFade(
        sceneItem, 
        Qt.binding(function() { return sceneItem.opts.mute ? 0 : sceneItem.opts.volume; }),
        (volume) => { player.volume = volume / 100.0; }
    )

    onDisplayModeChanged: {
        if(displayMode == Common.DisplayMode.Scale)
            player.fillMode = SceneViewer.STRETCH;
        else if(displayMode == Common.DisplayMode.Aspect)
            player.fillMode = SceneViewer.ASPECTFIT;
        else if(displayMode == Common.DisplayMode.Crop)
            player.fillMode = SceneViewer.ASPECTCROP;
    }

    // Force fillMode update on background.displayMode change
    Timer {
        id: displayModeFixTimer
        interval: 50
        repeat: false
        onTriggered: sceneItem.displayModeChanged()
    }
    Connections {
        target: sceneItem.opts
        function onDisplayModeChanged() {
            displayModeFixTimer.restart();
        }
    }

    SceneViewer {
        id: player
        anchors.fill: parent
        fps: sceneItem.opts.fps
        muted: sceneItem.opts.mute
        speed: sceneItem.opts.speed
        assets: sceneItem.assets
        userProperties: sceneItem.userPropsJson
        Component.onCompleted: {
            player.setAcceptMouse(true);
            player.setAcceptHover(true);
        }

        Connections {
            target: player
            function onFirstFrame() {
                background.sig_backendFirstFrame('scene', sceneItem);
            }
        }
    }

    Component.onCompleted: {
        background.nowBackend = 'scene';
        sceneItem.displayModeChanged();
    }
    function play() {
        volumeFade.start();
        player.play();
    }
    function pause() {
        volumeFade.stop();
        player.pause();
    }
    
    function getMouseTarget() {
        return Qt.binding(function() { return player; })
    }
}
