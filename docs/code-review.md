# Code review, November 2026

A full read of the plugin at commit `249caec`: the QML front end, the C++ plugin layer, the scene content parsers and the Vulkan render core. Vendored libraries (`source/src/backend_scene/third_party`) were not reviewed.

This file is the work list. Each item has an id, a location, what is wrong and the intended fix. Tick the box when the fix is merged. Performance before and after is tracked in [benchmark.md](benchmark.md).

How to read an item:

- **checked** means the code was read a second time to confirm the description. Everything else is a single reviewer's finding from reading code.
- Nothing here was reproduced as a failure unless the item says so.
- Paths are relative to `source/`. `B/` is short for `src/backend_scene/`, `ui/` for `plugin/contents/ui/`.

| Section | Items | Open |
| --- | --- | --- |
| A. Leaks and crashes in normal use | 12 | 0 |
| S. Schedule, playlists and crossfade | 16 | 16 |
| H. Hardening against bad workshop content | 21 | 0 |
| T. Follow-ups to the TEXB0004 / effects fix | 4 | 0 |
| E. Efficiency | 22 | 22 |
| R. Redundancy and dead code | 14 | 14 |
| B. Build | 8 | 8 |
| Q. Tests | 5 | 5 |
| N. Found while fixing | 3 | 3 |

## A. Leaks and crashes in normal use

Playlists and the crossfade create and destroy a scene every few minutes on each monitor, so anything left behind per switch adds up over days of uptime.

All fixed on 27 November 2026. How each was checked:

- A1, A2, A3: the quick benchmark shows 0 MB of GPU memory and 0 file descriptors per cycle (baseline: 45 MB and 3). The `GL_INVALID_ENUM at genExTexture` log line is gone too; it was a stale error from an earlier call (E21).
- A4: a scene with invalid JSON and one with no `objects` key now log an error and load nothing. The failure is only logged; telling QML so a playlist can skip the item is left to S9.
- A10: the test scenes render as before with the fallback texture in place.
- A5, A6, A7, A8, A9, A11, A12: by reading only. Their triggers are races or events (node dropped by Qt, bus failure) that were not reproduced before or after.

- [x] **A1. GL memory objects are never deleted** (checked). `B/qml_helper/glExtra.cpp:139-158,161-164`. `genExTexture` creates a memory object per shared frame image and drops the name; `deleteTexture` deletes only the texture. Three images per scene, about 33 MB each at 4K. Fix: keep the name in `ExTex` and delete it with the texture, or delete it right after `glTexStorageMem2DEXT`.
- [x] **A2. Exported fd is closed twice** (checked). `B/qml_helper/SceneBackend.cpp:150-156`. A successful `glImportMemoryFdEXT` takes ownership of the fd; the code then calls `close(fd)` itself. Another plasmashell thread can be handed the same number in between. Fix: close only when the import failed.
- [x] **A3. Fds that were never imported leak.** `B/src/Vulkan/TextureCache.cpp:221`, `B/src/Vulkan/Parameters.cpp:36`, `Parameters.hpp:60`. A wallpaper destroyed before all three images were presented leaks up to three fds and the memory behind them. The empty value is 0 (stdin). Fix: own the fd in `ExImageParameters`, close it in the destructor unless handed off, use -1 as empty.
- [x] **A4. A scene that fails to parse crashes the shell** (checked). `B/src/SceneWallpaper.cpp:471-472`, `B/src/WPSceneParser.cpp:1135,1144`. `Parse` returns null on invalid JSON and the caller dereferences it; `json.at("objects")` throws; nothing on the load path catches. Fix: `try`/`catch` around `Parse`, null check, report failure to QML so a playlist can skip the item.
- [x] **A5. `shared_from_this()` after the owner is gone.** `B/src/SceneWallpaper.cpp:204,235,488,492`. Called from the render thread during Vulkan init or the first frame; throws `bad_weak_ptr` if the wallpaper was destroyed meanwhile. Not traced end to end. Fix: hold a `weak_ptr<MainHandler>` and `lock()` it.
- [x] **A6. Frame timer can start dead** (checked). `B/src/Timer/ThreadTimer.cpp:19-28`. `m_running` is set after the thread starts, so the thread can see false and exit. The wallpaper would freeze after its first frame until the next pause/resume. `Stop` (`:31-46`) can also miss its notify. Fix: set the flag before creating the thread.
- [x] **A7. Looper start/stop race.** `B/src/Looper/Looper.cpp:50,65`. `m_running` is set inside the new thread without a lock; `stop()` returns early when it is false. A wallpaper destroyed before its loopers first run skips the join and `~std::thread` terminates the process. Fix: set the flag under the mutex in `start()`.
- [x] **A8. `FrameTimer` deque is touched from two threads.** `B/src/Timer/FrameTimer.cpp:47-55,65-67`, `B/src/SceneWallpaper.cpp:339`. `SetRequiredFps` runs on the main looper while `FrameEnd` runs on the render thread. `(uint8_t)fps` also truncates, so 256 becomes 0 and divides by zero. Fix: send fps as a render-handler message, clamp the value.
- [x] **A9. `qFatal` in the sleep monitor** (checked). `src/TTYSwitchMonitor.cpp:10,24`. Aborts plasmashell if the system bus or the logind hookup fails. Fix: `qWarning` and carry on.
- [x] **A10. Sampler bindings left unwritten.** `B/src/VulkanRender/CustomShaderPass.cpp:105-126,179-184,419-423`, `B/src/Vulkan/TextureCache.cpp:461-470,500`. A texture that failed to load is skipped at bind time but the draw still samples it; a failed `CreateImage` still caches the half-built slot. Fix: push a 1×1 fallback for every reflected sampler without a valid slot; do not cache partial slots.
- [x] **A11. Paint node cannot be recreated while its `SceneObject` lives.** `B/qml_helper/SceneBackend.cpp:109-128,212-230`, `B/src/VulkanRender/VulkanRender.cpp:115-117,305`. Callbacks capture the `TextureNode` raw; if Qt drops only the node (item moved to another window, scene graph invalidated) every frame emits on freed memory and the new node shows nothing. `RenderInitInfo::uuid` is a span into the node, posted to another thread. Trigger not reproduced. Fix: route callbacks through a shared state object cleared in the destructor, copy the uuid.
- [x] **A12. Final-pass descriptor layouts accumulate on scene reload.** `B/src/Vulkan/GraphicsPipeline.cpp:175`, `B/src/VulkanRender/FinPass.cpp:167`. Each reload on a live renderer (user-property change) appends another push-descriptor layout. Fix: reset `PipelineParameters` in `create()` or `FinPass::destory`.

