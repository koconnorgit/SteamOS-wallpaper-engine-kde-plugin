

# Fork additions

This fork (koconnorgit) adds a **wallpaper schedule**: each monitor can change its wallpaper at set times of day or relative to sunrise and sunset, and rotate through playlists.

- **Slots** say when to change. A slot fires at a clock time (13:00) or at sunrise/sunset with an optional offset (1 h after sunset, 30 min before sunrise), and stays in effect until the next slot fires.
- A slot shows either **one wallpaper** or a **playlist**. A playlist is just a list of wallpapers; each slot that uses one sets how often to change and whether to go in order or shuffled, for as long as that slot is in effect.
- Slots are **per monitor**; set them up in each screen's wallpaper settings. **Playlists are shared**: build one once and use it from any monitor's slots. Each monitor still rotates through it on its own, counted from when its own slot fired. A playlist never puts up a wallpaper another monitor is already showing; it skips ahead to the next free one. Playlists are stored in `~/.config/wallpaper-engine-kde-playlists.json`.

Where things are:

- **Schedule tab**: turn the schedule on, add and edit slots, create and reorder playlists.
- **Wallpapers tab**: the calendar button on each card (and in the detail panel) adds that wallpaper to a playlist or assigns it to a slot.
- **Settings tab**: Fade Duration sets the length of the crossfade, per monitor.

Behaviour worth knowing:

- Sunrise and sunset come from Plasma's day/night cycle (the location or custom times set under System Settings → Display & Monitor → Night Light). This needs Plasma 6.5 or newer; on older versions clock-time slots still work.
- Picking a wallpaper by hand while the schedule is on keeps it until the next slot fires. Editing the schedule resumes it immediately.
- A wallpaper paused behind a fullscreen or maximized window (per the Pause setting) is not changed while paused; it catches up when it resumes.
- Wallpapers that have been unsubscribed are skipped.
- The earlier Day / Night switch is converted automatically into a sunrise slot and a sunset slot. The Randomize timer has been removed; use a shuffled playlist instead.

It also adds a **crossfade between wallpapers**: whenever the wallpaper changes, by schedule or by hand, the new one fades in over the old one instead of cutting. The fade is 500 ms by default; set Fade Duration to 0 to switch at once. Both wallpapers run for the length of the fade, so their audio can overlap briefly.

Tests for the schedule logic: `node --test source/tests/schedule.test.mjs`

## Build & install on Arch (CachyOS, Manjaro, EndeavourOS)

```bash
git clone https://github.com/koconnorgit/SteamOS-wallpaper-engine-kde-plugin.git
cd SteamOS-wallpaper-engine-kde-plugin
./install-arch.sh
```

The script installs build dependencies, builds from source, installs the C++ library system-wide, registers the wallpaper package with `kpackagetool6`, and restarts plasmashell.

The script uses `pacman`, so it only works on Arch-based distributions. Wallpaper Engine itself has to be installed through Steam first; scenes need its `assets` folder.

## After installation

1. Right-click the desktop → **Configure Desktop and Wallpaper**.
2. Change the wallpaper type from **Image** to **Wallpaper Engine**.
3. Point it to your Steam library folder (the one containing `steamapps`):
   - common: `~/.local/share/Steam`
   - Steam Deck: `~/.steam/steam`
   - Flatpak: `~/.var/app/com.valvesoftware.Steam/data/Steam`

If scenes that used to work start crashing Plasma, try the **win 7 compatibility** beta of Wallpaper Engine (Steam → Library → right-click Wallpaper Engine → Properties → Betas).

## If Plasma crashes on start

Open a terminal (Ctrl+Alt+T, or a console login) and remove the plugin from the desktop config, then log out:

```bash
sed -i '/wallpaperEngineKde/d' ~/.config/plasma-org.kde.plasma.desktop-appletsrc
qdbus6 org.kde.Shutdown /Shutdown logout
```

## Uninstall

Switch each desktop to another wallpaper type first, then:

```bash
kpackagetool6 -t Plasma/Wallpaper -r com.github.catsout.wallpaperEngineKde
sudo rm -r /usr/lib/qt6/qml/com/github/catsout/wallpaperEngineKde \
           /usr/share/plasma/wallpapers/com.github.catsout.wallpaperEngineKde
```

## Credits

Based on [catsout/wallpaper-engine-kde-plugin](https://github.com/catsout/wallpaper-engine-kde-plugin), by way of [slynobody/SteamOS-wallpaper-engine-kde-plugin](https://github.com/slynobody/SteamOS-wallpaper-engine-kde-plugin), which also offers prebuilt packages of the original plugin for SteamOS and Debian (without this fork's additions).

Use at your own risk.
