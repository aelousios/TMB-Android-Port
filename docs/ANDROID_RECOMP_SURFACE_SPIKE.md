# Android runtime integration: surface spike

Status: source-level platform patch, not a complete Android runner.

## Source inspection finding

At `snuri00/ps2-web-recomp` revision `333999564ad0fe84f24b504416869dc182404a61`:

- `runtime/include/ps2/system.hpp` exposes `WindowTarget` values for X11, Wayland, and Canvas.
- `runtime/src/gs/gpu/gpu.hpp` exposes matching `SurfaceTarget` values, and `runtime/src/gs/gs.cpp` casts between these enums. Any added kind must be appended to both enums at the same ordinal.
- `runtime/src/gs/gpu/gpu.cpp::attach_surface` creates Xlib or Wayland native surfaces in its non-Emscripten path. wgpu-native's Android API instead takes an `ANativeWindow` through `WGPUSurfaceSourceAndroidNativeWindow`.
- `host/window.cpp::attach_gpu` already obtains platform window information via SDL2 `SDL_GetWindowWMInfo`; it lacks a branch for `SDL_SYSWM_ANDROID`.
- The runtime already exports `set_pad_buttons`, `set_pad_axis`, `set_pad_pressure`, and `set_audio_sink`. So input and audio have existing host-facing seams; the app still needs to drive them from Android controls and audio/window lifecycle.

References:
- https://github.com/snuri00/ps2-web-recomp/blob/333999564ad0fe84f24b504416869dc182404a61/runtime/include/ps2/system.hpp
- https://github.com/snuri00/ps2-web-recomp/blob/333999564ad0fe84f24b504416869dc182404a61/runtime/src/gs/gpu/gpu.hpp
- https://github.com/snuri00/ps2-web-recomp/blob/333999564ad0fe84f24b504416869dc182404a61/runtime/src/gs/gpu/gpu.cpp
- https://github.com/snuri00/ps2-web-recomp/blob/333999564ad0fe84f24b504416869dc182404a61/host/window.cpp
- wgpu-native surface API: https://webgpu-native.github.io/webgpu-headers/Surfaces.html

## Patch scope

The patch appends `Android` to both surface-kind enums, creates a WGPU surface from the supplied native-window pointer, and maps SDL's Android window system to that kind. It deliberately does not add an Android Activity, modify the CMake target into `libmain.so`, or add touch controls. Those require a separate end-to-end integration and must be built/tested against a working game runtime, not guessed at independently of the boot probe.

## Validation required after applying

1. `git diff --check` in the upstream checkout.
2. Confirm the actual pinned wgpu-native Android headers include `WGPUSurfaceSourceAndroidNativeWindow` and the matching `WGPUSType_SurfaceSourceAndroidNativeWindow` enum.
3. Build the `ps2runtime` target with Android NDK and the ARM64 wgpu-native release library.
4. Only after an Android host target is added, test SDL surface creation/destruction and resize on a device.

No Android build or device test was performed in this environment. This document explicitly does not claim otherwise.