## S. Schedule, playlists and crossfade

- [ ] **S1. Two monitors on one playlist change only every other step.** `ui/js/schedule.mjs:363-369`, `ui/main.qml:186-199`. With the same slot time and interval, the first monitor to evaluate skips past the second's outgoing wallpaper and the second then keeps what it has. Simulated by the reviewer: A = a,c,c,e,e,a and B = b,b,d,d,f,f. Fix: have presence carry a "valid until" time and ignore entries that are due to change.
- [ ] **S2. Re-deciding a step makes a monitor hop.** `ui/main.qml:117-120,224-227`, `ui/js/schedule.mjs:368`. A playlist save or schedule edit blanks the applied key; the "prefer something other than current" rule then picks a different wallpaper for the same step. Fix: do not blank the key on playlist text changes; keep the current wallpaper when it is still a free candidate.
- [ ] **S3. Outgoing wallpaper takes the incoming one's options.** `ui/main.qml:31-57,195-196`, `ui/backend/Scene.qml:10-16,45-48`, `Mpv.qml:12-16,59`, `QtMultimedia.qml:9-14,55`. Mute, volume, display mode, speed and user properties are shared bindings that switch before the new backend exists. A muted wallpaper can become audible for the wait plus the fade. Fix: pass options at creation or freeze the outgoing holder's values.
- [ ] **S4. Any first frame reveals the incoming wallpaper** (checked). `ui/main.qml:279,473-486`. The signal carries only a backend name and `reveal()` only checks `waiting`. A late frame from the previous backend can fade in an empty holder. Fix: connect to the specific incoming item.
- [ ] **S5. Startup loads the saved wallpaper, then the scheduled one** (checked). `ui/main.qml:602-618`. `applySource()` runs before the migrations and `evaluateSchedule()`. Two scene loads whenever the schedule moved on while logged out. Fix: evaluate first.
- [ ] **S6. Sleep handler calls methods that do not exist** (checked). `ui/main.qml:91-103`. `this.pause()` / `this.play()` on the monitor object. The wallpaper is never paused for suspend. Fix: `backendLoader.item.pause()` / `background.autoPause()`.
- [ ] **S7. `userPicked` is never reset after Apply.** `ui/config.qml:121,287-291`, `ui/page/WallpaperPage.qml:361`. A later Apply with the dialog still open writes a stale wallpaper back without a new hold. Fix: reset in `saveConfig()`.
- [ ] **S8. Pausing during a transition handles only the incoming backend.** `ui/main.qml:391-399,466-470,596-600,608`. The outgoing one keeps playing with audio; the incoming one can be paused before its first frame. Fix: pause the old holder too, defer pausing the new one until revealed.
- [ ] **S9. A backend that fails to create replaces a working wallpaper.** `ui/main.qml:466-470,515-533`. `createObject` returns null instead of throwing, the empty holder is revealed after 5 s, and later calls hit `null.play()`. Fix: treat null like the catch branch; on timeout keep the old holder.
- [ ] **S10. Global Display/Mute/Volume/Speed may not apply live.** `ui/main.qml:43-76,136-138`. Imperative assignment removes the bindings; the remaining `Connections` are the kind the file's own comment says never fire. Needs a runtime check: change Volume after one scheduled change.
- [ ] **S11. Editing a slot whose playlist was deleted retargets it silently.** `ui/page/SchedulePage.qml:477-482`. `findIndex` -1 is clamped to 0. Two slots at the same time also shadow one another with no hint (`ui/js/schedule.mjs:306`). Fix: preselect "Nothing yet"; mark shadowed slots.
- [ ] **S12. Sun-relative playlist slots re-seed when the sun time shifts.** `ui/js/schedule.mjs:321,338,343`. `firedAt` in milliseconds feeds the key, step boundaries and shuffle seed. Depends on KNightTime behaviour. Fix: seed with slot id plus local date.
- [ ] **S13. Mouse grabber is never destroyed** (checked). `ui/main.qml:320`. `this.mouseHooker.destroy;` has no parentheses.
- [ ] **S14. Web wallpapers never receive user properties** (checked). `ui/backend/QtWebView.qml:41-45`, `ui/Pyext.qml:34-38`. A callback is passed to a one-argument `readfile`. `getWorkshopIDPath()` (`ui/main.qml:310`) is also wrong for defaultprojects/myprojects. Fix: `.then(...)`, null-check `json.general`, derive the folder from the source.
- [ ] **S15. Playlist file is last-writer-wins.** `src/PlaylistStore.cpp:27-36,50-54`, `ui/config.qml:110-112,130`. An open settings dialog overwrites changes made elsewhere on Apply. A truncated external edit is read as an empty list and resets every monitor. Fix: debounce `reload()`, ignore unparsable content, merge or reject on mismatch.
- [ ] **S16. WindowModel.** `ui/WindowModel.qml:99` calls a signal handler as a function (likely throws on activity switch); `:251` logs on every window event in FullScreen pause mode.

