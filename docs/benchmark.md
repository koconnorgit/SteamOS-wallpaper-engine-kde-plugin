# Performance benchmark

A repeatable measurement of the scene backend, taken before the work in [code-review.md](code-review.md) and to be taken again once it is done. It is an indicator that the fixes helped, not a target: correctness comes first, and several numbers below can only move if a specific review item is fixed.

## Running it

```bash
tools/bench/run.py <label>           # about 6 minutes; writes docs/benchmarks/<label>.json and .md
tools/bench/run.py <label> --quick   # about 1 minute, to check the harness still works
```

It builds what it needs (`source/build-bench` for the headless program, and `source/build` for the plugin if that directory is configured). A window opens for about a minute during the second half. Close games and other GPU-heavy programs first. Logs go to `source/build-bench/logs/`.

Requirements: an NVIDIA GPU with `nvidia-smi` (for GPU use and GPU memory; the other numbers work without it), `qml6`, Wallpaper Engine installed through Steam, and the five Workshop items below.

## What is measured

Two harnesses run against the working tree.

**Headless** (`source/src/backend_scene/standalone_view/scenebench.cpp`). Drives `SceneWallpaper` through the offscreen path the plugin uses, at 3840×2160, with no window and no Qt.

| Measure | How | Review items it should reflect |
| --- | --- | --- |
| Cold load | Construct to first frame, with an empty shader cache directory and the driver's shader disk cache off | E8, E9, E10, E11, E12, E13 |
| Warm load | Same, median of 9 further loads | E9, E10, E11, E12, E13 |
| Teardown | Time to destroy the wallpaper, median of 9 | E1 |
| CPU per frame | Process CPU time over 15 s divided by frames rendered, at 15 fps (the plugin default) and at 240 fps | E4, E6, E7 |
| GPU use at 240 fps | Mean of `nvidia-smi pmon` samples for the process over the same 15 s | E4, E5, E6, E7 |
| Left behind per cycle | File descriptors, threads and resident memory before and after 40 create/destroy cycles | A3 |

