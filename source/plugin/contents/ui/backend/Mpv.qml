import QtQuick 2.5
import com.github.catsout.wallpaperEngineKde 1.2
import ".."

Item{
    id: videoItem
    anchors.fill: parent
    property alias source: player.source
    // this wallpaper's own options (see optionsComponent in main.qml)
    property var opts
    readonly property int displayMode: videoItem.opts.displayMode
    readonly property real videoRate: videoItem.opts.speed
    readonly property bool stats: videoItem.opts.mpvStats
    property var volumeFade: Common.createVolumeFade(
        videoItem, 
        Qt.binding(function() { return videoItem.opts.mute ? 0 : videoItem.opts.volume; }),
        (volume) => { player.volume = volume; }
    )
    
    onDisplayModeChanged: {
        if(videoItem.displayMode == Common.DisplayMode.Crop) {
            player.setProperty("keepaspect", true);
            player.setProperty("panscan", 1.0);
        } else if(videoItem.displayMode == Common.DisplayMode.Aspect) {
            player.setProperty("keepaspect", true);
            player.setProperty("panscan", 0.0);
        } else if(videoItem.displayMode == Common.DisplayMode.Scale) {
            player.setProperty("keepaspect", false);
            player.setProperty("panscan", 0.0);
        }
    }

    // Force displayMode update on background.displayMode change
    Timer {
        id: displayModeFixTimer
        interval: 50
        repeat: false
        onTriggered: videoItem.displayModeChanged()
    }
    Connections {
        target: videoItem.opts
        function onDisplayModeChanged() {
            displayModeFixTimer.restart();
        }
    }
    // it's ok for toggle, true will always cause a signal at first
    onStatsChanged: {
        player.command(["script-binding","stats/display-stats-toggle"]);
    }

    onVideoRateChanged: player.setProperty('speed', videoRate);

    // source
    // mute
    // volume
    // fun:setProperty(name,value)
    Mpv {
        id: player
        anchors.fill: parent
        mute: videoItem.opts.mute
        volume: 0
        Connections {
            target: player
            ignoreUnknownSignals: true
            function onFirstFrame() {
                background.sig_backendFirstFrame('mpv', videoItem);
            }
        }
    }
    Component.onCompleted:{
        background.nowBackend = 'mpv';
        videoItem.displayModeChanged();
    }

    function play(){
        // stop pause time to avoid quick switch which cause keep pause 
        pauseTimer.stop();
        player.play();
        volumeFade.start();
    }
    function pause(){
        volumeFade.stop();
        pauseTimer.start();
    }
    Timer{
        id: pauseTimer
        running: false
        repeat: false
        interval: 200
        onTriggered: {
            player.pause();
        }
    }
    function getMouseTarget() {
    }
}