## H. Hardening against bad workshop content

The parsers trust workshop files completely and run inside plasmashell. Each of these needs unusual or corrupt content. A4 (catch around the load) turns most from a shell crash into a wallpaper that fails to load, but hangs and out-of-bounds writes still need their own fix.

All fixed on 27 November 2026, and checked three ways:

- **Library comparison.** All 120 scene wallpapers in the Workshop folder were loaded with the renderer before and after the fixes. All 120 reach a first frame with both, and the only change in error output is the one described in N1.
- **Damaged files.** `tools/fuzz/mutate.py` made 112 corrupted copies of four wallpapers (truncations, byte flips in headers, json, shaders, models and texture headers, planted extreme integers). The fixed renderer worked through all 112 without a crash or a hang. The renderer before the fixes, run on 72 of them, crashed on 32 (30 segmentation faults, 2 uncaught exceptions).
- **Rendering.** Screenshots of the benchmark scenes are unchanged.

Notes on individual fixes:

- H1 keeps the old integer zoom (1.5 behaves as 1) and only stops zoom below 1 from dividing by zero. Whether fractional zoom should be honoured is a separate question.
- H7 stops a mip chain at the first level whose size is not what the gpu expects, where it used to upload it anyway.
- H9 shortens an animation to the frames it really has, and drops one with none.
- H21: the mpv event queue is still never drained; that part moved to R4.

