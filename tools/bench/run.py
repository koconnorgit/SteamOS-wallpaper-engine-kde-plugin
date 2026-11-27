#!/usr/bin/env python3
"""Performance benchmark for the scene backend. See docs/benchmark.md.

    tools/bench/run.py <label>            # writes docs/benchmarks/<label>.json and .md
    tools/bench/run.py <label> --quick    # short run, to check the harness works

Runs two harnesses against the working tree:

  headless   source/build-bench/scenebench: load time, teardown time, CPU and GPU
             cost per frame, and what a create/destroy cycle leaves behind.
  window     tools/bench/cycle.qml with the plugin from source/build: the same
             cycle through the Qt/OpenGL path, for GPU memory, fds and frame gaps.
             Opens a window for about a minute.

Close games and other GPU-heavy programs first; the numbers are only comparable
between runs made under similar conditions.
"""
import argparse
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SOURCE = REPO / "source"
STEAM = Path.home() / ".steam/steam/steamapps"
WORKSHOP = STEAM / "workshop/content/431960"
ASSETS = STEAM / "common/wallpaper_engine/assets"

# A fixed set from the Workshop: (id, why it is in the set).
SCENES = [
    ("3593248271", "PNG-container texture, glitter effect with a mask"),
    ("3602874264", "28 textures, caustics effect"),
    ("3798007377", "particles, cloud motion effect"),
    ("1905244619", "older scene from the Xmas playlist"),
    ("2276185076", "older scene from the Xmas playlist"),
]


def sh(cmd, **kw):
    return subprocess.run(cmd, check=True, text=True, capture_output=True, **kw).stdout.strip()


def scene_path(wid):
    for name in ("scene.pkg", "scene.json"):
        p = WORKSHOP / wid / name
        if p.exists():
            return str(p)
    return None


def scene_title(wid):
    try:
        return json.loads((WORKSHOP / wid / "project.json").read_text(errors="replace")).get("title", "")
    except Exception:
        return ""


def environment(res):
    def first(cmd, default="unknown"):
        try:
            return sh(cmd).splitlines()[0]
        except Exception:
            return default

    cpu = "unknown"
    for line in Path("/proc/cpuinfo").read_text().splitlines():
        if line.startswith("model name"):
            cpu = line.split(":", 1)[1].strip()
            break
    return {
        "commit": first(["git", "-C", str(REPO), "rev-parse", "--short", "HEAD"]),
        "dirty": bool(first(["git", "-C", str(REPO), "status", "--porcelain", "--", "source"], "")),
        "date": time.strftime("%Y-%m-%d %H:%M"),
        "gpu": first(["nvidia-smi", "--query-gpu=name,driver_version", "--format=csv,noheader"]),
        "cpu": cpu,
        "kernel": platform.release(),
        "resolution": res,
    }


