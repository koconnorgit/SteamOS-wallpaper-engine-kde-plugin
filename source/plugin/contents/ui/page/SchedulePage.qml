import QtQuick 2.6
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.5

import ".."
import "../js/schedule.mjs" as Sched

import org.kde.kirigami 2.6 as Kirigami

// Schedule tab: slots (when to change) and playlists (what to rotate through).
// All state lives in root.schedule (config.qml); edits go through
// root.updateSchedule() and take effect when the dialog is applied.
Flickable {
    id: schedulePage

    Kirigami.Theme.inherit: true

    Layout.fillWidth: true
    ScrollBar.vertical: ScrollBar { id: scrollbar }

    contentWidth: width - (scrollbar.visible ? scrollbar.width : 0)
    contentHeight: content.implicitHeight + Kirigami.Units.largeSpacing * 2
    clip: true
    boundsBehavior: Flickable.OvershootBounds

    readonly property var conf: root.schedule
    readonly property var sun: root.sunTimes
    property real now: Date.now()
    // what the schedule shows right now (ignores manual picks and missing files)
    readonly property var current: Sched.resolve(conf, now, sun)
    readonly property real holdUntil: root.wallpaperConfiguration
        ? (Number(root.wallpaperConfiguration.ScheduleHoldUntil) || 0) : 0

    Timer {
        interval: 20000
        running: schedulePage.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: schedulePage.now = Date.now()
    }

    function formatTime(ms) {
        return new Date(ms).toLocaleTimeString(Qt.locale(), Locale.ShortFormat);
    }
    function formatMinutes(minutes) {
        const d = new Date();
        d.setHours(Math.floor(minutes / 60), minutes % 60, 0, 0);
        return formatTime(d.getTime());
    }
    function triggerText(trigger) {
        return trigger.type === 'clock' ? formatMinutes(trigger.minutes) : Sched.describeTrigger(trigger);
    }
    function playlistSummary(pl) {
        const n = pl.items.length;
        const count = n === 1 ? "1 wallpaper" : `${n} wallpapers`;
        if (n < 2) return count;
        return `${count} · ${pl.order === 'shuffle' ? 'shuffled' : 'in order'} · every ${Sched.formatDuration(pl.interval)}`;
    }
    function todaySunText() {
        if (!sun) return "";
        const sunrise = Sched.todayMinutes({ type: 'sun', event: 'sunrise', offset: 0 }, now, sun);
        const sunset = Sched.todayMinutes({ type: 'sun', event: 'sunset', offset: 0 }, now, sun);
        if (sunrise === null || sunset === null) return "";
        return `Today: sunrise ${formatMinutes(sunrise)}, sunset ${formatMinutes(sunset)}`;
    }
    function statusText() {
        if (!conf.enabled) return "The schedule is off.";
        if (conf.slots.length === 0) return "Add a slot to get started.";
        if (holdUntil > now)
            return `Keeping the wallpaper you picked by hand until ${formatTime(holdUntil)}.`;
        if (!current) return "No slot can fire yet.";
        const what = current.item ? current.item.title : "nothing assigned";
        let text = `Now: ${triggerText(current.slot.trigger)} → ${what}`;
        if (current.playlist) text += ` (${current.playlist.name})`;
        if (current.nextChangeAt) text += `. Next change at ${formatTime(current.nextChangeAt)}.`;
        return text;
    }

    function openSlotDialog(slot, wallpaper) {
        slotDialog.openFor(slot, wallpaper);
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        // ---- on/off and status ----
        RowLayout {
            Layout.fillWidth: true
            Switch {
                id: enableSwitch
                text: "Change wallpapers on a schedule"
                checked: schedulePage.conf.enabled
                onToggled: {
                    const on = checked;
                    root.updateSchedule(conf => { conf.enabled = on; });
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                text: schedulePage.todaySunText()
                color: Kirigami.Theme.disabledTextColor
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: schedulePage.statusText()
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            visible: Sched.usesSun(schedulePage.conf) && !schedulePage.sun
            color: Kirigami.Theme.negativeTextColor
            text: "Sunrise and sunset times are not available, so those slots are skipped. " +
                  "They need Plasma 6.5 or newer and the plugin library built from this version."
        }

        // ---- slots ----
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            Kirigami.Heading {
                level: 3
                text: "Slots"
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "Add slot"
                icon.name: "list-add-symbolic"
                onClicked: schedulePage.openSlotDialog(null, null)
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: "Each slot changes the wallpaper at a time of day, or relative to sunrise or sunset, " +
                  "and stays in effect until the next slot."
        }

        Repeater {
            model: Sched.sortedSlots(schedulePage.conf, schedulePage.now, schedulePage.sun)

            Control {
                id: slotRow
                Layout.fillWidth: true
                padding: Kirigami.Units.smallSpacing

                readonly property var slot: modelData.slot
                readonly property var target: slot.target
                readonly property var playlist: target.type === 'playlist'
                    ? Sched.findPlaylist(schedulePage.conf, target.id) : null
                readonly property bool active: schedulePage.conf.enabled && schedulePage.current
                    && schedulePage.current.slot.id === slot.id

                background: Rectangle {
                    color: slotRow.active ? Kirigami.Theme.activeBackgroundColor : Kirigami.Theme.backgroundColor
                    radius: 4
                }
                contentItem: RowLayout {
                    spacing: Kirigami.Units.largeSpacing

                    ColumnLayout {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 11
                        Layout.maximumWidth: Kirigami.Units.gridUnit * 11
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            text: schedulePage.triggerText(slotRow.slot.trigger)
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: slotRow.slot.trigger.type === 'sun'
                            color: Kirigami.Theme.disabledTextColor
                            text: modelData.minutes === null
                                ? "time unknown"
                                : `≈ ${schedulePage.formatMinutes(modelData.minutes)} today`
                        }
                    }

                    Image {
                        Layout.preferredWidth: 64
                        Layout.preferredHeight: 36
                        visible: slotRow.target.type === 'wallpaper' && status === Image.Ready
                        source: slotRow.target.type === 'wallpaper' ? root.schedulePreview(slotRow.target) : ''
                        sourceSize.width: 128
                        sourceSize.height: 72
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    Kirigami.Icon {
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                        visible: slotRow.target.type === 'playlist'
                        source: "view-media-playlist"
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: (slotRow.target.type === 'none' || (slotRow.target.type === 'playlist' && !slotRow.playlist))
                                ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                            text: {
                                if (slotRow.target.type === 'wallpaper') return slotRow.target.title;
                                if (slotRow.target.type === 'playlist')
                                    return slotRow.playlist ? slotRow.playlist.name : "Playlist no longer exists";
                                return "Nothing assigned yet";
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            visible: Boolean(slotRow.playlist)
                            elide: Text.ElideRight
                            color: Kirigami.Theme.disabledTextColor
                            text: slotRow.playlist ? schedulePage.playlistSummary(slotRow.playlist) : ""
                        }
                    }

                    ToolButton {
                        icon.name: "document-edit-symbolic"
                        ToolTip.text: "Edit slot"
                        ToolTip.visible: hovered
                        onClicked: schedulePage.openSlotDialog(slotRow.slot, null)
                    }
                    ToolButton {
                        icon.name: "edit-delete-symbolic"
                        ToolTip.text: "Remove slot"
                        ToolTip.visible: hovered
                        onClicked: {
                            const id = slotRow.slot.id;
                            root.updateSchedule(conf => { conf.slots = conf.slots.filter(s => s.id !== id); });
                        }
                    }
                }
            }
        }

        // ---- playlists ----
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            Kirigami.Heading {
                level: 3
                text: "Playlists"
            }
            Item { Layout.fillWidth: true }
            Button {
                text: "New playlist"
                icon.name: "list-add-symbolic"
                onClicked: {
                    const id = root.newPlaylist(null);
                    playlistDialog.openFor(Sched.findPlaylist(root.schedule, id));
                }
            }
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: "A playlist rotates through its wallpapers while its slot is in effect. " +
                  "Add wallpapers from the Wallpapers tab with the calendar button on each card."
        }

        Repeater {
            model: schedulePage.conf.playlists

            Control {
                id: playlistBox
                Layout.fillWidth: true
                padding: Kirigami.Units.smallSpacing

                readonly property var playlist: modelData

                background: Rectangle {
                    color: Kirigami.Theme.backgroundColor
                    radius: 4
                }
                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    RowLayout {
                        Layout.fillWidth: true
                        Kirigami.Icon {
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            source: "view-media-playlist"
                        }
                        Label {
                            text: playlistBox.playlist.name
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: Kirigami.Theme.disabledTextColor
                            text: schedulePage.playlistSummary(playlistBox.playlist)
                        }
                        ToolButton {
                            icon.name: "list-add-symbolic"
                            readonly property var selected: wallpaperPage.selectedScheduleItem()
                            enabled: Boolean(selected) && !root.playlistHas(playlistBox.playlist, selected)
                            ToolTip.text: selected
                                ? `Add "${selected.title}" (selected in the Wallpapers tab)`
                                : "Add the wallpaper selected in the Wallpapers tab"
                            ToolTip.visible: hovered
                            onClicked: root.togglePlaylistItem(playlistBox.playlist.id, selected)
                        }
                        ToolButton {
                            icon.name: "document-edit-symbolic"
                            ToolTip.text: "Rename, order and interval"
                            ToolTip.visible: hovered
                            onClicked: playlistDialog.openFor(playlistBox.playlist)
                        }
                        ToolButton {
                            icon.name: "edit-delete-symbolic"
                            ToolTip.text: "Delete playlist"
                            ToolTip.visible: hovered
                            onClicked: {
                                const id = playlistBox.playlist.id;
                                root.updateSchedule(conf => {
                                    conf.playlists = conf.playlists.filter(p => p.id !== id);
                                    conf.slots.forEach(s => {
                                        if (s.target.type === 'playlist' && s.target.id === id)
                                            s.target = { type: 'none' };
                                    });
                                });
                            }
                        }
                    }

                    Label {
                        visible: playlistBox.playlist.items.length === 0
                        color: Kirigami.Theme.disabledTextColor
                        text: "Empty"
                    }

                    Repeater {
                        model: playlistBox.playlist.items

                        RowLayout {
                            id: itemRow
                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.largeSpacing
                            spacing: Kirigami.Units.smallSpacing

                            readonly property bool exists: root.scheduleItemExists(modelData)

                            function move(delta) {
                                const plId = playlistBox.playlist.id;
                                const from = index, to = index + delta;
                                root.updateSchedule(conf => {
                                    const items = Sched.findPlaylist(conf, plId).items;
                                    if (to < 0 || to >= items.length) return;
                                    items.splice(to, 0, items.splice(from, 1)[0]);
                                });
                            }

                            Label {
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 1.5
                                horizontalAlignment: Text.AlignRight
                                color: Kirigami.Theme.disabledTextColor
                                text: index + 1
                            }
                            Image {
                                Layout.preferredWidth: 64
                                Layout.preferredHeight: 36
                                source: root.schedulePreview(modelData)
                                sourceSize.width: 128
                                sourceSize.height: 72
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                color: itemRow.exists ? Kirigami.Theme.textColor : Kirigami.Theme.negativeTextColor
                                text: itemRow.exists ? modelData.title : `${modelData.title} (missing, will be skipped)`
                            }
                            ToolButton {
                                icon.name: "arrow-up-symbolic"
                                enabled: index > 0
                                onClicked: itemRow.move(-1)
                            }
                            ToolButton {
                                icon.name: "arrow-down-symbolic"
                                enabled: index < playlistBox.playlist.items.length - 1
                                onClicked: itemRow.move(1)
                            }
                            ToolButton {
                                icon.name: "list-remove-symbolic"
                                ToolTip.text: "Remove from playlist"
                                ToolTip.visible: hovered
                                onClicked: root.togglePlaylistItem(playlistBox.playlist.id, modelData)
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- slot editor ----
    Dialog {
        id: slotDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: slotId ? "Edit slot" : "Add slot"
        standardButtons: Dialog.Ok | Dialog.Cancel

        property string slotId: ""
        // entries: { text, target }
        property var targets: []

        function openFor(slot, wallpaper) {
            slotId = slot ? slot.id : "";
            const trigger = slot ? slot.trigger : { type: 'clock', minutes: 12 * 60 };

            whenBox.currentIndex = trigger.type === 'clock' ? 0 : (trigger.event === 'sunrise' ? 1 : 2);
            if (trigger.type === 'clock') {
                hourSpin.value = Math.floor(trigger.minutes / 60);
                minuteSpin.value = trigger.minutes % 60;
                relationBox.currentIndex = 0;
                offsetHourSpin.value = 0;
                offsetMinuteSpin.value = 0;
            } else {
                relationBox.currentIndex = trigger.offset === 0 ? 0 : (trigger.offset < 0 ? 1 : 2);
                offsetHourSpin.value = Math.floor(Math.abs(trigger.offset) / 60);
                offsetMinuteSpin.value = Math.abs(trigger.offset) % 60;
            }

            // what the slot can show: the wallpaper passed in, the one it
            // already has, the one selected in the Wallpapers tab, any playlist
            const list = [];
            const seen = new Set();
            const addWallpaper = (item, note) => {
                if (!item || seen.has(item.wid)) return;
                seen.add(item.wid);
                list.push({
                    text: `Wallpaper: ${item.title}${note}`,
                    target: Object.assign({ type: 'wallpaper' }, item)
                });
            };
            addWallpaper(wallpaper, "");
            if (slot && slot.target.type === 'wallpaper') addWallpaper(slot.target, "");
            addWallpaper(wallpaperPage.selectedScheduleItem(), " (selected in Wallpapers tab)");
            schedulePage.conf.playlists.forEach(pl => {
                list.push({ text: `Playlist: ${pl.name}`, target: { type: 'playlist', id: pl.id } });
            });
            list.push({ text: "Nothing yet", target: { type: 'none' } });
            targets = list;

            let at = 0;
            if (!wallpaper && slot && slot.target.type === 'playlist')
                at = list.findIndex(e => e.target.type === 'playlist' && e.target.id === slot.target.id);
            else if (!wallpaper && slot && slot.target.type === 'none')
                at = list.length - 1;
            targetBox.currentIndex = Math.max(0, at);
            open();
        }

        function trigger() {
            if (whenBox.currentIndex === 0)
                return { type: 'clock', minutes: hourSpin.value * 60 + minuteSpin.value };
            const amount = offsetHourSpin.value * 60 + offsetMinuteSpin.value;
            const sign = relationBox.currentIndex === 1 ? -1 : (relationBox.currentIndex === 2 ? 1 : 0);
            return {
                type: 'sun',
                event: whenBox.currentIndex === 1 ? 'sunrise' : 'sunset',
                offset: sign * amount
            };
        }

        onAccepted: {
            const id = slotId;
            const trig = trigger();
            const target = targets[targetBox.currentIndex].target;
            root.updateSchedule(conf => {
                const existing = conf.slots.find(s => s.id === id);
                if (existing) {
                    existing.trigger = trig;
                    existing.target = target;
                } else {
                    // the first slot turns the schedule on
                    if (conf.slots.length === 0) conf.enabled = true;
                    conf.slots.push({ id: Sched.newId('s'), trigger: trig, target: target });
                }
            });
        }

        contentItem: GridLayout {
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.smallSpacing

            Label { text: "Change at" }
            ComboBox {
                id: whenBox
                Layout.fillWidth: true
                model: ["A time of day", "Sunrise", "Sunset"]
            }

            Label { text: "Time"; visible: whenBox.currentIndex === 0 }
            RowLayout {
                visible: whenBox.currentIndex === 0
                SpinBox {
                    id: hourSpin
                    from: 0; to: 23
                    textFromValue: (value) => Sched.pad2(value)
                }
                Label { text: ":" }
                SpinBox {
                    id: minuteSpin
                    from: 0; to: 59
                    textFromValue: (value) => Sched.pad2(value)
                }
                Label {
                    color: Kirigami.Theme.disabledTextColor
                    text: `(${schedulePage.formatMinutes(hourSpin.value * 60 + minuteSpin.value)})`
                }
            }

            Label { text: "Offset"; visible: whenBox.currentIndex !== 0 }
            RowLayout {
                visible: whenBox.currentIndex !== 0
                ComboBox {
                    id: relationBox
                    model: ["Exactly then", "Before", "After"]
                }
                SpinBox {
                    id: offsetHourSpin
                    visible: relationBox.currentIndex !== 0
                    from: 0; to: 11
                }
                Label { text: "h"; visible: relationBox.currentIndex !== 0 }
                SpinBox {
                    id: offsetMinuteSpin
                    visible: relationBox.currentIndex !== 0
                    from: 0; to: 59
                    stepSize: 5
                }
                Label { text: "min"; visible: relationBox.currentIndex !== 0 }
            }

            Label { text: "Show" }
            ComboBox {
                id: targetBox
                Layout.fillWidth: true
                Layout.minimumWidth: Kirigami.Units.gridUnit * 18
                model: slotDialog.targets
                textRole: "text"
            }

            Item { width: 1; height: 1 }
            Label {
                Layout.fillWidth: true
                Layout.maximumWidth: Kirigami.Units.gridUnit * 24
                wrapMode: Text.Wrap
                color: Kirigami.Theme.disabledTextColor
                text: "To use a different wallpaper, use the calendar button on its card in the Wallpapers tab."
            }
        }
    }

    // ---- playlist settings ----
    Dialog {
        id: playlistDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: "Playlist"
        standardButtons: Dialog.Ok | Dialog.Cancel

        property string playlistId: ""

        function openFor(playlist) {
            if (!playlist) return;
            playlistId = playlist.id;
            nameField.text = playlist.name;
            orderBox.currentIndex = playlist.order === 'shuffle' ? 1 : 0;
            intervalHourSpin.value = Math.floor(playlist.interval / 60);
            intervalMinuteSpin.value = playlist.interval % 60;
            open();
            nameField.forceActiveFocus();
            nameField.selectAll();
        }

        onAccepted: {
            const id = playlistId;
            const name = nameField.text.trim();
            const order = orderBox.currentIndex === 1 ? 'shuffle' : 'sequential';
            const interval = Math.max(1, intervalHourSpin.value * 60 + intervalMinuteSpin.value);
            root.updateSchedule(conf => {
                const pl = Sched.findPlaylist(conf, id);
                if (!pl) return;
                if (name) pl.name = name;
                pl.order = order;
                pl.interval = interval;
            });
        }

        contentItem: GridLayout {
            columns: 2
            columnSpacing: Kirigami.Units.largeSpacing
            rowSpacing: Kirigami.Units.smallSpacing

            Label { text: "Name" }
            TextField {
                id: nameField
                Layout.fillWidth: true
                Layout.minimumWidth: Kirigami.Units.gridUnit * 14
                onAccepted: playlistDialog.accept()
            }

            Label { text: "Order" }
            ComboBox {
                id: orderBox
                Layout.fillWidth: true
                model: ["In order", "Shuffle"]
            }

            Label { text: "Change every" }
            RowLayout {
                SpinBox {
                    id: intervalHourSpin
                    from: 0; to: 23
                }
                Label { text: "h" }
                SpinBox {
                    id: intervalMinuteSpin
                    from: 0; to: 59
                }
                Label { text: "min" }
            }
        }
    }
}