- [x] **H1. Integer division by zero on zoom** (checked). `B/src/WPSceneParser.cpp:481-482`. `ortho_w / (i32)general.zoom`; any zoom in (-1, 1) is 0. Fix: divide in float, clamp zoom.
- [x] **H2. Null dereference in the model parser** (checked). `B/src/WPMdlParser.cpp:44-46`. `MemBinaryStream(*pfile)` before `if (!pfile)`.
- [x] **H3. Infinite loops at end of file in the model parser.** `B/src/WPMdlParser.cpp:71-73,190-214,228-230`. Reads return 0 at EOF; the loops scan for a marker that never comes, including any model with no `MDLA` section. Counts at `:80-89,163,223,246,250` drive `resize()` unchecked. Fix: add `Eof()`, break on it, check counts against remaining bytes.
- [x] **H4. Shader cache trusts empty or truncated files and is written in place.** `B/src/WPShaderParser.cpp:433-466,573-584`. A zero-length file "loads" with no stages; a bad file is never recompiled. Two monitors loading the same scene can read mid-write. Fix: write to a temp file and `rename()`; validate version, count, sizes and the SPIR-V magic; fall through to compile on any failure.
- [x] **H5. Texture slot indexes are never range-checked.** `B/src/wpscene/WPMaterial.h:17`, `B/src/WPSceneParser.cpp:316-323,398,844-846`, `B/src/WPShaderParser.cpp:147-155`, `B/src/VulkanRender/CustomShaderPass.cpp:179`. A bind index of -1 is a wild write; a slot ≥ 13 reads past the name tables. Fix: one `kMaxTexSlots` constant, reject outside [0, 13), initialise `index`.
- [x] **H6. Pkg header is unvalidated.** `B/src/WPPkgFs.cpp:12-21,33-46,56-57`. String lengths, entry count, offsets and lengths are used as read. Fix: validate all against the file size.
- [x] **H7. Texture sizes are not validated against the data.** `B/src/WPTexImageParser.cpp:163-239`, `B/src/Vulkan/TextureCache.cpp:436-486`. Counts go straight into `resize()`; `Read` results are ignored; nothing ties the byte count to width × height; a decoded PNG smaller than the declared mip is uploaded with the declared size. Fix: bound counts, check reads, use the decoded size, require enough bytes for raw formats.
- [x] **H8. Sprite sheet parsing.** `B/src/WPTexImageParser.cpp:259-326`, `B/src/WPSceneParser.cpp:381-389`. Negative or out-of-range `imageId` is used after being logged; `framecount` is unbounded; `resolution % (int)f1.width` divides by zero for a frame narrower than one pixel. Fix: validate, fall back to non-sprite, guard the modulo.
- [x] **H9. Animation length is not checked against frame data.** `B/src/WPPuppet.cpp:73-75,120`, `B/src/WPMdlParser.cpp:243-256`. `% length` with length 0; `frames[...]` indexed past its size every frame. Fix: reject such animations at parse.
- [x] **H10. Sound stream crashes on the audio thread.** `B/src/WPSoundParser.cpp:61-69`, `B/src/Audio/SoundManager.cpp:41-43,74-80`. An empty `sound` list indexes an empty vector; a missing file gives a null stream that the decoder reads. Fix: reject empty lists, treat null as no data.
- [x] **H11. Pkg stream seek and read boundaries** (checked the seek). `B/src/Fs/include/Fs/LimitedBinaryStream.h:20,36-48,62-65`. `SeekSet(0)` always fails; `SeekEnd` is off by one. Suspected to break some sounds stored inside packages (not run). Fix: `pos >= 0`, correct `SeekEnd`, clamp `Read`.
- [x] **H12. Unbounded recursion and `npos` handling in shader includes.** `B/src/WPShaderParser.cpp:74-97,120,476-480`, `B/src/wpscene/WPParticleObject.cpp:9-20`. A header that includes itself, a particle that lists itself as a child, a final `#include` with no newline, a `[COMBO]` line without `{`. Fix: cap depth, treat `npos` as size, check before `substr`.
- [x] **H13. Particle indexes are 16-bit.** `B/src/Particle/WPParticleRawGener.cpp:208-226,249`, `B/src/WPSceneParser.cpp:985,1024`. The loop counter is `uint16_t` against a `size_t` count, so ≥ 65536 particles never terminates; `index * 4` wraps above 16383 quads. Fix: clamp or use 32-bit indexes.
- [x] **H14. Emitter hang and negative control point.** `B/src/Particle/ParticleEmitter.cpp:28-35`, `B/src/WPParticleParser.cpp:386,419,566`, `B/src/wpscene/WPParticleObject.cpp:74-75`. A negative rate loops forever; `% 8` keeps negatives negative. Fix: `fmod`, clamp to [0, 7].
- [x] **H15. Other throw sites reachable from content.** `B/src/WPMdlParser.cpp:218` (`stoi`), `B/src/wpscene/WPMaterial.cpp:91` (`.at(0)`), `B/src/Fs/include/Fs/PhysicalFs.h:19` and `CBinaryStream.h:59` (filesystem calls on over-long names). Fix: error-code overloads, checks.
- [x] **H16. Uninitialised values when a JSON key is missing.** `B/src/wpscene/WPMaterial.cpp:69,121`, `WPImageObject.cpp:149-152`, `WPScene.h:35-37`, `WPImageObject.h:50,53`, `B/src/WPSceneParser.cpp:685`. Fix: default-initialise.
- [x] **H17. Asserts relied on that vanish in the default build.** `B/src/VulkanRender/CustomShaderPass.cpp:129-131`, `SceneToRenderGraph.cpp:132`, `VulkanRender.cpp:449-450`. `.at()` then throws on the render thread. Fix: `find()` plus log, mark the pass unprepared.
- [x] **H18. `g_DayTime` is always 0** (checked). `B/src/WPShaderValueUpdater.cpp:21-28,190`. The only assignment is commented out. Fix: restore it, updated about once a second.
- [x] **H19. Sampler V axis uses `wrapS`** (checked). `B/src/Vulkan/TextureCache.cpp:68-70,446-448`. Also `TextureKey::HashValue` (`:366-378`) omits `minFilter`.
- [x] **H20. Latent mistakes.** `B/src/WPSceneParser.cpp:1177,1182` set `.height = ortho_w` (overwritten later); `B/src/Vulkan/Shader.cpp:359` uses `blocks.front()` after `push_back`.
- [x] **H21. Config file and logging I/O.** `src/FileHelper.cpp:32-34,148-189`: per-wallpaper config is a non-atomic read-modify-write that discards an unreadable file, and the id is used in a path unvalidated. `B/src/Utils/Logging.cpp:28-31`: an unchecked `fopen` before `vfprintf` on shader compile failure. `src/backend_mpv/MpvBackend.cpp:346-359`: `mpv_create()` result unchecked, options set after initialise. Fix: `QSaveFile`, validate ids, check results.

