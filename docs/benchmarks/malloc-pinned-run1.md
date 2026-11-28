### malloc-pinned-run1

Commit `7b0d98d` with uncommitted changes, 2026-11-28 05:55. NVIDIA GeForce RTX 5070 Ti, 620.73.04; AMD Ryzen 7 9800X3D 8-Core Processor; kernel 7.3.6-3-cachyos. Rendered at 3840x2160.

| Scene | Cold load (ms) | Warm load (ms) | Teardown (ms) | CPU per frame, 15 fps (ms) | CPU per frame, 240 fps (ms) | GPU use at 240 fps (%) | Frames in 240 fps run |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3593248271 | 324 | 256 | 24.4 | 0.149 | 0.068 | 3.4 | 3702 |
| 3602874264 | 1113 | 948 | 26.4 | 0.906 | 0.490 | 32.2 | 3701 |
| 3798007377 | 450 | 371 | 28.1 | 1.133 | 0.589 | 40.0 | 3701 |
| 1905244619 | 249 | 216 | 26.5 | 0.557 | 0.396 | 4.6 | 3701 |
| 2276185076 | 276 | 235 | 25.8 | 0.217 | 0.124 | 14.6 | 3701 |
| **Mean** | **482** | **405** | **26.2** | **0.592** | **0.333** | **19.0** | |

Headless create/destroy, 40 cycles: +0.00 file descriptors, +0.02 MB resident memory and +0.00 threads per cycle; 634 ms per cycle.

Window harness (1766x994 logical, 30 cycles through Qt/OpenGL, exit code 0):

| Measure | Start | End | Per cycle |
| --- | ---: | ---: | ---: |
| GPU memory (MB) | 60 | 60 | +0.00 |
| File descriptors | 47 | 47 | +0.00 |
| Resident memory (MB) | 365 | 372 | +0.23 |
| Threads | 8 | 8 | +0.00 |

Time to first frame: median 290 ms (30 of 30 loads reported a first frame). Longest gap between presented frames while a wallpaper loads: median 17 ms, worst 20 ms. While one is destroyed: median 17 ms, worst 20 ms.
