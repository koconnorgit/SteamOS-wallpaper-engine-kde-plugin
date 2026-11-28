### malloc-default-run1

Commit `7b0d98d` with uncommitted changes, 2026-11-28 05:52. NVIDIA GeForce RTX 5070 Ti, 620.73.04; AMD Ryzen 7 9800X3D 8-Core Processor; kernel 7.3.6-3-cachyos. Rendered at 3840x2160.

| Scene | Cold load (ms) | Warm load (ms) | Teardown (ms) | CPU per frame, 15 fps (ms) | CPU per frame, 240 fps (ms) | GPU use at 240 fps (%) | Frames in 240 fps run |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3593248271 | 333 | 260 | 25.7 | 0.136 | 0.058 | 1.3 | 3703 |
| 3602874264 | 1129 | 957 | 27.7 | 1.252 | 0.437 | 38.2 | 3701 |
| 3798007377 | 481 | 378 | 28.9 | 1.002 | 0.540 | 62.7 | 3704 |
| 1905244619 | 251 | 212 | 26.3 | 0.567 | 0.400 | 8.2 | 3700 |
| 2276185076 | 269 | 232 | 26.3 | 0.224 | 0.112 | 12.2 | 3702 |
| **Mean** | **492** | **408** | **27.0** | **0.636** | **0.310** | **24.5** | |

Headless create/destroy, 40 cycles: +0.00 file descriptors, +0.00 MB resident memory and +0.00 threads per cycle; 633 ms per cycle.

Window harness skipped: --no-window.
