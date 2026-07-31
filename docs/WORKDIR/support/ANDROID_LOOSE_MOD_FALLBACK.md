# Android Loose-Mod Fallback

## Purpose

This handoff records the Android repair that restores selected-mod loose-file
overrides without reintroducing the filesystem heap corruption previously seen
on device. It covers engine lookup behavior only. It neither includes nor
distributes game or mod assets.

The source case was XenoForce. Its `Data/INI/Locomotor.ini` contains a valid
`SDF-1Locomotor` definition, and the mod Object INIs reference that definition.
The same mod runs on PC. Android was not reading the loose INI override.

## Diagnosis

`loadMods()` registers the selected mod directory as a LocalFileSystem fallback
root. Android had an early return in
`StdLocalFileSystem::fixFilenameFromWindowsPath()` that avoided the unsafe
case-insensitive filesystem traversal, but also skipped every fallback probe.
As a result, the loose selected-mod `Data/INI/Locomotor.ini` could not resolve.
Retail `INIZH.big` supplied `Locomotor.ini` instead, and the mod then failed to
resolve `SDF-1Locomotor`.

## Unsafe-Path Trap

The first repair put `std::filesystem::path::operator/` and `exists()` back
before the Android early return. On device, that path again triggered Scudo heap
corruption in `basic_string::append` during `GameLODManager::init`.

Do not use `std::filesystem::path::operator/`, `exists()`, or
`directory_iterator` for Android fallback lookup in this code path. The Android
fallback must remain on fixed buffers, `snprintf`, and POSIX `stat()` probes.

## Non-Regression Contract

The Android lookup order is:

1. GameData-root loose files.
2. Selected-mod loose files.
3. Mod and retail archives.

The primary GameData root remains first, so an intentionally installed loose
GameData override is not displaced by a selected-mod file. A selected-mod loose
file must resolve before the same logical path from either a mod archive or a
retail archive. The Android-safe branch only joins and probes paths through the
fixed-buffer `snprintf` plus `stat()` implementation.

## Automated Regression Test

`tests/android_fallback_test.cpp` compiles the real
`StdLocalFileSystem.cpp` with `__ANDROID__` for that translation unit. It checks
that:

- A loose `Data/INI/Locomotor.ini` under a registered selected-mod root is found.
- A missing file is not reported as present.
- The primary GameData root wins when both primary and mod roots contain the
  same loose file.
- A loose selected-mod file wins through the public `FileSystem::openFile`
  boundary when an archive also provides the same logical path.

Run the focused regression test from the WSL worktree:

```bash
docker run --rm \
  -v "/home/rashid/.projects/GeneralsZH-Android:/work" \
  -w /work generalsx/linux-builder \
  ./build/linux64-deploy/tests/android_fallback_test
```

## Device Evidence

The release APK was exercised on a Lenovo TB322FC running Android 16 with
`Mods/XenoForce` selected. The engine reached `execute()` and the application
remained focused in `GameActivity`. The captured log contained no Scudo error,
fatal signal, or INI-field failure.

This establishes engine-level activation and runtime safety for the fallback.
It does not establish the presentation result. The green Mods button and the
mod content were not physically inspected.

## Safe Rebuild and Retest

Build the native target, package the APK, then use the app's existing legal
content and Storage Access Framework workflow. Do not write into
`Android/data` with ADB.

```bash
cmake --preset android-vulkan
cmake --build build/android-vulkan --target z_generals
./scripts/build/android/package-android-zh.sh
adb install -r android/app/build/outputs/apk/release/app-release.apk
```

On the Lenovo TB322FC, launch the installed APK with the existing selected mod,
then capture a bounded log after launch. Confirm that startup reaches `execute()`
and that the log has no Scudo, fatal-signal, or INI-field failure. Complete the
remaining physical check by observing the green Mods button and mod content.

For the concise engineering record, see
[`android.md` §10.12](../../../android.md#1012-loose-mod-ini-fallback-restored-without-filesystem-heap-corruption).