## T. Follow-ups to the TEXB0004 / effects fix (7e8221c)

- [x] **T1. Effect render-target names can collide** (checked). `B/src/WPSceneParser.cpp:775`. The suffix is the image's effect-layer address, the same for every effect on that image. Two effects using one fbo name share a render target; fixed-size repeat targets make that visible. Fix: add the effect index to the name.
- [x] **T2. No clamp on fbo width/height.** `B/src/wpscene/WPImageObject.cpp:24-25`, `B/src/WPSceneParser.cpp:775-808`. Negative values wrap, huge ones go to image creation. Neighbouring paths cast through `uint16_t` (`:752-753,798-799`). Fix: parse as `i32`, clamp to [1, 8192].
- [x] **T3. Dead call.** `B/src/WPTexImageParser.cpp:177`. `SkipMipmapV4Header` inside `Parse` can never act, since video textures return at `:157`.
- [x] **T4. Shader cache key is taken before the varying fix runs.** Sound today (the fix is a pure function of the hashed sources), but any change to `Finalprocessor` or `FixVaryingMismatch` needs `SHADER_DIR` bumped by hand. Fix: hash the final sources, or add a version constant to the key.

## N. Found while fixing

- [ ] **N1. Video textures are not supported.** `B/src/WPTexImageParser.cpp`. Some scenes store an mp4 as a texture (flag 32 in the TEXI header, `ftyp` at the start of the data). 3826449546 "A cup of autumn" and 3051401409 "Witch's Cauldron" have one. They used to be uploaded as if they were pixels, which lost the gpu device on the first of the two; now the layer is skipped with a clear message and the rest of the scene renders. Playing them needs a video decoder feeding a texture, which is a feature, not a fix.
- [ ] **N2. Resident memory grows 7–9 MB per wallpaper switch in the Qt path.** Seen in the benchmark's window harness and not explained by A1 (GPU memory no longer grows). Not present headless. Needs a heap profile of the window harness.
- [ ] **N3. Effect fbos without the `_rt_` prefix are looked up as files.** For example `_downscaled1` and `_coc` in a depth-of-field effect log `not found "/assets/materials/_downscaled1_….tex"`. The effect's passes then sample the fallback texture, so the effect is missing. Present before this work.

## E. Efficiency

Measured effect is tracked in [benchmark.md](benchmark.md) where the benchmark covers the item.

