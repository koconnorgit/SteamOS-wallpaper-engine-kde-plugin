#!/usr/bin/env python3
"""Feeds damaged copies of real wallpapers to the scene renderer.

    tools/fuzz/run.py                 # the default set, about 5 minutes
    tools/fuzz/run.py --count 8       # fewer copies per wallpaper
    tools/fuzz/run.py 1919443106      # only this workshop id

Each wallpaper's scene.pkg is copied a number of times with different damage
(see mutate.py) and every copy is loaded with scenebench --load-only. A copy
may load or fail to load; either is fine. What must not happen is a crash or
a hang, and the script exits non-zero if one does, naming the copies.

Runs headless. Needs about 1 GB in the temp directory.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SOURCE = REPO / "source"
STEAM = Path.home() / ".steam/steam/steamapps"
WORKSHOP = STEAM / "workshop/content/431960"
ASSETS = STEAM / "common/wallpaper_engine/assets"

# (workshop id, copies): effects and a PNG texture; particles and sound; particles; models
DEFAULT = [("3593248271", 32), ("2265114283", 32), ("1919443106", 40), ("3602874264", 8)]


def load(exe, cache, scenes, timeout):
    """Returns None when every scene was got through, otherwise what went wrong."""
    env = dict(os.environ, MANGOHUD="0")
    try:
        p = subprocess.run([str(exe), "--load-only", "--res", "1280x720", "--cache", str(cache), str(ASSETS)] + scenes,
                           env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "hang"
    if p.returncode != 0:
        return f"signal {-p.returncode}" if p.returncode < 0 else f"exit {p.returncode}"
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ids", nargs="*", help="workshop ids (default: a built-in set)")
    ap.add_argument("--count", type=int, help="copies per wallpaper")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()

    bench = SOURCE / "build-bench"
    if not (bench / "build.ninja").exists():
        subprocess.run(["cmake", "-B", str(bench), "-S", str(SOURCE / "src/backend_scene/standalone_view"),
                        "-DCMAKE_BUILD_TYPE=RelWithDebInfo", "-G", "Ninja"], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", str(bench), "--target", "scenebench"], check=True, stdout=subprocess.DEVNULL)
    exe = bench / "scenebench"

    todo = [(wid, args.count or 24) for wid in args.ids] if args.ids else [(w, args.count or n) for w, n in DEFAULT]
    bad = []
    total = 0
    with tempfile.TemporaryDirectory(prefix="wek-fuzz-") as t:
        tmp = Path(t)
        (tmp / "cache").mkdir()
        for i, (wid, count) in enumerate(todo):
            pkg = WORKSHOP / wid / "scene.pkg"
            if not pkg.exists():
                sys.exit(f"{pkg} not found")
            out = tmp / wid
            subprocess.run([sys.executable, str(HERE / "mutate.py"), str(pkg), str(out), str(count), str(args.seed + i)],
                           check=True, stdout=subprocess.DEVNULL)
            scenes = sorted(str(p) for p in out.glob("*/scene.pkg"))
            total += len(scenes)
            # a load that fails waits out scenebench's 20 s limit
            problem = load(exe, tmp / "cache", scenes, 25 * len(scenes) + 60)
            if problem:
                # find out which copies: one process each
                for s in scenes:
                    one = load(exe, tmp / "cache", [s], 60)
                    if one:
                        bad.append((wid, Path(s).parent.name, one))
                if not any(b[0] == wid for b in bad):
                    bad.append((wid, "(only when run together)", problem))
            print(f"{wid}: {len(scenes)} copies, {sum(1 for b in bad if b[0] == wid)} crashed or hung", flush=True)
            for p in out.glob("*/scene.pkg"):
                p.unlink()
    for wid, name, what in bad:
        print(f"  FAILED {wid} copy {name}: {what}")
    print(f"{total} damaged wallpapers, {len(bad)} crashed or hung")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
