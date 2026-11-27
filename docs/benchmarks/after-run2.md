### after-run2

Commit `3ef3699`, 2026-11-27 14:50. NVIDIA GeForce RTX 5070 Ti, 620.73.04; AMD Ryzen 7 9800X3D 8-Core Processor; kernel 7.3.6-2-cachyos. Rendered at 3840x2160.

| Scene | Cold load (ms) | Warm load (ms) | Teardown (ms) | CPU per frame, 15 fps (ms) | CPU per frame, 240 fps (ms) | GPU use at 240 fps (%) | Frames in 240 fps run |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3593248271 | 326 | 258 | 26.2 | 0.131 | 0.061 | 3.5 | 3701 |
| 3602874264 | 1067 | 942 | 27.5 | 0.921 | 0.410 | 23.3 | 3702 |
| 3798007377 | 485 | 376 | 30.4 | 0.961 | 0.493 | 38.0 | 3702 |
| 1905244619 | 250 | 218 | 29.3 | 0.509 | 0.333 | 5.9 | 3701 |
| 2276185076 | 274 | 232 | 27.6 | 0.194 | 0.105 | 16.4 | 3700 |
| **Mean** | **480** | **405** | **28.2** | **0.543** | **0.280** | **17.4** | |

Headless create/destroy, 40 cycles: +0.00 file descriptors, +0.21 MB resident memory and +0.00 threads per cycle; 631 ms per cycle.

Window harness (1766x994 logical, 30 cycles through Qt/OpenGL, exit code 0):

| Measure | Start | End | Per cycle |
| --- | ---: | ---: | ---: |
| GPU memory (MB) | 60 | 60 | +0.00 |
| File descriptors | 48 | 48 | +0.00 |
| Resident memory (MB) | 529 | 728 | +6.62 |
| Threads | 8 | 8 | +0.00 |

Time to first frame: median 257 ms (30 of 30 loads reported a first frame). Longest gap between presented frames while a wallpaper loads: median 17 ms, worst 17 ms. While one is destroyed: median 17 ms, worst 18 ms.
