# Game boot dependency (important)

The APK's file picker and its PS2 ELF inspector are not a PS2 execution engine. The validated Android WebGPU smoke test currently boots only a diagnostic entry point plus a generated-game stub.

The chosen PS2 Web Recomp route runs a host-side porting pipeline against a specific PS2 disc executable and statically generates C++ that is then compiled into the native app. Therefore, a future APK can let the user select the disc image at runtime for game data and file access, but this repository cannot yet truthfully offer a working "select ISO and play" flow until the matching game-specific generated code is produced, built, and integrated. No ISO is currently available in the development environment, so the TMB boot probe and game-compatibility fixes are blocked on the user's disc.

Do not remove the existing "TECH DEMO ONLY" / "not yet integrated" messaging until the real game has been observed booting and accepting input on device.
