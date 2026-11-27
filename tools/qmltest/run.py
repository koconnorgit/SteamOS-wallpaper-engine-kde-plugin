#!/usr/bin/env python3
"""Runs the plugin's main.qml outside Plasma and plays config changes into it.

    tools/qmltest/run.py                 # every scenario
    tools/qmltest/run.py crossfade       # one of them
    tools/qmltest/run.py --list

A window opens for the length of each scenario. The script prints what
happened (wallpapers loaded and destroyed, the state of the crossfade, schedule
decisions, QML errors) and checks the basics: the number of wallpapers loaded,
a clean end state with one wallpaper showing, no JavaScript errors, and a few
lines each scenario must or must not produce. It exits non-zero if a check
fails. The finer points in each scenario's notes are still for a person to
read against the output. The harness uses a private config directory, so the real
playlists and per-wallpaper options are not touched.

main.qml is taken from the working tree, the C++ plugin from source/build.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SOURCE = REPO / "source"
STEAM = Path.home() / ".steam/steam"
WORKSHOP = STEAM / "steamapps/workshop/content/431960"

# wallpapers from the Workshop, small ones that load quickly
A, B, C, D, E = "3593248271", "2276185076", "1601099860", "1590580682", "2321186234"


def config_defaults():
    """The config keys and their defaults, from the plugin's main.xml."""
    import xml.etree.ElementTree as ET
    ns = "{http://www.kde.org/standards/kcfg/1.0}"
    values = {}
    for e in ET.parse(SOURCE / "plugin/contents/config/main.xml").getroot().iter(ns + "entry"):
        d = e.find(ns + "default")
        v = d.text if d is not None and d.text else ""
        kind = e.get("type")
        if kind == "Bool":
            v = v == "true"
        elif kind in ("Int", "UInt"):
            v = int(v or 0)
        elif kind == "Double":
            v = float(v or 0)
        values[e.get("name")] = v
    return values


def src(wid):
    return f"file://{WORKSHOP}/{wid}/scene.json+scene"


def pick(wid, source_first=False):
    """The two config keys a wallpaper change writes, in either order."""
    keys = [("WallpaperWorkShopId", wid), ("WallpaperSource", src(wid))]
    return dict(reversed(keys) if source_first else keys)


def scenario_crossfade(tmp):
    bad = tmp / "broken" / "9999999999"
    bad.mkdir(parents=True)
    (bad / "scene.json").write_text("{ this is not json")
    return {
        "notes": [
            "start: one wallpaper loads, no fade (nothing to fade from)",
            "3 s: change to B. Expect a second holder at opacity 0, then ~, then one holder left",
            "7 s: change to C writing the source before the id. Same as above",
            "11 s: change to D and 150 ms later to E. D is dropped unseen, E fades in over C",
            "15 s: change to a scene that cannot load. After 15 s it is given up and E stays",
            "34 s: change to A. The crossfade still works after the failure",
        ],
        "loads": 7,
        "must": [r"never drew a frame, keeping the previous one"],
        "config": {**pick(A)},
        "steps": [
            {"at": 3000, "note": "to B", "set": pick(B)},
            {"at": 7000, "note": "to C, source first", "set": pick(C, source_first=True)},
            {"at": 11000, "note": "to D", "set": pick(D)},
            {"at": 11150, "note": "to E right away", "set": pick(E)},
            {"at": 15000, "note": "to a broken scene",
             "set": {"WallpaperWorkShopId": "9999999999", "WallpaperSource": f"file://{bad}/scene.json+scene"}},
            {"at": 34000, "note": "to A", "set": pick(A)},
            {"at": 38000, "quit": True},
        ],
    }


def scenario_schedule(tmp):
    # a playlist slot that fired at midnight, changing every minute
    playlists = {"version": 1, "playlists": [{"id": "p", "name": "Test", "items": [
        {"wid": w, "source": src(w), "title": w, "preview": ""} for w in (A, B, C, D)]}]}
    (tmp / "config").mkdir(exist_ok=True)
    (tmp / "config" / "wallpaper-engine-kde-playlists.json").write_text(json.dumps(playlists))
    conf = {"version": 1, "enabled": True, "slots": [
        {"id": "s0", "trigger": {"type": "clock", "minutes": 0},
         "target": {"type": "playlist", "id": "p", "order": "sequential", "interval": 1}}]}
    # start 20 s before a minute boundary so the step change comes soon
    wait = (60 - time.localtime().tm_sec - 20) % 60
    time.sleep(wait)
    return {
        "notes": [
            "the saved wallpaper (E) is not what the schedule wants: expect ONE load at start, of the scheduled one",
            "about 20 s in the playlist steps: one 'schedule:' line and one crossfade",
            "30 s: a hand pick (B). The schedule must leave it alone at the next step",
        ],
        "loads": 3,
        "must": [r"schedule: 00:00 -> \d+"],
        "must_not": [rf"loading scene: {E}/"],
        "config": {**pick(E), "ScheduleConf": json.dumps(conf)},
        "steps": [
            {"at": 30000, "note": "hand pick B", "set": {**pick(B), "ScheduleManualPick": "1"}},
            {"at": 95000, "quit": True},
        ],
    }


