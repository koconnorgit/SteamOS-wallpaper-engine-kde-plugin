import QtQuick 2.6
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.5
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.kirigami 2.4 as Kirigami

import "page"
import "js/schedule.mjs" as Sched

ColumnLayout {
    id: root
    spacing: 5

    // Устанавливаем тему для всех дочерних элементов
    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false

    // Required by Plasma 6
    property var configDialog
    property var wallpaperConfiguration

    property string cfg_SteamLibraryPath
    property string cfg_WallpaperWorkShopId
    property string cfg_WallpaperSource
    property string cfg_FilterStr
    property int    cfg_SortMode

    property alias  cfg_Fps:                 settingPage.cfg_Fps
    property alias  cfg_Volume:              settingPage.cfg_Volume
    property alias  cfg_MpvStats:            settingPage.cfg_MpvStats
    property alias  cfg_Speed:               settingPage.cfg_Speed
    property alias  cfg_MuteAudio:           settingPage.cfg_MuteAudio
    property alias  cfg_MouseInput:          settingPage.cfg_MouseInput
    property alias  cfg_ResumeTime:          settingPage.cfg_ResumeTime
    property alias  cfg_PauseFilterByScreen: settingPage.cfg_PauseFilterByScreen
    property alias  cfg_PauseOnBatPower:     settingPage.cfg_PauseOnBatPower
    property alias  cfg_PauseBatPercent:     settingPage.cfg_PauseBatPercent
    property int    cfg_DisplayMode
    property int    cfg_PauseMode
    property int    cfg_VideoBackend

    property int    cfg_PerOptChanged: 0

    //property alias  cfg_UseMpv
    //property string cfg_BackgroundColor: "black"
    //property alias  cfg_FilterMode: wallpaperPage.cfg_FilterMode

    property string cfg_CustomConf
    property var customConf: {
        customConf = Common.loadCustomConf(cfg_CustomConf);
    }

    property var iconSizes: {
        if(PlasmaCore.Units) {
            iconSizes = PlasmaCore.Units.iconSizes;
        } else {
            iconSizes = {
                large: 48
            }
        }
    }
    // property var themeWidth: {
    //     if(PlasmaCore.Theme && PlasmaCore.Theme.mSize) {
    //         themeWidth = PlasmaCore.Theme.mSize(theme.defaultFont).width;
    //     } else if(theme) {
    //         themeWidth = theme.mSize(theme.defaultFont).width;
    //     } else {
    //         themeWidth = font.pixelSize;
    //     }
    // }

    property var libcheck: ({
        wallpaper: Common.checklib_wallpaper(root),
        qtwebsockets: Common.checklib_websockets(root),
        qtwebchannel: Common.checklib_webchannel(root)
    })


                    
    property var plugin_info: {
        if(!libcheck.wallpaper) {
            plugin_info = {
                version: "-",
                cache_path: null
            }
        } else {
            plugin_info = Qt.createQmlObject(`
                import QtQuick 2.0;
                import com.github.catsout.wallpaperEngineKde 1.2
                PluginInfo {}
            `, this);
        }
    }

    property var pyext: {
        // FileHelper-based Pyext (no Python/WebSocket dependency)
        pyext = Qt.createQmlObject(`
            import QtQuick 2.0;
            Pyext {}
        `, this);
    }

    function saveConfig() {
        wallpaperPage.saveConfig();
    }

    // ---- wallpaper schedule (slots and playlists, see js/schedule.mjs) ----
    property string cfg_ScheduleConf
    // bumped whenever a wallpaper is picked by hand, so the running wallpaper
    // can tell a deliberate pick from a value this dialog merely wrote back
    property string cfg_ScheduleManualPick
    property bool   userPicked: false

    readonly property var schedule: Sched.parse(cfg_ScheduleConf)

    function updateSchedule(change) {
        const conf = Sched.clone(root.schedule);
        change(conf);
        cfg_ScheduleConf = Sched.serialize(conf);
    }
    // schedule entry for a wallpaper from the list model
    function scheduleItem(model) {
        if (!model || !model.workshopid) return null;
        const source = Common.packWallpaperSource(model);
        if (!source) return null;
        return {
            wid: model.workshopid,
            source: source,
            title: model.title || model.workshopid,
            preview: model.preview || ''
        };
    }
    function schedulePreview(item) {
        if (!item || !item.preview) return '';
        const path = Common.unpackWallpaperSource(item.source).path;
        return path.substring(0, path.lastIndexOf('/') + 1) + item.preview;
    }
    function scheduleItemExists(item) {
        try {
            return pyext.exists(Common.wallpaperDirOfSource(item.source));
        } catch (e) {
            return true;
        }
    }
    function playlistHas(playlist, item) {
        return Boolean(item) && playlist.items.some(it => it.wid === item.wid);
    }
    function togglePlaylistItem(playlistId, item) {
        if (!item) return;
        updateSchedule(conf => {
            const pl = Sched.findPlaylist(conf, playlistId);
            if (!pl) return;
            const at = pl.items.findIndex(it => it.wid === item.wid);
            if (at >= 0) pl.items.splice(at, 1);
            else pl.items.push(item);
        });
    }
    function newPlaylist(item) {
        const id = Sched.newId('p');
        updateSchedule(conf => {
            conf.playlists.push({
                id: id,
                name: `Playlist ${conf.playlists.length + 1}`,
                order: 'sequential',
                interval: 30,
                items: item ? [item] : []
            });
        });
        return id;
    }
    function setSlotWallpaper(slotId, item) {
        if (!item) return;
        updateSchedule(conf => {
            const slot = conf.slots.find(s => s.id === slotId);
            if (slot) slot.target = Object.assign({ type: 'wallpaper' }, item);
        });
    }
    function newSlotFor(item) {
        bar.currentIndex = 1;
        schedulePage.openSlotDialog(null, item);
    }

    // Wallpapers the schedule uses, for the badges and the "Scheduled" toggle
    // on the Wallpapers tab:
    //   scheduled  every wallpaper some slot will show (its own wallpaper, or
    //              the members of its playlist)
    //   active     the subset belonging to the slot in effect right now
    //   showing    the one on screen per the schedule
    readonly property var activeSchedule: {
        const res = { scheduled: {}, active: {}, showing: "" };
        const conf = root.schedule;
        if (!conf.enabled) return res;
        const collect = (slot, into) => {
            const target = slot.target;
            if (target.type === 'wallpaper') {
                into[target.wid] = true;
            } else if (target.type === 'playlist') {
                const pl = Sched.findPlaylist(conf, target.id);
                if (pl) pl.items.forEach(it => { into[it.wid] = true; });
            }
        };
        conf.slots.forEach(slot => collect(slot, res.scheduled));
        const cur = schedulePage.current;
        if (cur) {
            collect(cur.slot, res.active);
            if (cur.item) res.showing = cur.item.wid;
        }
        return res;
    }

    // "Scheduled" toggle on the Wallpapers tab: list only those wallpapers.
    // Has no effect while nothing is scheduled.
    property bool cfg_ScheduledOnly
    // a string, so the list is only rebuilt when the set really changes
    readonly property string scheduledIds: Object.keys(activeSchedule.scheduled).sort().join(',')
    readonly property bool scheduledOnlyActive: cfg_ScheduledOnly && scheduledIds !== ""

    // sunrise / sunset times, null while unknown
    property var sunSchedule: null
    readonly property var sunTimes: (sunSchedule && sunSchedule.available && sunSchedule.ready)
        ? ({ sunrises: sunSchedule.sunrises, sunsets: sunSchedule.sunsets })
        : null
    Component.onCompleted: {
        if (!libcheck.wallpaper) return;
        try {
            sunSchedule = Qt.createQmlObject(`
                import QtQuick 2.0;
                import com.github.catsout.wallpaperEngineKde 1.2
                SunSchedule {}
            `, root);
        } catch (e) {
            console.error("SunSchedule is not available; rebuild the plugin library", e);
        }
    }

    // While this dialog is open the schedule may change the wallpaper. Follow
    // it, so that applying other settings does not put the old one back.
    readonly property string liveWallpaperSource: wallpaperConfiguration
        ? String(wallpaperConfiguration.WallpaperSource) : ""
    onLiveWallpaperSourceChanged: {
        if (root.userPicked || !wallpaperConfiguration || !liveWallpaperSource) return;
        cfg_WallpaperWorkShopId = wallpaperConfiguration.WallpaperWorkShopId;
        cfg_WallpaperSource = liveWallpaperSource;
    }

    WallpaperListModel {
        id: wpListModel
        workshopDirs: Common.getProjectDirs(cfg_SteamLibraryPath)
        globalConfigPath: Common.getGlobalConfigPath(cfg_SteamLibraryPath)
        filterStr: cfg_FilterStr
        sortMode: cfg_SortMode
        restrictToIds: root.scheduledOnlyActive
        onlyIds: root.scheduledIds
        initItemOp: (item) => {
            if(!root.customConf) return;
            item.favor = root.customConf.favor.has(item.workshopid);
        }
        enabled: Boolean(cfg_SteamLibraryPath)
        readfile: pyext.readfile
    }

    Component.onDestruction: {
        if(this.pyext) this.pyext.destroy();
    }

    function saveCustomConf() {
        cfg_CustomConf = Common.prepareCustomConf(this.customConf);
    }


    // Content
    PlasmaComponents.TabBar {
        id: bar
        implicitWidth: font.pixelSize*8 * 4
        PlasmaComponents.TabButton {
            text: "Wallpapers"
        }
        PlasmaComponents.TabButton {
            text: "Schedule"
        }
        PlasmaComponents.TabButton {
            text: "Settings"
        }
        PlasmaComponents.TabButton {
            text: "About"
        }
    }

    StackLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        currentIndex: bar.currentIndex

        WallpaperPage {
            id: wallpaperPage
        }

        SchedulePage {
            id: schedulePage
        }

        SettingPage {
            id: settingPage
        }

        AboutPage {}
    }
}
