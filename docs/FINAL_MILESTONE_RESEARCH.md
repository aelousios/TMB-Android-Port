# TMB Android Port: researched route to a playable build

Research date: 2026-10-24
Baseline expected: `aelousios/TMB-Android-Port` commit `1df2db7af1e2f15ffdd16d3ac9a0693d5da9818e` (guest EE memory milestone).

## Decision

Stop expanding the hand-written placeholder runtime as the primary path. Use **PS2 Web Recomp** as the first candidate runtime, test the original US retail ELF on the Mac, then port its native runtime to Android only after the game gets far enough to justify that work.

Upstream: https://github.com/snuri00/ps2-web-recomp
Pinned revision: `333999564ad0fe84f24b504416869dc182404a61`
License: MIT (preserve its LICENSE if its code is later copied or vendored).

This is a hybrid native recompilation route, not a pure game-source port: the main PS2 Emotion Engine code is statically translated ahead of time into C++, while the runtime models PS2 hardware/services (IOP, vector units, GS, DMA, sound, disc I/O). The CPU instructions are not interpreted at runtime, but hardware compatibility is still implemented/emulated. This is the closest credible open-source route found to a playable, native-compiled application without waiting for the TMB matching decompilation to finish.

## Why this is a better primary route

* `ps2-web-recomp` has an automated `scripts/port.sh` pipeline that takes a PS2 disc image, extracts the boot ELF named in `SYSTEM.CNF`, recompiles it to C++, performs headless exploration to discover indirect entry points and VU1 microprograms, then builds a native runner.
* Its runtime is materially more complete than a bare ELF loader: EE kernel services, an IOP R3000A interpreter/HLE layer, GS memory/rasterization through WebGPU, VU1 microprogram compilation, SPU2 audio and IPU movie decoding are documented.
* The project reports successful bring-up of Gran Turismo 4 and God of War II. More importantly for TMB, the revision pinned below includes the 2026-10-24 merge of PR #20, **“Downhill Domination plays from boot through races.”** That contribution reports a full run from boot through intro movies, menus and races, including split screen, on macOS ARM64 and Chrome, with generic runtime fixes and no game-specific code. The fixes include PS2 thread exit semantics, realistic delayed HLE RPC replies, Graphics Synthesizer soft reset behavior, VIF ROW/COL reset persistence, and VIF1 download FIFO status. Downhill Domination is a closer vehicle/racing workload than GT4 or GoW II and materially increases confidence in the runtime choice. **Twisted Metal: Black itself has not been independently verified with it.** The first task remains an ISO-based compatibility probe, not an assumed Android integration.
* The native runner has a documented macOS Apple Silicon build path and uses SDL2 plus `wgpu-native`. `wgpu-native` publishes Android ARM64 builds and documents Vulkan/OpenGL ES support on Android, so an Android native target is technically plausible, but this is not a turnkey Android port of `ps2-web-recomp`.

## Why not make the existing TMB Re-Arc project the runtime base yet?

Upstream TMB Re-Arc: https://github.com/OpenTrac/tmb_Re-Arc
Current upstream report: https://github.com/OpenTrac/tmb_Re-Arc/blob/master/config/US/report.json

The checked-in progress report says 26,980 of 1,221,468 code bytes are matched (2.2088%) and 107 of 3,606 functions are matched (2.9673%). Its README also asks developers not to port the code until the matching decompilation is closer to completion. This does not mean the game itself is only 2.2% understood, and it does not block static recompilation: the PS2 Web Recomp pipeline starts with the original ELF and analyses machine code directly, independently of how much matching C source has been reconstructed.

## Secondary candidate: PS2Recomp

Repository: https://github.com/ran-j/PS2Recomp
Android runner instructions: https://github.com/ran-j/PS2Recomp/blob/main/android/README.md

This tool also statically recompiles R5900 instructions into C++ and already contains an Android NativeActivity runner. Keep it as a fallback/experiment. Its own README describes the runtime as a foundation with basic GS/VU/file/system stubs and says performance is very bad for VU and GS. For a graphics-heavy game like TMB, PS2 Web Recomp's fuller GS/VU/IOP implementation is the better first probe; PS2Recomp remains useful for comparison or isolated subsystems.

## Milestones, in order

### A. Prove TMB on the existing native runtime (Mac, needs your ISO)

Run `scripts/probe_ps2_web_recomp.sh /absolute/path/to/Twisted-Metal-Black-US.iso` from the repository root. The script checks prerequisites, fetches the pinned open-source runtime outside the Git repository, and runs the generic native recompilation/exploration pipeline. It does not copy the ISO into this repository. The target release must be the US NTSC-U disc whose boot ELF is `SCUS_971.01` / serial `SCUS-97101`.

Inspect these local outputs in the external work directory printed by the script:

* `games/tmb/explore.log` — first missing syscall, IOP library, halt reason or crash.
* `games/tmb/shots/` — frames captured during exploration.
* `build/native-tmb/ninja.log` — native compile errors if the generated game fails to compile.

Do not push the ISO, extracted ELF, generated game code, generated assets, or screenshots from commercial game content into GitHub.

