### after-run1

Commit `3ef3699`, 2026-11-27 14:46. NVIDIA GeForce RTX 5070 Ti, 620.73.04; AMD Ryzen 7 9800X3D 8-Core Processor; kernel 7.3.6-2-cachyos. Rendered at 3840x2160.

| Scene | Cold load (ms) | Warm load (ms) | Teardown (ms) | CPU per frame, 15 fps (ms) | CPU per frame, 240 fps (ms) | GPU use at 240 fps (%) | Frames in 240 fps run |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3593248271 | 318 | 253 | 25.8 | 0.126 | 0.058 | 4.4 | 3701 |
| 3602874264 | 1072 | 953 | 28.5 | 1.083 | 0.407 | 29.4 | 3701 |
| 3798007377 | 466 | 374 | 30.7 | 1.204 | 0.503 | 27.1 | 3701 |
| 1905244619 | 237 | 213 | 29.0 | 0.497 | 0.327 | 8.5 | 3701 |
| 2276185076 | 269 | 227 | 26.6 | 0.193 | 0.105 | 8.4 | 3701 |
| **Mean** | **472** | **404** | **28.1** | **0.621** | **0.280** | **15.6** | |

Headless create/destroy, 40 cycles: +0.00 file descriptors, +0.06 MB resident memory and +0.00 threads per cycle; 638 ms per cycle.

Window harness (1766x994 logical, 30 cycles through Qt/OpenGL, exit code 0):

| Measure | Start | End | Per cycle |
| --- | ---: | ---: | ---: |
| GPU memory (MB) | 60 | 60 | +0.00 |
| File descriptors | 48 | 48 | +0.00 |
| Resident memory (MB) | 528 | 735 | +6.89 |
| Threads | 8 | 8 | +0.00 |

Time to first frame: median 258 ms (30 of 30 loads reported a first frame). Longest gap between presented frames while a wallpaper loads: median 17 ms, worst 18 ms. While one is destroyed: median 17 ms, worst 17 ms.
