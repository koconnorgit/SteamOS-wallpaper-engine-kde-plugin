### final-run1

Commit `92cbf51`, 2026-11-27 16:09. NVIDIA GeForce RTX 5070 Ti, 620.73.04; AMD Ryzen 7 9800X3D 8-Core Processor; kernel 7.3.6-2-cachyos. Rendered at 3840x2160.

| Scene | Cold load (ms) | Warm load (ms) | Teardown (ms) | CPU per frame, 15 fps (ms) | CPU per frame, 240 fps (ms) | GPU use at 240 fps (%) | Frames in 240 fps run |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3593248271 | 326 | 265 | 26.7 | 0.133 | 0.060 | 7.9 | 3702 |
| 3602874264 | 1068 | 948 | 27.8 | 0.885 | 0.406 | 34.1 | 3701 |
| 3798007377 | 479 | 377 | 30.1 | 0.964 | 0.494 | 46.6 | 3701 |
| 1905244619 | 247 | 223 | 31.0 | 0.532 | 0.328 | 9.0 | 3701 |
| 2276185076 | 285 | 238 | 28.7 | 0.199 | 0.109 | 13.6 | 3700 |
| **Mean** | **481** | **410** | **28.9** | **0.543** | **0.279** | **22.3** | |

Headless create/destroy, 40 cycles: +0.00 file descriptors, +0.03 MB resident memory and +0.00 threads per cycle; 635 ms per cycle.

Window harness (1766x994 logical, 30 cycles through Qt/OpenGL, exit code 0):

| Measure | Start | End | Per cycle |
| --- | ---: | ---: | ---: |
| GPU memory (MB) | 60 | 60 | +0.00 |
| File descriptors | 48 | 48 | +0.00 |
| Resident memory (MB) | 545 | 759 | +7.13 |
| Threads | 8 | 8 | +0.00 |

Time to first frame: median 258 ms (30 of 30 loads reported a first frame). Longest gap between presented frames while a wallpaper loads: median 16 ms, worst 18 ms. While one is destroyed: median 17 ms, worst 18 ms.