def build():
    bench = SOURCE / "build-bench"
    if not (bench / "build.ninja").exists():
        subprocess.run(["cmake", "-B", str(bench), "-S", str(SOURCE / "src/backend_scene/standalone_view"),
                        "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-G", "Ninja"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", str(bench), "--target", "scenebench"], check=True, stdout=subprocess.DEVNULL)
    plugin = SOURCE / "build"
    if (plugin / "build.ninja").exists():
        subprocess.run(["cmake", "--build", str(plugin)], check=True, stdout=subprocess.DEVNULL)
    return bench / "scenebench", plugin / "src"


def bench_env():
    env = dict(os.environ)
    env["MANGOHUD"] = "0"
    # without this Qt sends console.log to the journal when stderr is a pipe
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    # the driver's own shader cache would make the first load depend on earlier runs
    env["__GL_SHADER_DISK_CACHE"] = "0"
    return env


def run_headless(exe, scenes, res, quick, tmp):
    out = tmp / "headless.json"
    cache = tmp / "shader-cache"
    cache.mkdir()
    cmd = [str(exe), "--cache", str(cache), "--res", res, "--out", str(out)]
    cmd += ["--seconds", "3", "--warm", "2", "--cycles", "6"] if quick else ["--seconds", "15", "--warm", "7", "--cycles", "40"]
    cmd += [str(ASSETS)] + scenes
    with open(tmp / "headless.log", "w") as log:
        subprocess.run(cmd, check=True, env=bench_env(), stdout=log, stderr=subprocess.STDOUT)
    return json.loads(out.read_text())


def sample(pid):
    proc = Path("/proc") / str(pid)
    s = {"fds": len(os.listdir(proc / "fd")), "threads": len(os.listdir(proc / "task")), "rss_kb": -1, "vram_mb": -1}
    for line in (proc / "status").read_text().splitlines():
        if line.startswith("VmRSS:"):
            s["rss_kb"] = int(line.split()[1])
    try:
        for line in sh(["nvidia-smi", "pmon", "-c", "1", "-s", "m"]).splitlines():
            f = line.split()
            if len(f) > 3 and f[1] == str(pid) and f[3].isdigit():
                s["vram_mb"] = int(f[3])
    except Exception:
        pass
    return s


def run_window(plugin_dir, scenes, quick, tmp):
    lib = plugin_dir / "libWallpaperEngineKde.so"
    qml = shutil.which("qml6") or shutil.which("qml")
    if not lib.exists() or not qml:
        return {"skipped": "needs the plugin built in source/build and qml6 on PATH"}
    # QML import layout: <dir>/com/github/catsout/wallpaperEngineKde/{qmldir,lib}
    mod = tmp / "imports/com/github/catsout/wallpaperEngineKde"
    mod.mkdir(parents=True)
    os.symlink(lib, mod / lib.name)
    shutil.copy(SOURCE / "src/qmldir", mod / "qmldir")
    conf = {"assets": str(ASSETS), "scenes": scenes, "cycles": 6 if quick else 30, "warmup": 3,
            "width": 1766, "height": 994}
    p = subprocess.Popen([qml, "-I", str(tmp / "imports"), str(Path(__file__).with_name("cycle.qml")), "--", json.dumps(conf)],
                         env=bench_env(), stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    res = {"cycles": conf["cycles"], "window": f'{conf["width"]}x{conf["height"]}', "samples": {}, "load_ms": [],
           "load_gap_ms": [], "drop_gap_ms": []}
    timer = threading.Timer(600, p.kill)
    timer.start()
    with open(tmp / "window.log", "w") as log:
        for line in p.stderr:
            log.write(line)
            m = re.search(r"BENCH (\w+) (.*)", line)
            if not m:
                continue
            kind, rest = m.group(1), m.group(2).split()
            if kind == "SAMPLE":
                time.sleep(1.5)  # let the last teardown finish
                res["samples"][rest[0]] = sample(p.pid)
            elif kind == "LOAD":
                if float(rest[1]) >= 0:
                    res["load_ms"].append(float(rest[1]))
                res["load_gap_ms"].append(float(rest[2]))
            elif kind == "DROP":
                res["drop_gap_ms"].append(float(rest[1]))
    p.wait()
    timer.cancel()
    res["exit"] = p.returncode
    return res


def med(v):
    return statistics.median(v) if v else float("nan")


def report(label, env, scene_ids, headless, window):
    L = []
    w = L.append
    w(f"### {label}")
    w("")
    w(f"Commit `{env['commit']}`{' with uncommitted changes' if env['dirty'] else ''}, {env['date']}. "
      f"{env['gpu']}; {env['cpu']}; kernel {env['kernel']}. Rendered at {env['resolution']}.")
    w("")
    w("| Scene | Cold load (ms) | Warm load (ms) | Teardown (ms) | CPU per frame, 15 fps (ms) | CPU per frame, 240 fps (ms) | GPU use at 240 fps (%) | Frames in 240 fps run |")
    w("| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    tot = {"cold": [], "warm": [], "down": [], "c15": [], "c240": [], "gpu": []}
    for wid, s in zip(scene_ids, headless["scenes"]):
        if not s["ok"]:
            w(f"| {wid} | failed to load | | | | | | |")
            continue
        st = {x["fps"]: x for x in s["steady"]}
        c15 = st[15]["cpu_ms"] / max(st[15]["frames"], 1)
        c240 = st[240]["cpu_ms"] / max(st[240]["frames"], 1)
        gpu = st[240]["gpu_util"]
        for k, v in (("cold", s["load_cold_ms"]), ("warm", s["load_warm_ms"]), ("down", s["teardown_ms"]),
                     ("c15", c15), ("c240", c240), ("gpu", gpu)):
            tot[k].append(v)
        w(f"| {wid} | {s['load_cold_ms']:.0f} | {s['load_warm_ms']:.0f} | {s['teardown_ms']:.1f} | {c15:.3f} | {c240:.3f} | "
          f"{gpu:.1f} | {st[240]['frames']} |")
    if tot["cold"]:
        m = {k: statistics.mean(v) for k, v in tot.items()}
        w(f"| **Mean** | **{m['cold']:.0f}** | **{m['warm']:.0f}** | **{m['down']:.1f}** | **{m['c15']:.3f}** | **{m['c240']:.3f}** | "
          f"**{m['gpu']:.1f}** | |")
    w("")
    c = headless["cycles"]
    n = c["count"]
    w(f"Headless create/destroy, {n} cycles: {(c['fds_after'] - c['fds_before']) / n:+.2f} file descriptors, "
      f"{(c['rss_kb_after'] - c['rss_kb_before']) / n / 1024:+.2f} MB resident memory and "
      f"{(c['threads_after'] - c['threads_before']) / n:+.2f} threads per cycle; {c['total_ms'] / n:.0f} ms per cycle.")
    w("")
    if "skipped" in window:
        w(f"Window harness skipped: {window['skipped']}.")
    else:
        a, b = window["samples"].get("start"), window["samples"].get("end")
        n = window["cycles"]
        w(f"Window harness ({window['window']} logical, {n} cycles through Qt/OpenGL, exit code {window['exit']}):")
        w("")
        w("| Measure | Start | End | Per cycle |")
        w("| --- | ---: | ---: | ---: |")
        if a and b:
            for key, name, div in (("vram_mb", "GPU memory (MB)", 1), ("fds", "File descriptors", 1),
                                   ("rss_kb", "Resident memory (MB)", 1024), ("threads", "Threads", 1)):
                w(f"| {name} | {a[key] / div:.0f} | {b[key] / div:.0f} | {(b[key] - a[key]) / div / n:+.2f} |")
        w("")
        w(f"Time to first frame: median {med(window['load_ms']):.0f} ms ({len(window['load_ms'])} of {n} loads reported a first frame). "
          f"Longest gap between presented frames while a wallpaper loads: median {med(window['load_gap_ms']):.0f} ms, "
          f"worst {max(window['load_gap_ms'], default=float('nan')):.0f} ms. "
          f"While one is destroyed: median {med(window['drop_gap_ms']):.0f} ms, worst {max(window['drop_gap_ms'], default=float('nan')):.0f} ms.")
    w("")
    return "\n".join(L)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("label")
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--res", default="3840x2160")
    ap.add_argument("--no-window", action="store_true", help="skip the window harness")
    ap.add_argument("--scenes", nargs="*", help="workshop ids to use instead of the built-in set")
    args = ap.parse_args()

    ids = args.scenes or [wid for wid, _ in SCENES]
    missing = [wid for wid in ids if not scene_path(wid)]
    if missing or not ASSETS.is_dir():
        sys.exit(f"missing scenes {missing} or assets folder {ASSETS}")
    scenes = [scene_path(wid) for wid in ids]

    exe, plugin_dir = build()
    env = environment(args.res)
    with tempfile.TemporaryDirectory(prefix="wek-bench-") as t:
        tmp = Path(t)
        print("headless harness ...", flush=True)
        headless = run_headless(exe, scenes, args.res, args.quick, tmp)
        window = {"skipped": "--no-window"} if args.no_window else None
        if window is None:
            print("window harness ...", flush=True)
            window = run_window(plugin_dir, scenes, args.quick, tmp)
        logs = SOURCE / "build-bench/logs"
        logs.mkdir(parents=True, exist_ok=True)
        for name in ("headless.log", "window.log"):
            if (tmp / name).exists():
                shutil.copy(tmp / name, logs / f"{args.label}.{name}")
    for wid, s in zip(ids, headless["scenes"]):
        s["scene"] = wid
    out = REPO / "docs/benchmarks"
    out.mkdir(parents=True, exist_ok=True)
    data = {"label": args.label, "environment": env,
            "scenes": [{"id": wid, "title": scene_title(wid)} for wid in ids], "headless": headless, "window": window}
    (out / f"{args.label}.json").write_text(json.dumps(data, indent=1) + "\n")
    md = report(args.label, env, ids, headless, window)
    (out / f"{args.label}.md").write_text(md)
    print(md)


if __name__ == "__main__":
    main()