- [ ] **E1. Scene teardown blocks the Qt render thread.** `B/qml_helper/SceneBackend.cpp:93-103,176`, `B/src/SceneWallpaper.cpp:139-143`. 50–85 ms per dropped wallpaper in the journal. Fix: release the last reference on a worker thread.
- [ ] **E2. File I/O on the GUI thread behind a fake promise.** `src/FileHelper.cpp:36-146`, `ui/Pyext.qml:19-55`. `ui/WallpaperListModel.qml:218` reads every `project.json` when the dialog opens; `ui/page/WallpaperPage.qml:536` sizes a directory inside a binding. Fix: one batched worker-thread scan, cached sizes.
- [ ] **E3. Settings page is instantiated twice per open.** `ui/WallpaperListModel.qml:168-194,240-258`, `ui/config.qml:261-281`. Plasma creates a throwaway instance; each runs the full scan. Fix: defer the scan until visible.
- [ ] **E4. Framebuffer created and destroyed every frame** (checked). `B/src/VulkanRender/FinPass.cpp:197-208`. Fix: one per output image. `FinPass::prepare` also recompiles its two fixed shaders on every load (`:104-116`).
- [ ] **E5. Mip chain regenerated every frame, sampler capped at LOD 1** (checked the cap). `B/src/Vulkan/TextureCache.cpp:76`, `B/src/VulkanRender/VulkanRender.cpp:430-434`, `CopyPass.cpp:152`. Fix: `maxLod = mipmap_level`.
- [ ] **E6. Whole dynamic buffer re-uploaded every frame.** `B/src/Vulkan/StagingBuffer.cpp:232-248`, `B/src/VulkanRender/VulkanRender.cpp:383`. Fix: persistent mapping, dirty range only.
- [ ] **E7. Every pass re-executes every frame.** `B/src/VulkanRender/CustomShaderPass.cpp:400-402,416,439-457`, `VulkanRender.cpp:385-389`. A fully static scene still renders at the configured fps. Fix: stop drawing after the first frame when nothing is time-dependent; drop the per-pass heap allocation and no-op barrier.
- [ ] **E8. One Vulkan instance and device per wallpaper, no pipeline cache.** `B/src/VulkanRender/VulkanRender.cpp:145-174,214-224`, `B/src/Vulkan/GraphicsPipeline.cpp:240`. Fix (small): a serialised `VkPipelineCache`. Sharing a device is a large refactor.
- [ ] **E9. Texture upload: one submit and `vkDeviceWaitIdle` per slot.** `B/src/Vulkan/TextureCache.cpp:472-498`. Staging results are unchecked. Fix: one command buffer, one fence.
- [ ] **E10. Textures decoded once per pass.** `B/src/VulkanRender/CustomShaderPass.cpp:118-120`, `B/src/Vulkan/TextureCache.cpp:420-423`, `B/src/WPTexImageParser.cpp:232-235`. The decode happens before the cache lookup. Fix: check the cache first; move the buffer instead of copying.
- [ ] **E11. `ParseHeader` is uncached.** `B/src/WPSceneParser.cpp:289,355`, `B/src/WPTexImageParser.cpp:244-336`, `B/src/WPPkgFs.cpp:53`. Every material re-opens the pkg per texture. Fix: a header map in the parser.
- [ ] **E12. `std::regex` built on every call.** `B/src/WPShaderParser.cpp:239,249,288,308,320,341,349,354,373,394,396,416`, `B/src/WPSceneParser.cpp:233`. About 13 per material, several even on a cache hit. Fix: `static const`, plain string operations for literals.
- [ ] **E13. Shader sources and JSON re-read and re-copied per object.** `B/src/WPSceneParser.cpp:270-306`, `B/src/WPJson.cpp:46,64`. Fix: per-parse source and SPIR-V maps; return references from `ResolveUserProperty`.
- [ ] **E14. Shader cache is never pruned and not shared between scenes.** `B/src/WPShaderParser.cpp:22,421-431`. `~/.cache/wescene-renderer/<id>/spvs01/`. Fix: prune old `spvsNN` directories and entries for unsubscribed ids.
- [ ] **E15. Schedule tab's 20 s timer rebuilds too much.** `ui/page/SchedulePage.qml:28-41,154`, `ui/config.qml:215-245`, `ui/page/WallpaperPage.qml:334-336`. All slot rows are recreated and three bindings per grid card re-evaluate. Fix: expose key and wid as strings.
- [ ] **E16. Each monitor stats every playlist item once a minute.** `ui/main.qml:154-157,175,205-206,229`. Fix: cache availability.
- [ ] **E17. Repeated reads on selection and at start.** `ui/page/WallpaperPage.qml:536,680-695,882,928-935,1105`, `ui/main.qml:33,81,307,593,607`.
- [ ] **E18. Render size is truncated, not rounded.** `B/qml_helper/SceneBackend.cpp:206-210,219`. 3839×2160 and 2560×1439 with fractional scaling, so every frame is resampled; the size is also fixed at first paint. Fix: `qRound`, and resize or recreate on geometry change.
- [ ] **E19. mpv first-frame signal can fire before a frame exists.** `src/backend_mpv/MpvBackend.cpp:230,303-305,339-343`. Fix: emit after `MPV_RENDER_UPDATE_FRAME`. `play()`/`pause()` also cost two synchronous property reads each (`:166-189`).
- [ ] **E20. `getDirSize` has a dead first pass.** `src/FileHelper.cpp:64-78`. One wasted directory scan per call.
- [ ] **E21. GL error checks attribute stale errors.** `B/qml_helper/glExtra.cpp:31-36,140-158`. `GL_INVALID_ENUM at genExTexture` is logged about once per scene start; the cause is undetermined. Fix: drain errors on entry, return 0 on a failed import.
- [ ] **E22. Constant shader values loaded twice per image** (checked). `B/src/WPSceneParser.cpp:638,641-660`. The second block repeats the first, including its log lines.