def scenario_mixed(tmp):
    def other(wid, file, kind):
        return {"WallpaperWorkShopId": wid, "WallpaperSource": f"file://{WORKSHOP}/{wid}/{file}+{kind}"}
    video = other("3349073961", "0001-0240.mp4", "video")
    web = other("1252839488", "index.html", "web")
    return {
        "notes": [
            "a scene, then a video with mpv, a web page, the same video with QtMultimedia, and a scene again",
            "every change should fade: a second holder at 0, then ~, then one holder left",
            "the video must not be revealed before it has a frame (no long gap between 'waiting=false' and '~')",
        ],
        "loads": 2,
        "config": {**pick(A), "VideoBackend": 1},
        "steps": [
            {"at": 3000, "note": "to a video (mpv)", "set": video},
            {"at": 8000, "note": "to a web page", "set": web},
            {"at": 15000, "note": "QtMultimedia for videos", "set": {"VideoBackend": 0}},
            {"at": 15500, "note": "to the video again", "set": video},
            {"at": 21000, "note": "to a scene", "set": pick(B)},
            {"at": 25000, "quit": True},
        ],
    }


def scenario_static(tmp):
    still = "1766198823"   # nothing in it moves, so the renderer stops drawing after its first frames
    return {
        "notes": [
            "to a scene that draws three frames and then stops, and away from it again",
            "it has to fade in like any other, stay up, and fade out",
        ],
        "loads": 3,
        "config": {**pick(A)},
        "steps": [
            {"at": 3000, "note": "to the still scene", "set": pick(still)},
            {"at": 9000, "note": "to B", "set": pick(B)},
            {"at": 13000, "quit": True},
        ],
    }


SCENARIOS = {"crossfade": scenario_crossfade, "schedule": scenario_schedule, "mixed": scenario_mixed,
             "static": scenario_static}

KEEP = re.compile(r"TEST |loading scene|wekde.mpv|Destroy sceneobject|schedule:|never drew|can't load scene|parse scene|"
                  r"TypeError|ReferenceError|Error:|Unable to assign|Cannot |is not a function|is not defined|"
                  r"qml: |Binding loop|not a usable", re.I)
DROP = re.compile(r"read json|unknown tex|not found in render|_alias_lightCookie|looper|vulkan device|cache folder|"
                  r"pkg version|new buffer block|gl: |swapchain image|receive external|muted, not init|"
                  r"Reloading scene|source: /", re.I)


