### final-run2

Commit `92cbf51`, 2026-11-27 16:13. NVIDIA GeForce RTX 5070 Ti, 620.73.04; AMD Ryzen 7 9800X3D 8-Core Processor; kernel 7.3.6-2-cachyos. Rendered at 3840x2160.

| Scene | Cold load (ms) | Warm load (ms) | Teardown (ms) | CPU per frame, 15 fps (ms) | CPU per frame, 240 fps (ms) | GPU use at 240 fps (%) | Frames in 240 fps run |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3593248271 | 337 | 260 | 26.8 | 0.137 | 0.061 | 8.5 | 3702 |
| 3602874264 | 1075 | 948 | 28.1 | 1.168 | 0.425 | 38.6 | 3701 |
| 3798007377 | 460 | 381 | 31.4 | 0.950 | 0.494 | 49.5 | 3702 |
| 1905244619 | 247 | 218 | 28.7 | 0.512 | 0.337 | 12.6 | 3702 |
| 2276185076 | 273 | 236 | 28.8 | 0.206 | 0.108 | 11.1 | 3701 |
| **Mean** | **478** | **409** | **28.8** | **0.595** | **0.285** | **24.1** | |

Headless create/destroy, 40 cycles: +0.00 file descriptors, +0.04 MB resident memory and +0.00 threads per cycle; 636 ms per cycle.

Window harness (1766x994 logical, 30 cycles through Qt/OpenGL, exit code 0):

| Measure | Start | End | Per cycle |
| --- | ---: | ---: | ---: |
| GPU memory (MB) | 60 | 60 | +0.00 |
| File descriptors | 48 | 48 | +0.00 |
| Resident memory (MB) | 539 | 730 | +6.35 |
| Threads | 8 | 8 | +0.00 |

Time to first frame: median 258 ms (30 of 30 loads reported a first frame). Longest gap between presented frames while a wallpaper loads: median 17 ms, worst 17 ms. While one is destroyed: median 17 ms, worst 18 ms.
