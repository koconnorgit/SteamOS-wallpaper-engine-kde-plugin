import QtQuick 2.5
import QtMultimedia
import ".."

Item{
    id: videoItem
    anchors.fill: parent
    property alias source: player.source
    // this wallpaper's own options (see optionsComponent in main.qml)
    property var opts
    property int displayMode: videoItem.opts.displayMode
    property var volumeFade: Common.createVolumeFade(
        videoItem, 
        Qt.binding(function() { return videoItem.opts.mute ? 0 : videoItem.opts.volume; }),
        (volume) => { audioOut.volume = volume / 100.0; }
    )

    onDisplayModeChanged: {
        if(displayMode == Common.DisplayMode.Scale)
            videoView.fillMode = VideoOutput.Stretch;
        else if(displayMode == Common.DisplayMode.Aspect)
            videoView.fillMode = VideoOutput.PreserveAspectFit;
        else if(displayMode == Common.DisplayMode.Crop)
            videoView.fillMode = VideoOutput.PreserveAspectCrop;
    }

    // Force fillMode update on background.displayMode change
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

    VideoOutput {
        id: videoView
        //fillMode: wallpaper.configuration.FillMode
        anchors.fill: parent
    }
    Connections {
        id: firstFrameWatch
        target: videoView.videoSink
        function onVideoFrameChanged() {
            firstFrameWatch.enabled = false;
            background.sig_backendFirstFrame('QtMultimedia', videoItem);
        }
    }
    AudioOutput {
        id: audioOut
        volume: 0.0
        muted: videoItem.opts.mute
    }
    MediaPlayer {
        id: player
        loops: MediaPlayer.Infinite
        playbackRate: videoItem.opts.speed
        videoOutput: videoView
        audioOutput: audioOut
    }
    Component.onCompleted:{
        background.nowBackend = "QtMultimedia";
        videoItem.displayModeChanged();
    }

    function play(){
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
        interval: 300
        onTriggered: {
            player.pause();
        }
    }
    function getMouseTarget() {
    }
}
