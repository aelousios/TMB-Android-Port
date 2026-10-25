# Android ISO selection and private staging

The main app's document picker copies the selected file to app-private storage at `noBackupFilesDir/tmb/selected-disc.iso`. It uses a temporary `.part` file and replaces the previous staged file only after the new copy completes. The staged file is then inspected by the existing ISO-9660/ELF validator. The app records the selected URI, local staged path and file size in app preferences and logs `TMB_ISO_STAGED_PRIVATE`.

The disc file stays inside the app sandbox; this code does not upload it to a server or commit it to Git. A sufficiently large free-storage allowance is required for the copy.

**Important:** this is reliable ISO staging and validation, not game execution. PS2 Web Recomp's `port.sh` statically translates the disc's EE executable into generated native C++. That build-time, game-specific code still has to be generated on a development computer, integrated with the Android native host and tested. The Android app is not yet able to compile/boot arbitrary PS2 games just because the user selected an ISO.
