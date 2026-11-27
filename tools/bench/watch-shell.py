#!/usr/bin/env python3
"""Samples the running plasmashell's memory over time, to tell a leak from noise.

    tools/bench/watch-shell.py out.tsv                  # 12 hours, once a minute
    tools/bench/watch-shell.py out.tsv --hours 2 --every 30
    tools/bench/watch-shell.py out.tsv --not-pid 1234   # wait for a shell other than 1234
    tools/bench/watch-shell.py --summary out.tsv        # read a finished (or running) log

"Resident memory" as a single number mostly tracks the GPU driver's mappings and
shared library pages. A leak would show in anonymous memory (the heap and
private mappings), so the categories are logged separately:

    anon_mb     private anonymous memory, RssAnon; this is the one to watch
    heap_mb     the part of it that is the main heap
    file_mb     pages of mapped files, mostly shared libraries
    shmem_mb    shared memory, mostly buffers the GPU driver maps in
    nvidia_mb   resident pages of /dev/nvidiactl mappings
    vram_mb     GPU memory, from nvidia-smi
    fds, threads
    loads       scene wallpapers loaded since the shell started (from the journal)

Only reads /proc, nvidia-smi and the journal. Stops when the shell exits.
"""
import argparse
import os
import subprocess
import sys
import time


def shell_pid():
    try:
        return int(subprocess.run(["pgrep", "-x", "plasmashell"], capture_output=True, text=True).stdout.split()[0])
    except (IndexError, ValueError):
        return None


def sample(pid):
    s = {"anon_mb": 0, "file_mb": 0, "shmem_mb": 0, "heap_mb": 0, "nvidia_mb": 0, "vram_mb": -1}
    for line in open(f"/proc/{pid}/status"):
        for key, name in (("RssAnon:", "anon_mb"), ("RssFile:", "file_mb"), ("RssShmem:", "shmem_mb")):
            if line.startswith(key):
                s[name] = int(line.split()[1]) // 1024
    name = ""
    heap = nvidia = 0
    for line in open(f"/proc/{pid}/smaps"):
        head = line.split(None, 1)[0]
        if "-" in head and not head.endswith(":"):
            parts = line.split()
            name = parts[5] if len(parts) > 5 else ""
        elif line.startswith("Rss:"):
            kb = int(line.split()[1])
            if name == "[heap]":
                heap += kb
            elif name == "/dev/nvidiactl":
                nvidia += kb
    s["heap_mb"], s["nvidia_mb"] = heap // 1024, nvidia // 1024
    s["fds"] = len(os.listdir(f"/proc/{pid}/fd"))
    s["threads"] = len(os.listdir(f"/proc/{pid}/task"))
    try:
        for line in subprocess.run(["nvidia-smi", "pmon", "-c", "1", "-s", "m"], capture_output=True, text=True,
                                   timeout=20).stdout.splitlines():
            f = line.split()
            if len(f) > 3 and f[1] == str(pid) and f[3].isdigit():
                s["vram_mb"] = int(f[3])
    except Exception:
        pass
    try:
        out = subprocess.run(["journalctl", "--user", "-b", f"_PID={pid}", "--no-pager", "-g", "loading scene", "-o", "cat"],
                             capture_output=True, text=True, timeout=60).stdout
        s["loads"] = sum(1 for l in out.splitlines() if "loading scene" in l)
    except Exception:
        s["loads"] = -1
    return s


COLUMNS = ["anon_mb", "heap_mb", "file_mb", "shmem_mb", "nvidia_mb", "vram_mb", "fds", "threads", "loads"]


def summary(path):
    rows = []
    for line in open(path):
        f = line.rstrip("\n").split("\t")
        if f[0] == "time" or len(f) < len(COLUMNS) + 2:
            continue
        rows.append((float(f[1]), {c: int(v) for c, v in zip(COLUMNS, f[2:])}))
    if len(rows) < 4:
        print("not enough samples yet")
        return
    hours = (rows[-1][0] - rows[0][0]) / 3600
    print(f"{len(rows)} samples over {hours:.1f} hours; wallpapers loaded {rows[0][1]['loads']} -> {rows[-1][1]['loads']}")
    quarter = max(1, len(rows) // 4)
    print(f"{'':12}{'first':>8}{'last':>8}{'min':>8}{'max':>8}{'mean, first quarter':>22}{'mean, last quarter':>21}{'MB per hour':>13}")
    for c in COLUMNS[:-1]:
        vals = [r[1][c] for r in rows]
        a = sum(vals[:quarter]) / quarter
        b = sum(vals[-quarter:]) / quarter
        # slope between the quarter means, over the time between their middles
        span = (rows[-quarter // 2 - 1][0] - rows[quarter // 2][0]) / 3600 or 1
        print(f"{c:12}{vals[0]:8d}{vals[-1]:8d}{min(vals):8d}{max(vals):8d}{a:22.0f}{b:21.0f}{(b - a) / span:13.1f}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("--hours", type=float, default=12)
    ap.add_argument("--every", type=int, default=60, help="seconds between samples")
    ap.add_argument("--not-pid", type=int, help="wait until plasmashell is a process other than this one")
    ap.add_argument("--summary", action="store_true")
    args = ap.parse_args()
    if args.summary:
        summary(args.out)
        return

    pid = shell_pid()
    waited = 0
    while pid is None or pid == args.not_pid:
        time.sleep(5)
        waited += 5
        if waited > 6 * 3600:
            sys.exit("no new plasmashell appeared")
        pid = shell_pid()
    if args.not_pid:
        time.sleep(60)   # let a freshly started shell finish loading

    end = time.time() + args.hours * 3600
    with open(args.out, "w") as f:
        f.write("\t".join(["time", "epoch"] + COLUMNS) + f"\n# plasmashell pid {pid}\n")
        while time.time() < end and os.path.isdir(f"/proc/{pid}"):
            try:
                s = sample(pid)
            except (FileNotFoundError, ProcessLookupError):
                break
            f.write("\t".join([time.strftime("%m-%d %H:%M:%S"), str(int(time.time()))] + [str(s[c]) for c in COLUMNS]) + "\n")
            f.flush()
            time.sleep(args.every)


if __name__ == "__main__":
    main()
