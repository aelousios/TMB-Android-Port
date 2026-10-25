# Android host validation status

Last validated: 2026-10-25, Pixel 11 (ARM64).

## Confirmed

- SDL2 Android Activity and native library load.
- Android `ANativeWindow` is handed to `wgpu-native` through `WGPUSurfaceSourceAndroidNativeWindow`.
- WebGPU surface creation logged `TMB_WGPU_SURFACE_SMOKE: ATTACH_PASS`.
- The diagnostic presented 300 frames: `TMB_WGPU_SURFACE_SMOKE: PRESENT_PASS: frames=300`.
- `zipalign -c -P 16 -v 4 android-host-smoke-debug.apk` reported `Verification successful`.
- `llvm-objdump -p` reported `align 2**14` on all LOAD segments in the built `libmain.so` and rebuilt `libSDL2.so`.

## Rebuild

Run from the repository root on the Mac where the Android SDK/NDK and current build caches are installed:

```bash
bash scripts/build_android_webgpu_host.sh "$PWD"
```

The script is a diagnostic-host builder. It intentionally uses the pinned PS2 Web Recomp checkout and builds a surface/presentation probe, not TMB game code. It must not be described as a playable port.

## ISO selection

The primary `app` has an Android Storage Access Framework picker. This change persists the URI permission for the selected ISO so the app can access it again. Today that app still inspects the ISO and validates the US `SCUS_971.01` executable; it does not execute MIPS code.

## What remains for a real game boot

PS2 Web Recomp's `scripts/port.sh` statically generates native C++ from the disc executable on the development host. The game's generated source is version/executable-specific and is not present in this repository yet. A selected disc URI inside the APK cannot replace that generation/build step. With a legally obtained matching US NTSC-U ISO, run `scripts/probe_ps2_web_recomp.sh /absolute/path/to/disc.iso`, triage `games/tmb/explore.log`, then integrate the resulting generated game code into an Android host that opens the selected ISO from app-accessible storage. Do not upload the disc, extracted executable, generated game code, or copyrighted assets to GitHub.