## R. Redundancy and dead code

- [ ] **R1. Python-helper leftovers.** `ui/Pyext.qml` (name, fake `ok`/`log`/`version`, no-op `.catch`), `ui/page/WallpaperPage.qml:259-263`, `ui/config.qml:76-77`, `ui/Common.qml:299-305`.
- [ ] **R2. Qt 5 / KF5 branches that cannot build.** `QT_VERSION < 6` code in `src/backend_mpv/MpvBackend.cpp`, `B/qml_helper/SceneBackend.cpp`, `src/MouseGrabber.cpp:48-81`.
- [ ] **R3. Scene wrapper.** `B/qml_helper/SceneBackend.cpp/.hpp`: `m_eatFrameOp`, signals `textureInUse` and `nodeDestroyed`, `emitSceneFirstFrame()`, `resizeFb()`, `m_inited`, undefined `on_update`, `GlExtra::impl::test`.
- [ ] **R4. mpv wrapper.** (Also from H21: nothing drains the client event queue; with `loop=inf` it fills and mpv drops events. Drain it or disable the events.) `src/backend_mpv/MpvBackend.hpp/.cpp`: `statusChanged` never emitted; `status`, `logfile`, `initFinished`, `stop`, `getProperty` unused from QML; undefined `on_update`; unused `createTextureFromGl`, `on_mpv_events`, `createFramebufferObject`, `m_window`; unused includes.
- [ ] **R5. Small helper classes.** `src/MouseGrabber.hpp:14,27` (`forceCapture`, `sendEvent`), `src/PluginInfo.hpp:10-11,19` (wrong type, signals never emitted), `src/SunSchedule.hpp:15` and `src/TTYSwitchMonitor.hpp:7` (`QQuickItem` where `QObject` would do), `src/qmldir:3-6` (stale `classname` lines), `src/plugin.cpp:22,26` (missing includes).
- [ ] **R6. Renderer files in no target.** `B/src/Core/BlockingQueue.hpp` (would not compile), `B/src/Utils/FrameTimer.cpp` and its header.
- [ ] **R7. Unused renderer members and API.** `fps_counter` (`B/src/SceneWallpaper.cpp:241`); `m_testpass`, `DestroyRenderingResource`, `resource_index` (`B/src/VulkanRender/VulkanRender.cpp:76,284,315`); the cache/dirty API in `CustomShaderPass.hpp:61-74` and `VulkanPass.hpp:40-45`; `GraphicsPipeline::setRenderPass/getShaderSpv/setLogicOp`; `Device::GetUsage`; `Looper::unregisterHandler`; `Message::countEntries/getEntryAt`; `FinPass::Desc::render_layout`; `SceneWallpaper::m_offscreen`.
- [ ] **R8. Unused parser code.** `ImageHeader::mipmap_pow2`; unreachable block at `B/src/WPParticleParser.cpp:589-602`; `GenRopeParticleData` (`B/src/Particle/WPParticleRawGener.cpp:99-206`); `SoundManager::Test`; `WPSoundStream::Config::maxtime/mintime/mode` (parsed, unused, so "random" plays as loop); assorted unused parameters and locals.
- [ ] **R9. Commented-out blocks.** `B/src/Vulkan/Shader.cpp:246-314` (and `ToVkType` at `:463` used only by it), `B/src/WPShaderParser.cpp:113-117,356-358`, `B/src/WPSceneParser.cpp:225-229,403-406`, `B/src/WPPuppet.cpp:22-28`, `B/src/SceneWallpaper.cpp:259-268`.
- [ ] **R10. Duplicated logic.** `B/src/wpscene/WPMaterial.cpp:12-28` vs `:30-46`, and `:49-74` vs `:101-126`; the mip-header walk in `Parse` and `ParseHeader` (`B/src/WPTexImageParser.cpp:175-194,261-277`), which already disagree on the power-of-two test.
- [ ] **R11. Unused config and QML properties.** `plugin/contents/config/main.xml:49-52` (`FilterMode`), `:21-24` (`BackgroundColor` mislabelled, no UI); `ui/main.qml:18` (`filterStr`), `:280-284` (`onBackendFirstFrame` never connected).
- [ ] **R12. Stale QML module entries.** `ui/qmldir` (`WindowListModel` points at a missing file); `ui/Theme.qml` has no users.
- [ ] **R13. Unused or broken QML/JS helpers.** `ui/js/utils.mjs:1-23,40-46`; `ui/Common.qml` (`cbValueOfIndex`, `getWpModelFileSource`, `genItemListStr`, `listProperty`, stale comment at `:93-96`); `ui/WallpaperListModel.qml:47,230,252-256`; `ui/page/WallpaperPage.qml:375,389` (`reoslve` typo, throws inside the promise); `ui/js/schedule.mjs` (`res.index`, internal-only exports, unchecked `version`).
- [ ] **R14. Viewer-only code.** `drawFrameSwapchain`, `B/src/Vulkan/Swapchain.cpp` and the surface path are used only by the standalone viewer. Keep, but note.

