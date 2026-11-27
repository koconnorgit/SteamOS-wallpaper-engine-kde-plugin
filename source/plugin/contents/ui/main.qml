import QtQuick
import com.github.catsout.wallpaperEngineKde
import QtQuick.Window
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

import "js/schedule.mjs" as Sched

WallpaperItem {
Rectangle {
    id: background
    anchors.fill: parent
    color: wallpaper.configuration.BackgroundColor
    
    property string steamlibrary: Qt.resolvedUrl(wallpaper.configuration.SteamLibraryPath).toString()
    property string source: Qt.resolvedUrl(wallpaper.configuration.WallpaperSource).toString()

    property int    videoBackend: wallpaper.configuration.VideoBackend
    property int    fps: wallpaper.configuration.Fps

    property bool   mouseInput: wallpaper.configuration.MouseInput
    property bool   mpvStats: wallpaper.configuration.MpvStats

    property bool   pauseOnBatPower: wallpaper.configuration.PauseOnBatPower
    property int    pauseBatPercent: wallpaper.configuration.PauseBatPercent

    
    readonly property string workshopid: wallpaper.configuration.WallpaperWorkShopId

    // The options one wallpaper plays with: the ones saved for it, over the
    // global ones. Every backend gets a set of its own when it is created and
    // keeps it, so during a crossfade the outgoing wallpaper goes on playing
    // with its own volume, display mode and so on while the incoming one
    // already uses its own.
    Component {
        id: optionsComponent
        QtObject {
            property string wid
            property var saved: ({})
            function saved_or(key, def) {
                return saved.hasOwnProperty(key) ? saved[key] : def;
            }
            // plain bindings: they follow the saved options and the config alike
            readonly property int    displayMode: saved_or('display_mode', wallpaper.configuration.DisplayMode)
            readonly property bool   mute: saved_or('mute_audio', wallpaper.configuration.MuteAudio)
            readonly property int    volume: saved_or('volume', wallpaper.configuration.Volume)
            readonly property real   speed: saved_or('speed', wallpaper.configuration.Speed)
            // user properties for scene wallpapers, as a json string
            readonly property string userPropsJson: saved['user_props'] ? JSON.stringify(saved['user_props']) : ""
            readonly property int    fps: background.fps
            readonly property bool   mpvStats: background.mpvStats

            function reload() {
                pyext.read_wallpaper_config(wid).then((res) => { saved = res; });
            }
            // bumped by the settings dialog when saved options were edited
            readonly property int edits: wallpaper.configuration.PerOptChanged
            onEditsChanged: reload()
            Component.onCompleted: reload()
        }
    }

    // auto pause
    property bool   ok: !windowModel.reqPause && !powerSource.reqPause

    // detect TTY switch and pause wallpaper(s)
    // pause for suspend, and catch up with the schedule on wake
    TTYSwitchMonitor {
        id: ttyMonitor
        onTtySwitch: (sleep) => {
            if (sleep) {
                backendLoader.pauseAll();
            } else {
                background.autoPause();
                background.evaluateSchedule();
            }
        }
    }

    // ---- wallpaper schedule ----
    // Slots fire at clock times or relative to sunrise/sunset and pick a
    // wallpaper or a rotating playlist; see js/schedule.mjs. What should be
    // showing is recomputed from the wall clock on every evaluation, so
    // nothing but a manual-pick hold needs to be remembered.
    // Slots are this monitor's own; playlists are shared by all monitors.
    property var    scheduleConf: Sched.withPlaylists(
        Sched.parse(wallpaper.configuration.ScheduleConf),
        Sched.parsePlaylists(playlistStore.text))
    PlaylistStore {
        id: playlistStore
        // Playlists edited, possibly from another monitor's settings: choose
        // again. A wallpaper picked by hand stays put.
        onTextChanged: Qt.callLater(() => {
            background.scheduleRedecide = true;
            background.evaluateSchedule();
        })
    }
    // Identifies the schedule step that was last applied. The wallpaper for a
    // step is decided once, when the step begins, and then left alone: what
    // was chosen may depend on what the other monitors were showing at that
    // moment. Kept in the config so a restart does not decide again.
    readonly property string scheduleKey: wallpaper.configuration.ScheduleAppliedKey
    // Set when the schedule or a playlist was edited: the current step is
    // decided again, keeping what is showing if that is still a valid choice.
    property bool scheduleRedecide: false
    function setScheduleKey(key) {
        if (wallpaper.configuration.ScheduleAppliedKey !== key)
            wallpaper.configuration.ScheduleAppliedKey = key;
    }
    // What this monitor shows, for the other monitors to steer clear of.
    WallpaperPresence { id: presence }
    readonly property string shownWid: wallpaper.configuration.WallpaperWorkShopId
    onShownWidChanged: presence.wid = shownWid
    // Config values as last acted on, to tell what the settings dialog changed.
    // Change handlers on bound properties are used rather than Connections:
    // in testing, Connections handlers never fired for the config map's
    // capitalised keys.
    property string scheduleSeenConf: ""
    property string scheduleSeenPick: ""
    readonly property string scheduleConfStr: wallpaper.configuration.ScheduleConf
    readonly property string schedulePickStr: wallpaper.configuration.ScheduleManualPick
    onScheduleConfStrChanged: Qt.callLater(background.scheduleConfigChanged)
    onSchedulePickStrChanged: Qt.callLater(background.scheduleConfigChanged)

    SunSchedule {
        id: sunSchedule
        onReadyChanged: background.evaluateSchedule()
        onScheduleChanged: background.evaluateSchedule()
    }
    function scheduleSunTimes() {
        return { sunrises: sunSchedule.sunrises, sunsets: sunSchedule.sunsets };
    }
    function scheduleItemAvailable(item) {
        const dir = Common.wallpaperDirOfSource(item.source);
        return Boolean(dir) && pyext.exists(dir);
    }
    function scheduleHoldUntil() {
        return Number(wallpaper.configuration.ScheduleHoldUntil) || 0;
    }
    function setScheduleHoldUntil(time) {
        const value = time ? String(time) : "";
        if (wallpaper.configuration.ScheduleHoldUntil !== value)
            wallpaper.configuration.ScheduleHoldUntil = value;
    }

    function evaluateSchedule() {
        scheduleTimer.stop();
        const conf = background.scheduleConf;
        if (!conf.enabled || conf.slots.length === 0) {
            presence.until = 0;
            return;
        }
        // wait for real sun times rather than acting on the placeholder ones
        if (Sched.usesSun(conf) && !sunSchedule.ready) return;

        const now = Date.now();
        const res = Sched.resolve(conf, now, scheduleSunTimes(), scheduleItemAvailable);
        // the decision itself is in schedule.mjs, where it is tested
        const d = Sched.decide(res, now, {
            key: background.scheduleKey,
            holdUntil: scheduleHoldUntil(),
            ok: background.ok,
            redecide: background.scheduleRedecide,
            // a playlist skips ahead past wallpapers other monitors show
            taken: Sched.takenBy(presence.othersDetailed(), now),
            current: wallpaper.configuration.WallpaperWorkShopId
        });
        if (d.clearHold) setScheduleHoldUntil(0);
        if (d.item) {
            background.scheduleRedecide = false;
            setScheduleKey(d.key);
            if (wallpaper.configuration.WallpaperWorkShopId !== d.item.wid ||
                wallpaper.configuration.WallpaperSource !== d.item.source) {
                console.log(`schedule: ${Sched.describeTrigger(res.slot.trigger)} -> ${d.item.wid}` +
                            (d.item === res.item ? "" : ` (${res.item.wid} is on another monitor)`));
                wallpaper.configuration.WallpaperWorkShopId = d.item.wid;
                wallpaper.configuration.WallpaperSource = d.item.source;
            }
            // the other monitors may decide before the binding catches up
            presence.wid = d.item.wid;
        }
        presence.until = d.staysUntil;

        // Re-check at least once a minute: QML timers do not account for
        // time spent suspended, and the system clock can be changed.
        const delay = d.wake === null ? 60000 : d.wake - now + 100;
        scheduleTimer.interval = Math.max(1000, Math.min(60000, delay));
        scheduleTimer.start();

        // the first evaluation decides what the first backend shows
        background.startBackend();
    }
    Timer {
        id: scheduleTimer
        repeat: false
        onTriggered: background.evaluateSchedule()
    }

    // The settings dialog writes all its values in one go; look at them together.
    function scheduleConfigChanged() {
        const confStr = wallpaper.configuration.ScheduleConf;
        const pick = wallpaper.configuration.ScheduleManualPick;
        const edited = confStr !== background.scheduleSeenConf;
        const picked = pick !== background.scheduleSeenPick;
        background.scheduleSeenConf = confStr;
        background.scheduleSeenPick = pick;

        if (edited) {
            // an edited schedule takes effect right away
            setScheduleHoldUntil(0);
            background.scheduleRedecide = true;
        } else if (picked) {
            const res = Sched.resolve(background.scheduleConf, Date.now(), scheduleSunTimes(), scheduleItemAvailable);
            setScheduleHoldUntil(res && res.nextSlotAt ? res.nextSlotAt : 0);
        }
        evaluateSchedule();
    }
    onOkChanged: if (background.ok) background.evaluateSchedule()

    // One-time conversion of the old Day/Night switch into two schedule slots.
    function migrateLegacySchedule() {
        const c = wallpaper.configuration;
        if (!c.DayNightSwitch) return;
        c.DayNightSwitch = false;
        if (c.ScheduleConf) return;
        const conf = Sched.fromDayNight(
            { wid: c.DayWallpaperWorkShopId, source: String(c.DayWallpaperSource) },
            { wid: c.NightWallpaperWorkShopId, source: String(c.NightWallpaperSource) });
        if (conf.enabled) c.ScheduleConf = Sched.serialize(conf);
    }

    // Playlists used to be stored with each monitor's schedule. Move any
    // found there into the shared store.
    function migrateSharedPlaylists() {
        const c = wallpaper.configuration;
        if (!c.ScheduleConf) return;
        const conf = Sched.parse(c.ScheduleConf);
        if (conf.playlists.length === 0) return;
        const merged = Sched.mergePlaylists(Sched.parsePlaylists(playlistStore.text), conf.playlists);
        if (!playlistStore.save(Sched.serializePlaylists(merged))) {
            console.error("schedule: could not write", playlistStore.path);
            return;
        }
        c.ScheduleConf = Sched.serialize(conf);
    }

    property string nowBackend: ""
    // length of the fade between wallpapers, in ms; 0 switches at once
    property int fadeDuration: wallpaper.configuration.FadeDuration

    property var mouseHooker
    property bool hasLib: Common.checklib_wallpaper(background)

    property var customConf: Common.loadCustomConf(wallpaper.configuration.CustomConf)

    property string wallpaperPath
    property string wallpaperType

    // a backend has drawn; `item` is the backend's root, so that a late frame
    // from a wallpaper on its way out is not taken for the incoming one's
    signal sig_backendFirstFrame(string backname, var item)

    Component.onDestruction: {
        if(mouseHooker) {
            mouseHooker.destroy();
        }
    }

    function applySource() {
        const { path, type } = Common.unpackWallpaperSource(source);
        const path_changed = background.wallpaperPath !== path;
        const type_changed = background.wallpaperType !== type;
        const is_infobackend = background.nowBackend === "InfoShow";

        if(type_changed) wallpaperType = type;
        if(path_changed) wallpaperPath = path;

        // A changed wallpaper always gets a fresh backend, so that it can be
        // faded in over the one it replaces.
        if(type_changed || path_changed || is_infobackend || !source) {
            loadBackend();
        }

        sourceCallback();
    }

    onMouseInputChanged: {
        if(this.mouseInput) {
            hookTimer.start();
        }
        else if(this.mouseHooker) {
            this.mouseHooker.target = null;
            this.mouseHooker.destroy();
            this.mouseHooker = null;
        }
    }

    Timer {
        id: hookTimer
        running: true
        repeat: false
        interval: 2000
        property int tryTimes: 0
        onTriggered: {
            tryTimes++;
            if(tryTimes >= 10 || !background.hasLib || !background.mouseInput) return;
            if(background.mouseHooker) return;
            background.hookMouse();
        }
        Component.onCompleted: {
            background.hookMouse.connect(background.hookMouseSlot);
        }
    }
    signal hookMouse
    function hookMouseSlot() {
        if(!background.doHookMouse()) {
            hookTimer.start();
        } else {
            hookTimer.tryTimes = 0;
        }
    }
    function doHookMouse() {
        if(background.Window) {
            const screenArea = Common.findItem(Window.contentItem, "MouseEventListener");
            if(screenArea === null)
                return false;
            const screenGrid = Common.findItem(screenArea, "QQuickGridView");
            if(screenGrid === null)
                return false;
            console.error(screenGrid);
            if(background.mouseHooker) background.mouseHooker.destroy();
            background.mouseHooker = Qt.createQmlObject(`import QtQuick 2.12;
                    import com.github.catsout.wallpaperEngineKde 1.2
                    MouseGrabber {
                        z: -1
                        anchors.fill: parent
                    }
            `, screenGrid);
            return true;
       }
       return false;
    }

    WindowModel {
        id: windowModel
        screenGeometry: wallpaper.parent.screenGeometry
        filterByScreen: wallpaper.configuration.PauseFilterByScreen
        modePlay: wallpaper.configuration.PauseMode
        resumeTime: wallpaper.configuration.ResumeTime
    }

    PowerSource {
        id: powerSource
        readonly property bool reqPause: {
            (background.pauseOnBatPower && (st_battery_state == 'NoCharge' || st_battery_state == 'Discharging')) ||
            (background.pauseBatPercent !== 0 && st_battery_has && st_battery_percent < background.pauseBatPercent)
        }
    }

    Pyext {
        id: pyext
    }
    // lauch pause time to avoid freezing
    Timer {
        id: lauchPauseTimer
        running: false
        repeat: false
        interval: 300
        onTriggered: {
            if(backendLoader.item) backendLoader.item.pause();
            playTimer.start();
        }
    }
    Timer{
        id: playTimer
        running: false
        repeat: false
        interval: 5000
        onTriggered: { background.autoPause(); }
    }
    // lauch pause end

    // As always autoplay for refresh lastframe, sourceChange need autoPause
    // need a time for delay, which is needed for refresh
    function sourceCallback() {
        sourcePauseTimer.start();   
    }
    Timer {
        id: sourcePauseTimer
        running: false
        repeat: false
        interval: 200
        onTriggered: {
            // autoPause comes back here while an incoming wallpaper still has
            // to draw and fade in
            background.autoPause();
        }
    }
    // main  
    Item { 
        id: backendLoader
        anchors.fill: parent
        property var item: null
        // Each backend sits in a holder. A new wallpaper is brought up above
        // the one showing, kept invisible until it has drawn a frame, faded
        // in, and only then is the old one dropped.
        property var holder: null
        property var oldHolder: null
        // the backend in oldHolder, still playing under the incoming one
        property var oldItem: null
        property bool waiting: false
        readonly property bool inTransition: waiting || fadeAnim.running
        // Just above the point where the scene graph stops drawing an item,
        // which the backends need in order to produce their first frame.
        readonly property real hiddenOpacity: 0.002

        signal loaded

        Component {
            id: holderComponent
            Rectangle {
                anchors.fill: parent
                // covers the old wallpaper where the new one leaves bars
                color: background.color
                // blend backing and wallpaper as one image while fading
                layer.enabled: opacity < 1
            }
        }
        NumberAnimation {
            id: fadeAnim
            property: "opacity"
            to: 1
            duration: background.fadeDuration
            easing.type: Easing.InOutQuad
            onFinished: backendLoader.dropOld()
        }
        // For backends that never report a first frame. A scene always does
        // once it has loaded, so a scene that stays silent has failed (its
        // files are damaged, say): what is on screen is kept. The other
        // backends are shown regardless.
        Timer {
            id: firstFrameTimeout
            property bool giveUp: false
            interval: giveUp ? 15000 : 5000
            onTriggered: {
                if(giveUp) {
                    console.error(`wallpaper ${background.workshopid} never drew a frame, keeping the previous one`);
                    backendLoader.settle();
                    background.autoPause();
                } else {
                    backendLoader.reveal();
                }
            }
        }

        Component.onCompleted: {
            background.sig_backendFirstFrame.connect((backname, item) => {
                if(item === backendLoader.item) backendLoader.reveal();
            });
            if(background.hasLib) {
                this.loaded.connect(this.changeMouseTarget);
                background.mouseHookerChanged.connect(this.changeMouseTarget);
            }
        }
        function reveal() {
            if(!backendLoader.waiting) return;
            backendLoader.waiting = false;
            firstFrameTimeout.stop();
            fadeAnim.target = backendLoader.holder;
            fadeAnim.from = backendLoader.holder.opacity;
            fadeAnim.restart();
        }
        function dropOld() {
            if(this.oldHolder) this.oldHolder.destroy();
            this.oldHolder = null;
            this.oldItem = null;
        }
        function pauseAll() {
            if(this.item) this.item.pause();
            if(this.oldItem) this.oldItem.pause();
        }
        // Bring a transition still under way to an end, ahead of a new one.
        function settle() {
            if(this.waiting) {
                // the newest wallpaper was never seen: keep what is on screen
                this.waiting = false;
                firstFrameTimeout.stop();
                this.holder.destroy();
                this.holder = this.oldHolder;
                this.oldHolder = null;
                this.item = this.oldItem;
                this.oldItem = null;
            } else if(fadeAnim.running) {
                fadeAnim.stop();
                this.holder.opacity = 1;
                this.dropOld();
            }
        }
        // `wid`: the wallpaper's id, for backends that take options
        function load(url, properties, wid) {
            const com = Qt.createComponent(url);
            if(com.status === Component.Ready) {
                this.settle();
                const fade = this.holder !== null && background.fadeDuration > 0;
                const next = holderComponent.createObject(this, {
                    opacity: fade ? this.hiddenOpacity : 1
                });
                let obj = null;
                let error = "";
                // lives and dies with the holder
                if(wid !== undefined)
                    properties.opts = optionsComponent.createObject(next, { wid: wid });
                try {
                    obj = com.createObject(next, properties);
                } catch(e) {
                    error = String(e);
                }
                // createObject reports most failures by returning null
                if(!obj) {
                    next.destroy();
                    if(url === "backend/InfoShow.qml") {
                        console.error("could not create the info backend:", error || com.errorString());
                        return;
                    }
                    this.loadInfoShow(error || com.errorString() || "Could not create the wallpaper backend.");
                    return;
                }
                this.oldHolder = this.holder;
                this.oldItem = this.item;
                this.holder = next;
                this.item = obj;
                if(fade) {
                    this.waiting = true;
                    if(url === "backend/InfoShow.qml") this.reveal();
                    else {
                        firstFrameTimeout.giveUp = url === "backend/Scene.qml";
                        firstFrameTimeout.restart();
                    }
                } else {
                    this.dropOld();
                }
                this.loaded();
            } else if(com.status == Component.Error) {
                this.loadInfoShow(com.errorString());
            }
        }
        function loadInfoShow(info) {
            this.load("backend/InfoShow.qml", {
                wid: background.workshopid,
                type: background.wallpaperType,
                info: info
            });
        }
        function changeMouseTarget() {
           if(backendLoader.item && background.mouseHooker) {
                let re = backendLoader.item.getMouseTarget();
                if(!re)
                    re = null;
                background.mouseHooker.target = re;
           }
        }
    }

    function loadBackend() {
        let qmlsource = "";
        let properties = {};

    
        // check source
        if(!background.source) {
            backendLoader.loadInfoShow("Source is empty. The config may be broken.");
            return;
        }
        // choose backend
        switch (background.wallpaperType) {
            case 'video':
                if(background.videoBackend == Common.VideoBackend.Mpv && background.hasLib)
                    qmlsource = "backend/Mpv.qml";
                else qmlsource = "backend/QtMultimedia.qml";
                properties = {};
                break;
            case 'web':
                qmlsource = "backend/QtWebView.qml";
                properties = {readfile: pyext.readfile};
                break;
            case 'scene':
                if(background.hasLib) {
                    qmlsource = "backend/Scene.qml";
                    properties = {"assets": Common.getAssetsPath(steamlibrary)};
                } else {
                    backendLoader.loadInfoShow("Plugin lib not found. To support scene, please compile and install it.");
                    return; 
                }
                break;
            default:
                backendLoader.loadInfoShow("Not supported wallpaper type");
                return; 
        }
        properties['source'] = background.wallpaperPath;
        // The id is the wallpaper's folder name. Taken from the source, not
        // from the config's id, which may be written before or after it.
        const wid = Common.wallpaperDirOfSource(background.source).split('/').pop();
        backendLoader.load(qmlsource, properties, wid);
        sourceCallback();
    }
   
    function autoPause() {
        const item = backendLoader.item;
        const old = backendLoader.oldItem;
        if(background.ok) {
            if(item) item.play();
            if(old) old.play();
            return;
        }
        // the wallpaper on its way out stops at once, sound included
        if(old) old.pause();
        // the incoming one has to draw and fade in before it can be paused
        if(backendLoader.inTransition) sourcePauseTimer.restart();
        else if(item) item.pause();
    }

    // Loads the first backend, once. Held back until the schedule has had its
    // say, so that a wallpaper the schedule is about to replace is not loaded
    // first.
    property bool backendStarted: false
    function startBackend() {
        if(backendStarted) return;
        backendStarted = true;
        startTimer.stop();

        applySource();

        // background signal connect
        background.videoBackendChanged.connect(() => {
            // only videos use the video backend
            if(background.wallpaperType === 'video') background.loadBackend();
        });
        background.okChanged.connect(autoPause);
        background.sourceChanged.connect(applySource);

        lauchPauseTimer.start();
    }
    // sun times normally arrive within a few hundred ms; don't wait for long
    Timer {
        id: startTimer
        interval: 1500
        onTriggered: background.startBackend()
    }

    Component.onCompleted: {
        presence.wid = shownWid;
        migrateLegacySchedule();
        migrateSharedPlaylists();
        scheduleSeenConf = wallpaper.configuration.ScheduleConf;
        scheduleSeenPick = wallpaper.configuration.ScheduleManualPick;
        // starts the backend itself if it got as far as deciding
        evaluateSchedule();
        if(!backendStarted) {
            const conf = background.scheduleConf;
            const waitForSun = conf.enabled && conf.slots.length > 0 &&
                               Sched.usesSun(conf) && !sunSchedule.ready;
            if(waitForSun) startTimer.start();
            else startBackend();
        }
    }
}
}