**Window** (`tools/bench/cycle.qml`). Creates and destroys a `SceneViewer` 30 times in a 1766×994 window (2560×1441 pixels at this desktop's 1.45 scale) using the built plugin, so the Qt/OpenGL side runs as it does in plasmashell.

| Measure | How | Review items it should reflect |
| --- | --- | --- |
| GPU memory, fds, resident memory, threads | Sampled for the process after 3 warm-up cycles and again after 30 more | A1, A2, A3 |
| Time to first frame | Item creation to the `firstFrame` signal | as for load above |
| Frame gap on load and on destroy | Longest interval between presented frames while a wallpaper is created, and in the 300 ms after one is destroyed | E1, E2 |

Scenes, chosen to cover different content:

| Workshop id | Title | Why |
| --- | --- | --- |
| 3593248271 | Christmas glowing | PNG-container texture, glitter effect with a mask |
| 3602874264 | relaxing cosmic spring | 28 textures, caustics effect |
| 3798007377 | Autumn Vibes & Chill Lofi Beats 4K 21/9 | particles, cloud motion effect |
| 1905244619 | christmas | older scene from the Xmas playlist |
| 2276185076 | Christmas eve | older scene from the Xmas playlist |

## Limits of the method

- **Frame rate is capped.** The renderer never draws faster than the requested rate, and the rate tops out near 247 fps, so "frames rendered" says nothing about headroom. Cost per frame is read from CPU time and GPU use instead.
- **GPU use is coarse.** `nvidia-smi` reports whole percent, once a second, 14 samples per scene. Differences under about 2 points are noise.
- **CPU per frame at 15 fps is noisy** (see spread below); the 240 fps figure is steadier.
- **Frame gaps are quantised** to the monitor's refresh interval (about 17 ms on the 60 Hz monitor the window opened on). 17 ms means no frame was missed.
- **The schedule, playlists, crossfade and settings dialog are not measured.** Nothing here covers sections S, R, B or Q, most of H, or E2, E3 and E14–E19. The test suite covers those, not this benchmark.
- **The screen has to stay unlocked.** The window harness stalls while its window is not being drawn.
- **The desktop keeps running.** Three live wallpapers, the compositor and a browser share the GPU during a run.
- **It is one machine.** RTX 5070 Ti, driver 620.73.04, Ryzen 7 9800X3D, kernel 7.3.6.

## Baseline, 27 November 2026

Commit `249caec` plus the benchmark harness itself (hence "uncommitted changes" in the raw reports). Two full runs back to back; raw data in [benchmarks/](benchmarks/) as `baseline-run1` and `baseline-run2`. The table is the mean of the two.

| Scene | Cold load (ms) | Warm load (ms) | Teardown (ms) | CPU per frame, 15 fps (ms) | CPU per frame, 240 fps (ms) | GPU use at 240 fps (%) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 3593248271 | 312 | 269 | 24.0 | 0.133 | 0.058 | 6.1 |
| 3602874264 | 1516 | 1451 | 25.1 | 0.872 | 0.439 | 36.8 |
| 3798007377 | 1345 | 1410 | 27.2 | 1.169 | 0.489 | 62.1 |
| 1905244619 | 1723 | 1702 | 26.0 | 0.651 | 0.344 | 30.2 |
| 2276185076 | 274 | 261 | 23.6 | 0.198 | 0.102 | 15.9 |
| **Mean** | **1034** | **1019** | **25.2** | **0.605** | **0.286** | **30.2** |

Create/destroy cycles:

| Measure | Run 1 | Run 2 |
| --- | ---: | ---: |
| Headless: file descriptors per cycle | +3.00 | +3.00 |
| Headless: resident memory per cycle (MB) | +0.12 | +0.31 |
| Headless: threads per cycle | 0 | 0 |
| Headless: time per cycle (ms) | 1184 | 1169 |
| Window: GPU memory per cycle (MB) | +45.00 | +45.00 |
| Window: GPU memory, start → end of 30 cycles (MB) | 195 → 1545 | 195 → 1545 |
| Window: resident memory per cycle (MB) | +8.76 | +6.93 |
| Window: file descriptors per cycle | 0 | 0 |
| Window: threads per cycle | 0 | 0 |
| Window: time to first frame, median (ms) | 1216 | 1183 |
| Window: frame gap on load, median / worst (ms) | 17 / 19 | 17 / 18 |
| Window: frame gap on destroy, median / worst (ms) | 17 / 29 | 20 / 31 |

### What the baseline shows

- **GPU memory leaks at exactly three frame images per wallpaper.** 45 MB per cycle is 3 × 2560 × 1441 × 4 bytes. This is A1, measured. On the 4K monitor the same leak is about 100 MB per switch. All 1350 MB came back when the process exited, so in plasmashell it lasts until the shell restarts.
- **Three file descriptors leak per wallpaper whose images are never imported** (A3). The window harness imports them, so its count stays flat. The double close in A2 does not show up in a count.
- **Resident memory grows 7–9 MB per cycle in the window harness** and well under 1 MB headless. The difference points at the Qt/OpenGL side, most likely the same imported memory; no review item is tied to it yet, so it is worth re-reading after A1 is fixed.
- **No thread leaks** in either harness.
- **Load time is 0.26–1.7 s and the shader cache barely matters.** Cold and warm loads are within a few percent, so compiling shaders is not what takes the time. The three slow scenes are the ones with many or large textures, which points at texture decode and upload (E9, E10).
- **Teardown takes about 25 ms headless.** In the window harness destroying a wallpaper costs roughly one missed frame at 60 Hz in the worst case (29–31 ms gap); loading does not drop frames.
- **Steady-state cost is small in CPU terms** (0.06–0.5 ms per frame at 240 fps) and scene-dependent on the GPU: 6–62 % at 240 fps, which scales to roughly 0.4–4 % at the default 15 fps for one 4K wallpaper.

### Run-to-run spread

Between the two runs: warm load, teardown, CPU per frame at 240 fps and GPU use moved by 2 % or less on the means; cold load by 5 % (one scene by 18 %); CPU per frame at 15 fps by 14 %. The leak figures were identical. For the comparison, treat a change smaller than about 5 % (15 % for the two noisy columns) as no change.

## After the fixes

To be filled in from `tools/bench/run.py after-run1` and `after-run2`, with the same scene set and settings.

| Measure | Baseline | After | Change |
| --- | ---: | ---: | ---: |
| Cold load, mean (ms) | 1034 | | |
| Warm load, mean (ms) | 1019 | | |
| Teardown, mean (ms) | 25.2 | | |
| CPU per frame at 15 fps, mean (ms) | 0.605 | | |
| CPU per frame at 240 fps, mean (ms) | 0.286 | | |
| GPU use at 240 fps, mean (%) | 30.2 | | |
| Headless: file descriptors per cycle | +3.00 | | |
| Window: GPU memory per cycle (MB) | +45.00 | | |
| Window: resident memory per cycle (MB) | +7.85 | | |
| Window: time to first frame, median (ms) | 1200 | | |
| Window: frame gap on destroy, worst (ms) | 30 | | |
