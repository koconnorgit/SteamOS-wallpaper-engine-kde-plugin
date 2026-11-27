### baseline-run2

Commit `249caec` with uncommitted changes, 2026-11-27 12:40. NVIDIA GeForce RTX 5070 Ti, 620.73.04; AMD Ryzen 7 9800X3D 8-Core Processor; kernel 7.3.6-2-cachyos. Rendered at 3840x2160.

| Scene | Cold load (ms) | Warm load (ms) | Teardown (ms) | CPU per frame, 15 fps (ms) | CPU per frame, 240 fps (ms) | GPU use at 240 fps (%) | Frames in 240 fps run |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3593248271 | 318 | 268 | 23.8 | 0.129 | 0.056 | 6.4 | 3700 |
| 3602874264 | 1515 | 1447 | 25.1 | 0.863 | 0.446 | 37.1 | 3701 |
| 3798007377 | 1458 | 1410 | 27.5 | 1.001 | 0.489 | 62.0 | 3701 |
| 1905244619 | 1728 | 1705 | 25.8 | 0.637 | 0.332 | 29.8 | 3701 |
| 2276185076 | 275 | 260 | 24.2 | 0.183 | 0.102 | 15.6 | 3701 |
| **Mean** | **1059** | **1018** | **25.3** | **0.563** | **0.285** | **30.2** | |

Headless create/destroy, 40 cycles: +3.00 file descriptors, +0.31 MB resident memory and +0.00 threads per cycle; 1169 ms per cycle.

Window harness (1766x994 logical, 30 cycles through Qt/OpenGL, exit code 0):

| Measure | Start | End | Per cycle |
| --- | ---: | ---: | ---: |
| GPU memory (MB) | 195 | 1545 | +45.00 |
| File descriptors | 48 | 48 | +0.00 |
| Resident memory (MB) | 563 | 771 | +6.93 |
| Threads | 7 | 7 | +0.00 |

Time to first frame: median 1183 ms (30 of 30 loads reported a first frame). Longest gap between presented frames while a wallpaper loads: median 17 ms, worst 18 ms. While one is destroyed: median 20 ms, worst 31 ms.