def run(name):
    lib = SOURCE / "build/src/libWallpaperEngineKde.so"
    qml = shutil.which("qml6") or shutil.which("qml")
    if not lib.exists() or not qml:
        sys.exit("needs the plugin built in source/build and qml6 on PATH")
    with tempfile.TemporaryDirectory(prefix="wek-qmltest-") as t:
        tmp = Path(t)
        mod = tmp / "imports/com/github/catsout/wallpaperEngineKde"
        mod.mkdir(parents=True)
        os.symlink(lib, mod / lib.name)
        shutil.copy(SOURCE / "src/qmldir", mod / "qmldir")
        sc = SCENARIOS[name](tmp)
        config = config_defaults()
        config.update({"SteamLibraryPath": f"file://{STEAM}", "MouseInput": False, "PauseMode": 0})
        config.update(sc["config"])
        spec = {"main": str(SOURCE / "plugin/contents/ui/main.qml"), "config": config, "steps": sc["steps"],
                "width": 1280, "height": 720}
        env = dict(os.environ, MANGOHUD="0", QT_FORCE_STDERR_LOGGING="1", XDG_CONFIG_HOME=str(tmp / "config"),
                   XDG_CACHE_HOME=str(tmp / "cache"))
        (tmp / "config").mkdir(exist_ok=True)
        print(f"== {name}")
        for n in sc["notes"]:
            print(f"   expect  {n}")
        p = subprocess.Popen([qml, "-I", str(HERE / "imports"), "-I", str(tmp / "imports"), str(HERE / "harness.qml"),
                              "--", json.dumps(spec)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        t0 = time.time()
        everything = []
        try:
            for line in p.stderr:
                line = line.rstrip()
                everything.append(line)
                if DROP.search(line) or not KEEP.search(line):
                    continue
                stamp = "" if "TEST " in line else f"[{(time.time() - t0) * 1000:6.0f}] "
                print("   " + stamp + re.sub(str(WORKSHOP) + "/", "", line)[:200])
        finally:
            p.wait(timeout=30)
        print(f"   exit {p.returncode}")
        return check(sc, everything, p.returncode)


JS_ERRORS = re.compile(r"TypeError|ReferenceError|SyntaxError|is not a function|is not defined|Cannot read property|"
                       r"Cannot call method|Unable to assign \[undefined\] to (?!QRect)")


def check(sc, lines, code):
    """The checks every scenario has to pass. Returns the failures as text."""
    failed = []
    if code != 0:
        failed.append(f"exit code {code}")
    loads = sum(1 for l in lines if "loading scene:" in l)
    if "loads" in sc and loads != sc["loads"]:
        failed.append(f"{loads} scenes loaded, expected {sc['loads']}")
    states = [l for l in lines if "holders=[" in l]
    if not states or "holders=[1] waiting=false item=yes old=no" not in states[-1]:
        failed.append(f"did not end with one wallpaper showing: {states[-1][-60:] if states else 'no state seen'}")
    errors = [l for l in lines if JS_ERRORS.search(l)]
    if errors:
        failed.append(f"{len(errors)} JavaScript errors, first: {errors[0][:160]}")
    for pattern in sc.get("must", []):
        if not any(re.search(pattern, l) for l in lines):
            failed.append(f"nothing matched /{pattern}/")
    for pattern in sc.get("must_not", []):
        hit = [l for l in lines if re.search(pattern, l)]
        if hit:
            failed.append(f"/{pattern}/ matched: {hit[0][:120]}")
    for f in failed:
        print(f"   FAILED  {f}")
    if not failed:
        print("   passed")
    return failed


def run_dialog():
    """Opens the settings page for a few seconds and prints what its wallpaper list loaded."""
    lib = SOURCE / "build/src/libWallpaperEngineKde.so"
    qml = shutil.which("qml6") or shutil.which("qml")
    with tempfile.TemporaryDirectory(prefix="wek-qmltest-") as t:
        tmp = Path(t)
        mod = tmp / "imports/com/github/catsout/wallpaperEngineKde"
        mod.mkdir(parents=True)
        os.symlink(lib, mod / lib.name)
        shutil.copy(SOURCE / "src/qmldir", mod / "qmldir")
        values = config_defaults()
        values.update({"SteamLibraryPath": f"file://{STEAM}", **pick(A)})
        # the page takes each config key as a cfg_ property
        skip = {"ScheduleHoldUntil", "ScheduleAppliedKey", "DayNightSwitch", "DayWallpaperWorkShopId",
                "DayWallpaperSource", "NightWallpaperWorkShopId", "NightWallpaperSource", "BackgroundColor"}
        cfg = {f"cfg_{k}": v for k, v in values.items() if k not in skip}
        spec = {"config": str(SOURCE / "plugin/contents/ui/config.qml"), "values": values, "cfg": cfg, "seconds": 8}
        env = dict(os.environ, QT_FORCE_STDERR_LOGGING="1", XDG_CONFIG_HOME=str(tmp / "config"),
                   XDG_CACHE_HOME=str(tmp / "cache"))
        (tmp / "config").mkdir()
        print("== dialog")
        print("   expect  the wallpaper list fills in (a count well above 0) and no QML errors")
        p = subprocess.Popen([qml, "-I", str(tmp / "imports"), str(HERE / "dialog.qml"), "--", json.dumps(spec)],
                             env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        for line in p.stderr:
            if re.search(r"TEST |Error|rror:|is not|Unable|Cannot|undefined|Binding loop", line):
                print("   " + line.rstrip()[:220])
        p.wait(timeout=30)
        print(f"   exit {p.returncode}")


def screen_locked():
    """True while the session is locked. Qt stops driving a window's timers when it is not drawn."""
    try:
        out = subprocess.run(["qdbus6", "org.freedesktop.ScreenSaver", "/ScreenSaver",
                              "org.freedesktop.ScreenSaver.GetActive"], capture_output=True, text=True, timeout=5).stdout
        return out.strip() == "true"
    except Exception:
        return False


def main():
    args = sys.argv[1:]
    if screen_locked():
        sys.exit("the screen is locked: the test window would not be drawn and the run would stall")
    if "dialog" in args:
        run_dialog()
        args = [a for a in args if a != "dialog"]
        if not args:
            return
    if "--list" in args:
        print("\n".join(SCENARIOS))
        return
    failed = 0
    for name in args or list(SCENARIOS):
        if name not in SCENARIOS:
            sys.exit(f"no scenario {name}; have {', '.join(SCENARIOS)}")
        failed += 1 if run(name) else 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
