# Twisted Metal: Black — Native Port Lab

**Status: foundation prototype, not a playable Twisted Metal: Black port yet.**

This is an Android Studio project for beginning a native port investigation. It is **not an emulator wrapper**. The app uses a Kotlin Android shell and an NDK C++/OpenGL ES renderer. Right now, that renderer is an original placeholder driving arena used to validate the app lifecycle, touch input, native rendering and ISO-file inspection. It does not contain or run the commercial game's engine, assets, levels, audio, physics, AI or gameplay.

No game ISO, copyrighted game assets, extracted executable or decompilation source is bundled.

## What's implemented

- Android landscape app, full-screen and touch-friendly.
- NDK native library loaded through JNI.
- OpenGL ES 2.0 chase-camera scene with an original placeholder vehicle, obstacle course and moving target.
- Hold controls for steering, acceleration, reverse/braking, firing and special attack. These controls affect only the placeholder demo.
- Android document picker to select a local image without copying it into the app.
- Read-only ISO-9660 inspection for the US NTSC-U executable `SCUS_971.01`, checking the ELF signature. The app never writes to the image.
- Host-side tests for valid, malformed and wrong-game ISO images.
- GitHub Actions workflow to compile and publish a **debug APK build artifact** when this folder is pushed to a GitHub repository.

## What is not implemented

The selected ISO is only inspected. There is no boot option because this project does not yet have a portable Twisted Metal: Black runtime. The native scene is a test harness, not a recreation of the game.

The upstream [Twisted Metal: Black Re-Arc project](https://github.com/OpenTrac/tmb_Re-Arc) reconstructs code matching the original PS2 executable. Its README says the project is unfinished and asks people not to port it to other platforms until it is closer to completion. It currently builds a PS2 executable, not an Android engine. A matching decompilation is an important reverse-engineering asset, but it does not by itself replace PS2 graphics, audio, VU microcode, system services, file loading or controller behavior.

## Build the Android app

Requirements: Android Studio with Android SDK Platform 35, Android NDK `27.0.12077973`, CMake `3.22.1`, and JDK 17 or newer. Open this folder in Android Studio and let Gradle sync. Then choose **Build → Build APK(s)**. The debug APK should be produced at:

`app/build/outputs/apk/debug/app-debug.apk`

The CI workflow in `.github/workflows/android-apk.yml` installs the required SDK components and uploads `app-debug.apk` as an artifact. It does not require a game ISO to compile this foundation.

## Run the native ISO parser tests on Linux/macOS

From the project root:

```sh
g++ -std=c++17 -Wall -Wextra -Werror \
  -Iapp/src/main/cpp \
  tests/iso9660_test.cpp app/src/main/cpp/iso9660.cpp \
  -o /tmp/tmb_iso_test
/tmp/tmb_iso_test
```

Expected output: `iso9660 tests passed`.

## ISO support note

The current inspector expects a plain ISO-9660 image containing `SCUS_971.01`, as described by the upstream project for the US NTSC-U version (`SCUS-97101`). CHD and BIN/CUE are not parsed by this inspector. A successful check means only that the expected executable entry and ELF signature were found; it does **not** mean the app can launch the game.

## Next engineering milestones

1. Confirm the reconstruction's supported executable variants, toolchain and build reproducibility.
2. Map the PS2 services and runtime dependencies used by the game, keeping original assets outside the repository.
3. Define a portable platform boundary for graphics, audio, timing, input, file I/O and controller state.
4. Start replacing individual PS2-only systems with native Android implementations and verify each against known game behavior.
5. Only after enough of that runtime exists, replace the placeholder renderer and add a genuine game boot path.

The difficult work is the runtime and hardware compatibility layer, not the ISO picker or the APK packaging.
