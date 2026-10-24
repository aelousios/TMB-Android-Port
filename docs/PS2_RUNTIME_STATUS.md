# Native PS2 runtime status

## Current evidence

The repository's previous GitHub Actions run failed at Android SDK setup before Gradle or APK compilation: `android-actions/setup-android@v3` invoked `sdkmanager tools`, but that legacy package is no longer served. The overlay upgrades the action to v4, explicitly skips additional packages in the setup step, and keeps the explicit `sdkmanager` installation of current packages. See the [action's package-input documentation](https://github.com/android-actions/setup-android/blob/main/README.md#additional-packages).

The Android project now stages the ELF `PT_LOAD` segments into a bounded model of the PS2 Emotion Engine's 32 MiB main RAM and 16 KiB scratchpad. It handles direct low-memory addresses plus KSEG0/KSEG1 aliases, zeros `p_memsz - p_filesz` bytes for BSS, and rejects segments outside the modeled address ranges. This is only ELF loading. No MIPS instruction executes.

The upstream [Re-Arc progress report for `SCUS_971.01`](https://github.com/OpenTrac/tmb_Re-Arc/blob/master/config/US/report.json), checked on 24 October 2026, reports:

- 3,606 total functions; 107 matched (about 2.97%).
- 1,221,468 bytes of code; 26,980 matched (about 2.21%).
- 100% data matched in the report.

No original disc image was available in this workspace, so the loader is currently verified against synthetic ELF fixtures rather than a real `SCUS_971.01` executable.

These figures describe byte/function matching against the original build, not whether the reconstructed code compiles or runs. They do show the source reconstruction is early-stage. Source-porting the complete game now would require a large number of unfinished functions as well as replacing console-specific services.

## Still required for a genuine native game port

- A CPU execution strategy for the Emotion Engine MIPS III/R5900 instruction set. A native-source port would prefer source reconstruction; a static/dynamic recompiler is a separate design and must not be mislabeled as a source port.
- Correct handling of delay slots, exceptions, COP0, TLB/cache behavior, and the R5900-specific instruction set.
- Vector Unit 0 and Vector Unit 1 behavior/microcode, DMA chains, GS registers and command lists, interrupts, timers, IPU, SIF, IOP/SPU2/audio, controller I/O, memory cards, and disc/filesystem behavior.
- A portable renderer/audio/input/platform layer and a known-good comparison harness against the original game.
- A real game-image test on the targeted US NTSC-U executable, which this workspace has not yet been given.

## Honest milestone definition

The memory-stage milestone is complete only when synthetic test ELFs can load with deterministic entry address, segment bytes, zero-filled BSS, aliases, and rejection of unsupported memory locations. It is not a game boot. A game boot requires the supplied original image to reach an identifiable original scene and accept gameplay input without running a console emulator APK.