## B. Build

- [ ] **B1.** `src/backend_mpv/CMakeLists.txt:7,23`: links `${MPV_LIBRARIES}` with no include or library dirs. Use `PkgConfig::MPV`.
- [ ] **B2.** `src/backend_mpv/CMakeLists.txt:4,18-22`: private Gui include dirs no longer needed; `Qt::Quick` linked `PRIVATE` though public headers use it; `Qt::OpenGL` only transitive.
- [ ] **B3.** `src/CMakeLists.txt:1`, `src/backend_mpv/CMakeLists.txt:3`, `CMakeLists.txt:42-49`: hard Qt6 in one place, `Qt${QT_MAJOR_VERSION}` and a KF5 branch elsewhere.
- [ ] **B4.** `src/CMakeLists.txt:50-67,21,8-10`: `qmldir` copied to one directory and installed from another; listed as a library source; `AUTORCC`/`AUTOUIC` with nothing to process.
- [ ] **B5.** `src/backend_mpv/standalone_viewer/CMakeLists.txt`: orphaned from the main build.
- [ ] **B6.** `CMakeLists.txt:7,19-20`: `USE_PLASMAPKG` defaults to its own undefined value; `KF_MIN_VERSION 5.68.0` applied to the KF6 lookup.
- [ ] **B7.** `CMakeLists.txt:23`: default is RelWithDebInfo while `CLAUDE.md` documents Release; no LTO although everything links into one `.so`.
- [ ] **B8.** `B/src/CMakeLists.txt:17-19`: `-Wno-unused-variable` hides leftovers (for example `B/src/Vulkan/Instance.cpp:92`).

## Q. Tests

`node --test source/tests/schedule.test.mjs` passes (19 tests).

- [ ] **Q1. Extract the schedule decision into a pure function.** The hold, key, pause-deferral and skip logic lives in `ui/main.qml:167-233` and cannot be tested from node. Move it to `schedule.mjs` as `decide(conf, now, sun, state)` and test hold expiry, edit versus pick in one Apply, deferral while paused, and key reset.
- [ ] **Q2. Multi-step, multi-monitor test of `pickCandidate`.** Assert each monitor changes every step and that re-deciding a step is idempotent. Covers S1 and S2.
- [ ] **Q3. DST.** The test at `tests/schedule.test.mjs:188-196` does nothing outside US zones. Set `process.env.TZ` and add a 02:30 slot on both changes.
- [ ] **Q4. Edge cases.** Equal-time slots; `playlistIndex` with 0–3 items and across pass boundaries over many seeds; sun input with multi-day lists, invalid entries and negative offsets across midnight; `todayMinutes` fallback (`schedule.mjs:407-409`); sanitisers with `null`, `[]`, `3`, negative minutes; availability changing mid-slot.
- [ ] **Q5. Scene backend.** There are no C++ unit tests. Two pieces now exist to build on: `scenebench --load-only` loads a list of scenes and reports which reach a first frame, and `tools/fuzz/mutate.py <scene.pkg> <out dir> <count> <seed>` writes damaged copies to feed it. Wiring the two into one command with a pass/fail result would make the check in section H repeatable.

## Checked and found fine

- Vulkan object ownership on destroy: all handles are RAII, members are destroyed before the device, the render graph before the renderer.
- Every frame waits on its fence; `destroy()` waits idle before tearing down passes.
- No thread outlives its wallpaper apart from the races in A5–A7. The journal showed matching counts of scenes created and render handlers deleted.
- mpv handle lifetime; cross-thread signals are queued.
- A paused wallpaper renders nothing. Nothing renders faster than the configured fps.
- Midnight wrap for clock and sun slots; DST under node; behaviour when sunrise/sunset is unavailable; shuffle passes; hold and applied key persistence; the legacy migrations.
- `settle()` handles a new change while waiting or fading without leaking a holder.
- The playlist store's own writes are atomic (`QSaveFile`) and its file watcher does not loop.
- `FixVaryingMismatch`, the blend-mode 32 remap and the `util/noise` fallback: no concrete bug found.
- JSON getters catch type and range errors; the user-property context is thread-local.