### B. Fix the first real compatibility blockers

Work from reproducible logs. Implement the missing generic runtime behaviors upstream or in a clearly isolated TMB-specific compatibility patch. Prioritize boot, filesystem/CDVD and IOP modules, DMA/interrupts, GS/VU1 behavior, sound and controller input. Don't paper over a hard failure by returning success from an unknown syscall; that can create a misleading black-screen boot.

### C. Port the proven native runner to Android ARM64

The existing app's `NativeGameView` and OpenGL ES placeholder are not the eventual renderer. Adapt the PS2 Web Recomp native runner to an Android NDK/SDL2 entry point, link a compatible `wgpu-native` Android build, use the Android Vulkan backend (with OpenGL ES fallback only if the upstream renderer supports it), and expose an Android document picker for the user's ISO. Add touch controls by translating button presses to the runtime's pad/input layer. Keep the ISO and memory card in app-private/user-selected storage; bundle no game data.

### D. Device validation

Build the `arm64-v8a` APK, install on the target Android phone, record logs and frame times, then test clean boot, menus, an actual match, vehicle/weapons, audio, save/memory-card behavior, pause/resume and 20+ minutes of stability. The final milestone is not achieved by a successful APK build: the original game must reach gameplay and accept input.

## Immediate requirement

The ISO-based probe cannot run until you have a legally obtained US NTSC-U image. Everything else can be prepared, but no one can truthfully confirm game compatibility from the ISO filename alone.

## Sources

* PS2 Web Recomp README and workflow: https://github.com/snuri00/ps2-web-recomp/blob/333999564ad0fe84f24b504416869dc182404a61/README.md
* Downhill Domination boot-to-race milestone merged into the pinned revision: https://github.com/snuri00/ps2-web-recomp/pull/20
* PS2 Web Recomp MIT license: https://github.com/snuri00/ps2-web-recomp/blob/333999564ad0fe84f24b504416869dc182404a61/LICENSE
* PS2Recomp README and limitations: https://github.com/ran-j/PS2Recomp/blob/main/README.md
* PS2Recomp Android runner: https://github.com/ran-j/PS2Recomp/blob/main/android/README.md
* wgpu-native Android releases: https://github.com/gfx-rs/wgpu-native/releases
* SDL2 Android build guidance: https://wiki.libsdl.org/SDL2/README-android

## What the ICO Android port teaches us (the direct reference you requested)

ICO is a genuine native source port, not an emulator in an APK. Its architecture document says the game code under `ico2/` is compiled as C for the host. The host port replaces the platform-specific PS2 surroundings: EE kernel/thread scheduling and memory arena, Sony library compatibility headers, CDVD/ISO/CHD/BIN reading and VFS, Graphics Synthesizer/VU1 rendering and shader/RHI backends, SPU2 audio, movie playback, controller input, and memory-card saving. Android uses the same native game code through an NDK build, SDL3 glue, and Vulkan renderer. The public build instructions point at `android/`, `tools/fetch_android.sh`, Gradle and CMake; the APK itself contains no game data and needs the supported original disc image.

Sources: [ICO port](https://github.com/nathanialf/ico-pc), [architecture](https://github.com/nathanialf/ico-pc/blob/main/docs/port/ARCHITECTURE.md), [build guide](https://github.com/nathanialf/ico-pc/blob/main/docs/BUILDING.md), [Android Gradle config](https://github.com/nathanialf/ico-pc/blob/main/android/app/build.gradle).

**Why we cannot simply copy that recipe for TMB today:** ICO has an almost completely matched game source tree, including the main text and VU text. TMB's Re-Arc report, by contrast, has only 2.21% of code bytes and 107 of 3,606 functions matched. The ICO lesson is the architecture of the replacement platform layer, not a magic Android wrapper: someone must either reconstruct most of TMB's source or statically recompile its original MIPS executable. Since the TMB source decompilation is at an early stage, static recompilation is the better first experiment.

## Another practical option: PS2 Recomp's Android app

[PS2Recomp](https://github.com/ran-j/PS2Recomp) already has an Android `NativeActivity` runner and an NDK/CMake build, so it is structurally closer to an APK than PS2 Web Recomp. But its own README describes the runtime as experimental, with basic GS/VU/file/system stubs and poor VU/GS performance. Treat it as an alternative to test if PS2 Web Recomp's runtime cannot be adapted, not as a known playable TMB port.

## Optional browser-build experiment

Once the native probe gets TMB past boot, build the web target as a separate diagnostic. The PS2 Web Recomp shell already implements PS2-style touch controls, Gamepad API, audio worklet, memory-card import/export and ISO reads via HTTP byte ranges. Chrome on Android has WebGPU enabled by default on Android 12+ devices with Qualcomm or ARM GPUs, but support and drivers vary. An APK shell around the browser build may therefore be a quicker device-validation route, but it is **not** our selected definition of a native Android port, and it still requires actual TMB boot testing first.

Sources: [PS2 Web Recomp web shell](https://github.com/snuri00/ps2-web-recomp/blob/main/web/shell.html), [Chrome's Android WebGPU support note](https://developer.chrome.com/blog/new-in-webgpu-121).
