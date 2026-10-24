# TMB Android host smoke test

This separate Gradle module packages the current SDL2 Android Activity and the ARM64 `libmain.so` built from the pinned PS2 Web Recomp runtime. On launch, it shows a diagnostic screen and verifies SDL's Android window/event loop. It explicitly does **not** load or emulate Twisted Metal: Black; no game ISO or game-specific generated code is included.

Build from the repository root with:

```bash
gradle --no-daemon :android-host-smoke:assembleDebug
```

APK output:

`android-host-smoke/build/outputs/apk/debug/android-host-smoke-debug.apk`

This is a packaging/startup milestone only. Game boot still requires an owned ISO, a game-specific `scripts/port.sh` run, and Android argument/storage integration.
