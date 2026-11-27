### baseline-run1

Commit `249caec` with uncommitted changes, 2026-11-27 12:34. NVIDIA GeForce RTX 5070 Ti, 620.73.04; AMD Ryzen 7 9800X3D 8-Core Processor; kernel 7.3.6-2-cachyos. Rendered at 3840x2160.

| Scene | Cold load (ms) | Warm load (ms) | Teardown (ms) | CPU per frame, 15 fps (ms) | CPU per frame, 240 fps (ms) | GPU use at 240 fps (%) | Frames in 240 fps run |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3593248271 | 306 | 270 | 24.1 | 0.136 | 0.059 | 5.8 | 3701 |
| 3602874264 | 1517 | 1455 | 25.0 | 0.880 | 0.431 | 36.5 | 3700 |
| 3798007377 | 1232 | 1410 | 26.8 | 1.337 | 0.488 | 62.1 | 3701 |
| 1905244619 | 1718 | 1699 | 26.1 | 0.664 | 0.355 | 30.5 | 3701 |
| 2276185076 | 272 | 261 | 23.0 | 0.213 | 0.101 | 16.2 | 3701 |
| **Mean** | **1009** | **1019** | **25.0** | **0.646** | **0.287** | **30.2** | |

Headless create/destroy, 40 cycles: +3.00 file descriptors, +0.12 MB resident memory and +0.00 threads per cycle; 1184 ms per cycle.

Window harness (1766x994 logical, 30 cycles through Qt/OpenGL, exit code 0):

| Measure | Start | End | Per cycle |
| --- | ---: | ---: | ---: |
| GPU memory (MB) | 195 | 1545 | +45.00 |
| File descriptors | 48 | 48 | +0.00 |
| Resident memory (MB) | 523 | 786 | +8.76 |
| Threads | 7 | 7 | +0.00 |

Time to first frame: median 1216 ms (30 of 30 loads reported a first frame). Longest gap between presented frames while a wallpaper loads: median 17 ms, worst 19 ms. While one is destroyed: median 17 ms, worst 29 ms.
