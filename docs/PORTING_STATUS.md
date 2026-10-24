# Porting status and technical boundary

## Current milestone: native Android shell and test harness

The application package has a Kotlin/Android lifecycle shell and an NDK C++ shared library. The C++ renderer runs through OpenGL ES 2.0. The current arena geometry, target movement, hit detection and car handling are newly written test behavior. They are intentionally not represented as original game behavior.

The ISO inspector reads an already-open file descriptor without changing the selected file. It checks for an ISO-9660 primary volume descriptor at sector 16, walks directory records with depth and entry limits, locates `SCUS_971.01`, and validates its ELF32/little-endian/MIPS header and program-header table. It bounds-checks each PT_LOAD segment, totals file-backed and in-memory bytes, and verifies that the entry point is covered by a loadable segment. This is metadata inspection only: it does not extract, map, patch or execute the MIPS code.

## Next completed engineering step: executable metadata validation

The app now distinguishes “the filename exists and begins with ELF” from a structurally plausible PS2 MIPS ELF executable. `ps2_elf.cpp` validates class, endianness, type, machine, header dimensions, the program-header table, PT_LOAD file/memory ranges, alignment and entry-point coverage. Tests use a small synthetic ELF and do not contain any commercial game data. This gives future loader work reliable executable metadata; it is not a CPU or PS2 runtime.

## Why this does not yet run Twisted Metal: Black

The upstream Re-Arc repository is a matching decompilation effort. Its build instructions explain that it starts with the executable from the US PS2 image, splits that binary into segments, and builds a reconstructed PS2 executable. The same repository explicitly says it is a work in progress and asks developers not to port it until it is closer to completion.

Even a fully matching source reconstruction would not instantly be a native mobile game. PS2 executable code expects the console's memory map, graphics synthesizer, vector units and microcode, sound hardware, interrupts, timing, file system and other platform services. A native port needs an engine/platform layer that implements those contracts or replaces the game-facing systems. No such full Android layer currently exists here.

## Rules for future integration

- Never commit an ISO, BIOS-like firmware, extracted commercial assets, audio, texture data or a game executable to this repository.
- Keep all test content original and clearly labelled as placeholder content.
- Record the exact source revision and executable variant for every experiment.
- Keep a known-good build and automated tests before changing generated/decompiled code.
- Make the game-image selector and runtime separate concerns: validating an image is not equivalent to booting it.
- Do not silently fall back to an emulator. The project's target is a native engine/runtime port.

## Completion criteria for an eventual first boot

A milestone should not be called a game boot until the app reads the user-supplied image, initializes the real reconstructed game runtime, reaches a known original-game scene, handles input in the original game, and produces meaningful logs for failures. A demo arena or splash screen alone does not count.
