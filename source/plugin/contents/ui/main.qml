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

    property string filterStr: wallpaper.configuration.FilterStr

    property int    videoBackend: wallpaper.configuration.VideoBackend
    property int    fps: wallpaper.configuration.Fps

    property bool   mouseInput: wallpaper.configuration.MouseInput
    property bool   mpvStats: wallpaper.configuration.MpvStats

    property bool   pauseOnBatPower: wallpaper.configuration.PauseOnBatPower
    property int    pauseBatPercent: wallpaper.configuration.PauseBatPercent

    
    property var curOpt: ({})
    property string workshopid: {
        const wid = wallpaper.configuration.WallpaperWorkShopId;
        pyext.read_wallpaper_config(wid).then((res) => this.curOpt = res);
        return wid;
    }
    function get_opt_value(key, def) {
        if(curOpt.hasOwnProperty(key))
            return curOpt[key];
        return def;
    }

    // Update all derived properties when curOpt changes
    onCurOptChanged: {
        console.log("main.qml onCurOptChanged, curOpt:", JSON.stringify(curOpt));
        displayMode = get_opt_value('display_mode', wallpaper.configuration.DisplayMode);
        mute = get_opt_value('mute_audio', wallpaper.configuration.MuteAudio);
        volume = get_opt_value('volume', wallpaper.configuration.Volume);
        speed = get_opt_value('speed', wallpaper.configuration.Speed);
        const userProps = curOpt['user_props'];
        userPropsJson = userProps ? JSON.stringify(userProps) : "";
        console.log("main.qml updated: displayMode=", displayMode, "speed=", speed, "userPropsJson=", userPropsJson);
    }

    property int    displayMode: get_opt_value('display_mode', wallpaper.configuration.DisplayMode)
    property bool   mute: get_opt_value('mute_audio', wallpaper.configuration.MuteAudio)
    property int    volume: get_opt_value('volume', wallpaper.configuration.Volume)
    property real   speed: get_opt_value('speed', wallpaper.configuration.Speed)
    // User properties for scene wallpapers (JSON string format)
    property string userPropsJson: ""

    // Reactive bindings for configuration changes
    Connections {
        target: wallpaper.configuration
        function onDisplayModeChanged() {
            background.displayMode = background.get_opt_value('display_mode', wallpaper.configuration.DisplayMode);
        }
        function onMuteAudioChanged() {
            background.mute = background.get_opt_value('mute_audio', wallpaper.configuration.MuteAudio);
        }
        function onVolumeChanged() {
            background.volume = background.get_opt_value('volume', wallpaper.configuration.Volume);
        }
        function onSpeedChanged() {
            background.speed = background.get_opt_value('speed', wallpaper.configuration.Speed);
        }
    }

    property int    perOptChanged: wallpaper.configuration.PerOptChanged
    onPerOptChangedChanged: {
        console.log("main.qml onPerOptChangedChanged, perOptChanged:", perOptChanged, "workshopid:", workshopid);
        pyext.read_wallpaper_config(workshopid).then((res) => {
            console.log("main.qml read_wallpaper_config result:", JSON.stringify(res));
            this.curOpt = res;
        });
    }

    // auto pause
    property bool   ok: !windowModel.reqPause && !powerSource.reqPause

    // detect TTY switch and pause wallpaper(s)
    TTYSwitchMonitor {
        id: ttyMonitor
        onTtySwitch: {
            if (sleep) {
                console.log("Preparing for sleep (possibly a VT switch)");
                this.pause();
            } else {
                console.log("Waking up (VT switch back)");
                this.play();
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
            background.setScheduleKey("");
            background.evaluateSchedule();
        })
    }
    // Identifies the schedule step that was last applied. The wallpaper for a
    // step is decided once, when the step begins, and then left alone: what
    // was chosen may depend on what the other monitors were showing at that
    // moment. Kept in the config so a restart does not decide again.
    readonly property string scheduleKey: wallpaper.configuration.ScheduleAppliedKey
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
        if (!conf.enabled || conf.slots.length === 0) return;
        // wait for real sun times rather than acting on the placeholder ones
        if (Sched.usesSun(conf) && !sunSchedule.ready) return;

        const now = Date.now();
        const res = Sched.resolve(conf, now, scheduleSunTimes(), scheduleItemAvailable);
        let wake = res ? res.nextChangeAt : null;

        const hold = scheduleHoldUntil();
        if (hold > now) {
            // a wallpaper picked by hand stays until the next slot fires
            if (wake === null || hold < wake) wake = hold;
        } else {
            if (hold) setScheduleHoldUntil(0);
            // Changes are deferred while the wallpaper is paused (e.g. behind
            // a fullscreen window); okChanged brings us back here.
            if (res && res.item && res.key !== background.scheduleKey && background.ok) {
                setScheduleKey(res.key);
                // a playlist skips ahead past wallpapers other monitors show
                const item = Sched.pickCandidate(res, presence.others(),
                                                 wallpaper.configuration.WallpaperWorkShopId);
                if (wallpaper.configuration.WallpaperWorkShopId !== item.wid ||
                    wallpaper.configuration.WallpaperSource !== item.source) {
                    console.log(`schedule: ${Sched.describeTrigger(res.slot.trigger)} -> ${item.wid}` +
                                (item === res.item ? "" : ` (${res.item.wid} is on another monitor)`));
                    wallpaper.configuration.WallpaperWorkShopId = item.wid;
                    wallpaper.configuration.WallpaperSource = item.source;
                }
                // the other monitors may decide before the binding catches up
                presence.wid = item.wid;
            }
        }

        // Re-check at least once a minute: QML timers do not account for
        // time spent suspended, and the system clock can be changed.
        const delay = wake === null ? 60000 : wake - now + 100;
        scheduleTimer.interval = Math.max(1000, Math.min(60000, delay));
        scheduleTimer.start();
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
            setScheduleKey("");
        } else if (picked) {
            const res = Sched.resolve(background.scheduleConf, Date.now(), scheduleSunTimes(), scheduleItemAvailable);
            setScheduleHoldUntil(res && res.nextSlotAt ? res.nextSlotAt : 0);
        }
        evaluateSchedule();
    }
    Connections {
        target: ttyMonitor
        function onTtySwitch(sleep) { if (!sleep) background.evaluateSchedule(); }
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

    property var mouseHooker
    property bool hasLib: Common.checklib_wallpaper(background)

    property var customConf: Common.loadCustomConf(wallpaper.configuration.CustomConf)

    property string wallpaperPath
    property string wallpaperType

    signal sig_backendFirstFrame(string backname)
    function onBackendFirstFrame(backname) {
        console.error(`backend ${backname} first frame`);
        if (wallpaper.hasOwnProperty('accentColor'))
            wallpaper.accentColorChanged();
    }

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

        if(type_changed || is_infobackend || !source) {
            loadBackend();
        } else if(path_changed) {
            backendLoader.item.source = path;
        }

        sourceCallback();
    }

    function getWorkshopIDPath() {
        return Common.getWorkshopDir(this.steamlibrary) + `/${this.workshopid}`;
    }

    onMouseInputChanged: {
        if(this.mouseInput) {
            hookTimer.start();
        }
        else if(this.mouseHooker) {
            this.mouseHooker.target = null;
            this.mouseHooker.destroy;
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
            backendLoader.item.pause();
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
        onTriggered: background.autoPause();
    }
    // main  
    Item { 
        id: backendLoader
        anchors.fill: parent
        property var item: null

        signal loaded

        Component.onCompleted: {
            if(background.hasLib) {
                this.loaded.connect(this.changeMouseTarget);
                background.mouseHookerChanged.connect(this.changeMouseTarget);
            }
        }
        Component.onDestruction: {
            if(this.item) this.item.destroy();
        }
        function load(url, properties) {
            const com = Qt.createComponent(url);
            if(com.status === Component.Ready) {
                if(this.item) this.item.destroy(100);
                this.item = null;
                try {
                    this.item = com.createObject(this, properties);
                } catch(e) {
                    this.loadInfoShow(e);
                    return;
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
        console.error("load backend: "+qmlsource);
        backendLoader.load(qmlsource, properties);
        sourceCallback();
    }
   
    function autoPause() {
        background.ok
            ? backendLoader.item.play()
            : backendLoader.item.pause();
    }

    Component.onCompleted: {
        // load first backend
        applySource();

        // background signal connect
        background.videoBackendChanged.connect(loadBackend);
        background.okChanged.connect(autoPause);
        background.sourceChanged.connect(applySource);

        lauchPauseTimer.start();

        presence.wid = shownWid;
        migrateLegacySchedule();
        migrateSharedPlaylists();
        scheduleSeenConf = wallpaper.configuration.ScheduleConf;
        scheduleSeenPick = wallpaper.configuration.ScheduleManualPick;
        evaluateSchedule();
    }
}
}
