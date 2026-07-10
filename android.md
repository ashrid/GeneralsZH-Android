# Android Port: Status, Findings, and Fixes

**Last updated:** 2026-07-07 (main menu rendering achieved!)
**Target:** C&C Generals Zero Hour running natively on Android tablets (arm64-v8a)
**Approach:** Native port only (no Box64/emulation)

---

## 1. What We're Trying to Do

Get the full C&C Generals Zero Hour game engine (~500k LOC C++) running
**natively** on an Android tablet. This means:

- **DXVK (D3D8 → Vulkan)** translating the engine's DirectX 8 calls to Vulkan
  on Android's Adreno/Mali GPUs — the first-ever DXVK build for Android aarch64.
- **SDL3** as the windowing/input layer (replacing Win32 API).
- **OpenAL** for audio (replacing Miles Sound System).
- **FFmpeg stubbed** (vcpkg ffmpeg:arm64-android is broken — microsoft/vcpkg#33963).
- The engine cross-compiled with **Android NDK r27** + CMake, producing a
  `libmain.so` that SDL's `nativeRunMain` JNI entry point dlopens.
- **64-bit only** (arm64-v8a). No 32-bit path.

The goal is the game's **main menu rendering on the tablet screen**, proving
the full init pipeline works end-to-end: filesystem → BIG archives → INI
parsing → subsystem stores → DXVK device creation → rendering.

---

## 2. What's Been Achieved (The Milestones)

| Milestone | Status |
|-----------|--------|
| DXVK builds natively for Android aarch64 | ✅ Done (first-ever) |
| DXVK d3d8 clear test renders on Adreno 830 | ✅ Proven on real hardware |
| Full 500k LOC engine cross-compiles (20,811 symbols) | ✅ All subsystems compile |
| `libmain.so` produced for arm64-v8a | ✅ |
| Full APK packaged (libmain.so + DXVK + SDL3 + deps + fonts) | ✅ |
| 2GB GameData pushed to device external storage | ✅ |
| Engine finds and mounts BIG archives | ✅ |
| INI parsing works (GameData, Science, Terrain, etc.) | ✅ |
| Audio init passes (ZH music loaded with override fix) | ✅ |
| WeaponStore parses past FLESHY_SNIPER | ✅ (bugfix applied) |
| LocomotorStore loads ZH locomotors | ✅ (override fix applied) |
| Engine reaches DXVK device creation | ✅ (format + windowed fixes) |
| **Main menu renders on tablet** | **✅ Done! (07 Jul 2026)** |
| Menu button text visible (font rendering) | ✅ Done (APK asset extraction) |
| Touch input works (tap, drag, pinch) | ✅ Done (SAGE_MOBILE already implemented) |
| Audio playback | ⏳ Verify |
| Gameplay / full session | ⏳ Future |

---

## 3. Build & Deploy Workflow

### 3.1 Build the engine

```bash
# Configure (one-time)
cmake --preset android-vulkan

# Build
cmake --build build/android-vulkan --target z_generals
```

Key build flags:
- `-DRTS_GAMEMEMORY_ENABLE=OFF` — engine's custom DMA conflicts with libc++
  `c++_shared` (operator delete crash). Uses plain malloc/free.
- `-DRTS_BUILD_OPTION_FFMPEG=OFF` — FFmpeg stubbed for Android.
- `-DRTS_CRASHDUMP_ENABLE=OFF` — no minidump on Android.

### 3.2 Strip + package the APK

> **Use the automated packager:** `bash scripts/build/android/package-android-zh.sh [--install]`
> handles strip + staging + align + sign (+ optional install) in one step. The manual
> zipalign/apksigner flow below is preserved as historical reference for environments
> without the script; its paths (`build/android-spike`, `build/android-game`,
> `darwin-x86_64`) reflect the pre-automation state and are **not kept current**.

The APK is assembled from a staging directory at `build/android-spike/apk/`:

```bash
NDK="${HOME}/Library/Android/sdk/ndk/27.1.12297006"
STRIP="${NDK}/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-strip"
STAGING="build/android-spike/apk"
BUILD_TOOLS="${HOME}/Library/Android/sdk/build-tools/36.1.0"
KEYSTORE="${HOME}/.android/debug.keystore"

# Strip debug symbols
"${STRIP}" --strip-debug -o "${STAGING}/lib/arm64-v8a/libmain.so" \
    build/android-game/GeneralsMD/Code/Main/libmain.so

# Replace libmain.so in the unsigned APK base
cp -f build/android-game/GeneralsZH-full-unsigned.apk \
      build/android-game/GeneralsZH-full-aligned.apk.tmp
( cd "${STAGING}" && zip build/android-game/GeneralsZH-full-aligned.apk.tmp \
    "lib/arm64-v8a/libmain.so" )

# Align + sign
"${BUILD_TOOLS}/zipalign" -f -p 4 \
    build/android-game/GeneralsZH-full-aligned.apk.tmp \
    build/android-game/GeneralsZH-full-aligned.apk
"${BUILD_TOOLS}/apksigner" sign \
    --ks "${KEYSTORE}" --ks-pass pass:android --key-pass pass:android \
    --out build/android-game/GeneralsZH-full.apk \
    build/android-game/GeneralsZH-full-aligned.apk
```

### 3.3 Install + run + capture logs

```bash
adb install -r build/android-game/GeneralsZH-full.apk
adb logcat -c
adb shell am start -n me.generalsx.spike/.SpikeActivity
sleep 20
adb logcat -d -s GeneralsX:V | tail -60
```

### 3.4 Game data on device

GameData lives in external storage:
```
/storage/emulated/0/Android/data/me.generalsx.spike/files/GameData/
  Data/
    INI.big
    INIZH.big
    Audio.big
    AudioZH.big
    ... (all .big archives)
  SagePatch.ini
```

The engine `chdir()`s into `<external>/GameData` on launch (see `SDL3Main.cpp`).

---

## 4. Bugs Found and Fixed

### 4.1 ✅ FLESHY_SNIPER DamageType compiled out for ZH

**Symptom:** Engine crashed during WeaponStore init:
```
Error parsing INI file 'Data\INI\Weapon.ini' (Line: 'Weapon CINE_USAPathfinderSniperRifle')
```

**Root cause:** The `DAMAGE_FLESHY_SNIPER` enum value and its name in
`DamageTypeFlags::s_bitNameList[]` were gated behind `#if RTS_GENERALS` only:

```cpp
// Damage.h — BEFORE (broken):
#if RTS_GENERALS
    DAMAGE_FLESHY_SNIPER = 31,
#endif

// Damage.cpp — BEFORE (broken):
#if RTS_GENERALS
    "FLESHY_SNIPER",
#endif
```

The ZH build (`RTS_ZEROHOUR=1`, `RTS_GENERALS` undefined) compiled out
`FLESHY_SNIPER` from the enum names list. But the ZH engine loads the
**base Generals** `INI.big`, whose `Weapon.ini` references `DamageType =
FLESHY_SNIPER` (e.g. `CINE_USAPathfinderSniperRifle`). The INI parser's
`scanIndexList("FLESHY_SNIPER", s_bitNameList)` found no match → threw
`INI_INVALID_DATA` → crash.

**Fix:** Include `FLESHY_SNIPER` for ZH too, since ZH loads base data:

```cpp
// Damage.h — AFTER:
DAMAGE_FLESHY_SNIPER = 31;  // no #if guard

// Damage.cpp — AFTER:
"FLESHY_SNIPER",  // no #if guard
```

The `/*= 32*/` commented-out values on subsequent entries confirm the
developers designed the enum so FLESHY_SNIPER occupies slot 31 regardless.

**Files changed:**
- `Core/GameEngine/Include/GameLogic/Damage.h`
- `Core/GameEngine/Source/GameLogic/System/Damage.cpp`

---

### 4.2 ✅ BIG archive file override (base vs ZH)

**Symptom:** LocomotorStore only parsed 40 locomotors from the base
Generals `Locomotor.ini`, missing all ZH-specific locomotors like
`SpectreGunshipTransitLocomotor`. When Object INI files referenced these
missing locomotors, the engine crashed:
```
Error parsing INI file 'Data\INI\Object\airforcegeneral.ini'
  (Line: 'Object AirF_AmericaJetSpectreGunship1')
```

**Root cause:** On a Complete Edition install, both base Generals
(`INI.big`) and Zero Hour (`INIZH.big`) archives are in the same `Data/`
directory. The BIG file loader `loadBigFilesFromDirectory()` used
`overwrite=FALSE` by default:

```cpp
// StdBIGFileSystem.cpp — BEFORE (broken):
static Bool tryLoadBigFiles(..., Bool overwrite = FALSE) {
    ...
    fileSystem->loadBigFilesFromDirectory(directory, "*.big", overwrite);
}
```

Files are loaded in alphabetical order (`INI.big` before `INIZH.big`).
With `overwrite=FALSE`, the FIRST-loaded version (base `INI.big`) is
inserted at the front of the `ArchivedFileLocationMap` multimap. The
`getArchiveFile()` function returns `range.get()->second` — the first
entry. So the base `Locomotor.ini` (40 entries, no ZH locomotors) always
won over the ZH version (182 entries).

**Fix:** Default `overwrite=TRUE` so later-loaded ZH archives override
base files:

```cpp
// StdBIGFileSystem.cpp — AFTER:
static Bool tryLoadBigFiles(..., Bool overwrite = TRUE) {
    ...
}
// Also: loadBigFilesFromDirectory("", "*.big", TRUE) for CWD fallback.
```

With `overwrite=TRUE`, INIZH.big's files are inserted at the **front** of
the multimap, so `getArchiveFile()` returns the ZH version.

**Side effect:** Audio music check now passes (`musicLoaded=1`) because
the ZH audio archives are correctly prioritized.

**Files changed:**
- `Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFileSystem.cpp`

---

### 4.3 ✅ Multimap override precedence (resolving the 40/182 locomotor parsing issue)

**Symptom:** Only 40 of ~182 Zero Hour locomotors were being parsed. The parsing stopped after `BlimpLocomotor`. `SpectreGunshipTransitLocomotor` (defined in the Zero Hour `Locomotor.ini` in `INIZH.big`) was not loaded.

**Root cause:** The base Generals `Locomotor.ini` (inside `INI.big`) was still prioritized by the file system over the Zero Hour `Locomotor.ini` (inside `INIZH.big`). While `overwrite=TRUE` was passed, `ArchiveFileSystem::loadIntoDirectoryTree` used `std::multimap::insert(find(token), ...)` to insert the overriding file. In C++11 and later, `multimap::insert` with a hint does not guarantee that the new element is inserted at the beginning of the equal range (it is implementation-defined, and typically appends at the end of the equivalent key range). As a result, the base Generals version remained first in the `equal_range` and was returned by `getArchiveFile()`, while the Zero Hour version was placed second.

**Fix:** When `overwrite` is `TRUE`, we query all existing elements for that file `token` in the multimap, save them in a vector, erase them from the multimap, insert the new override (which becomes the first element), and then re-insert the erased elements. This guarantees that the new override element is at the beginning of the equal range (index 0) and takes precedence, while still preserving older instances at subsequent indices (which is essential for merging string tables like CSFs).

**Files changed:**
- `Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp`

---

### 4.4 ✅ LanguageRegistry crash on fresh install (no Options.ini)

**Symptom:** Engine crashed during early init in `LanguageRegistry::init()`
on a fresh Android install where no `Options.ini` exists.

**Root cause:** Without `Options.ini`, the engine has no language setting
configured. The language registry tries to look up a registry entry or
configuration file that doesn't exist and throws an exception.

**Fix:** Added a fallback to default English when registry entries or
configuration files are missing.

**Files changed:**
- `GeneralsMD/Code/GameEngine/Source/Common/GameEngine.cpp`

---

### 4.5 ✅ Global allocator / deallocator incompatibility (heap corruption)

**Symptom:** Random crashes in `operator delete` / `operator delete[]` —
the engine's custom memory allocator (`TheDynamicMemoryAllocator`) tried
to free memory that was allocated by the standard system allocator
(via `::malloc`), causing heap corruption.

**Root cause:** External libraries (OpenAL, libc++ containers) allocate
memory using the standard system allocator, but the engine intercepted
global `operator delete` and routed ALL deallocations through its custom
pool allocator. When these external allocations were freed through the
engine's custom path, the pool allocator corrupted its internal state.

**Fix:** Added a magic cookie field (`m_dmaMagicCookie = 0x47454d53`) in
`MemoryPoolSingleBlock::initBlock()`. Updated global `operator delete`
and `operator delete[]` to check for this cookie before routing to
`TheDynamicMemoryAllocator->freeBytes()`. If the cookie is not present,
the memory is freed via standard `::free()` instead.

**Files changed:**
- `Core/GameEngine/Include/Common/System/GameMemory.h` — added cookie field + getter
- `Core/GameEngine/Source/Common/System/GameMemory.cpp` — cookie init + delete routing

---

### 4.6 ✅ DXVK CreateDevice fails with D3DERR_NOTAVAILABLE (BackBufferFormat=UNKNOWN)

**Symptom:** `D3DInterface->CreateDevice()` failed with HRESULT `0x8876086A`
(`D3DERR_NOTAVAILABLE`). The engine logged `BackBufferFormat=0`
(`D3DFMT_UNKNOWN`) in the `_PresentParameters`.

**Root cause:** Two interacting issues:

1. **Windowed presentation mismatch**: DXVK on non-Windows platforms always
   needs `_PresentParameters.Windowed = TRUE` (there's no Win32 fullscreen
   concept). The existing Linux/macOS fix for this correctly set
   `Windowed = TRUE`, but the `#if` guard excluded Android/iOS:
   ```cpp
   // BEFORE — excluded Android:
   #if !defined(_WIN32) && !defined(__ANDROID__) && ...
   _PresentParameters.Windowed = TRUE;
   ```

2. **Format selection path mismatch**: The engine's format selection code
   branches on `if (IsWindowed)`. The engine's `IsWindowed` variable was
   `false` (game defaults to fullscreen), so it entered the fullscreen
   format-selection path which calls `Find_Color_And_Z_Mode()`. On Android,
   this function fails because there are no enumerated fullscreen display
   modes, leaving `BackBufferFormat = D3DFMT_UNKNOWN` (0).

   Meanwhile, `_PresentParameters.Windowed` was forced to `TRUE` for DXVK,
   so DXVK received `Windowed=1` with `BackBufferFormat=UNKNOWN` — an
   invalid combination that DXVK correctly rejects.

**Fix (3 changes in `dx8wrapper.cpp`):**

1. Simplified the windowed presentation guard to `#ifndef _WIN32` so ALL
   non-Windows platforms (including Android/iOS) force
   `_PresentParameters.Windowed = TRUE`.

2. Changed the format-selection branch to always use the windowed code path
   on non-Windows (`#ifndef _WIN32 { #else if (IsWindowed) { #endif`),
   since DXVK always operates in windowed presentation mode.

3. Added a fallback: if `GetAdapterDisplayMode()` returns `D3DFMT_UNKNOWN`
   on Android (no desktop mode), default to `D3DFMT_X8R8G8B8` (32-bit).

**Result:** `CreateDevice` succeeds with:
```
BackBufferFormat=21 (D3DFMT_A8R8G8B8)  Windowed=1
AutoDepthStencilFormat=75 (D3DFMT_D24S8)
```

**Files changed:**
- `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp`

---

### 4.7 ✅ Menu button text missing (font extraction from APK assets)

**Symptom:** The main menu rendered correctly (background scene, button
outlines) but button text was invisible/missing. No text appeared on any
UI element.

**Root cause:** The engine's FreeType font locator
(`FontCharsClass::Locate_Font_FontConfig` in `render2dsentence.cpp`)
probes `<CWD>/fonts/<name>.ttf` via `access()`. On Android, CWD is
`<storage>/GameData/`. The four font files (Liberation fonts renamed to
Windows names: `arial.ttf`, `arialbold.ttf`, `couriernew.ttf`,
`timesnewroman.ttf`) were bundled as **APK assets** in `assets/fonts/`.

**Android APK assets are invisible to `access()`/`fopen()`** — they're
not real filesystem paths. No code existed to extract them.

**Fix:** Added native font extraction in `SDL3Main.cpp` that runs on
first launch:
1. Creates `<storage>/GameData/fonts/` directory
2. Obtains `AAssetManager` via JNI (`SDL_GetAndroidJNIEnv` +
   `SDL_GetAndroidActivity` → `getAssets()` → `AAssetManager_fromJava`)
3. For each font file: `AAssetManager_open` → `AAsset_read` loop →
   writes to filesystem
4. Skips extraction if `fonts/arial.ttf` already exists (idempotent)

**Files changed:**
- `GeneralsMD/Code/Main/SDL3Main.cpp` — font extraction logic
- `GeneralsMD/Code/Main/CMakeLists.txt` — link `libandroid` for AAssetManager

---

### 4.8 Touch Input — Already Working (SAGE_MOBILE)

**Finding:** Touch input was already fully implemented via the `SAGE_MOBILE`
macro (defined when `__ANDROID__` is set). The implementation in
`SDL3GameEngine.cpp` includes:

- A `TouchState` gesture state machine: IDLE → PENDING → TAP/DRAG/LONGPRESS/PAN
- `handleTouchEvent()` processing `SDL_EVENT_FINGER_DOWN/MOTION/UP/CANCELED`
- `sendSyntheticMouse()` injecting synthetic `SDL_EVENT_MOUSE_*` events
  through the same `SDL3Mouse::addSDLEvent()` path real mice use
- Gestures: 1-finger tap → left click, 1-finger drag → drag-box,
  1-finger long-press → right click, 2-finger drag → camera pan,
  2-finger pinch → zoom wheel

**Verification:** Tapping the screen via `adb shell input tap` produces
`FINGER_DOWN` → `FINGER_UP` events that correctly flow through the
synthetic mouse pipeline. Tapping menu buttons triggers UI responses
(screen content changes).

**No code changes needed** — the existing implementation works correctly.

### 4.9 ✅ DXVK submodule must be init'd recursively (nested header submodules)

The `fbraz3-dxvk` fork nests four header submodules under itself:
- `include/native/directx` → `Joshua-Ashton/mingw-directx-headers` (provides `d3d8.h`, `d3d9.h`)
- `include/spirv` → `KhronosGroup/SPIRV-Headers`
- `include/vulkan` → `KhronosGroup/Vulkan-Headers`
- `subprojects/libdisplay-info` → `libdisplay-info`

A plain `git submodule update --init references/fbraz3-dxvk` leaves these uninitialized, so `include/native/directx/` is empty and the arm64 build fails at `Generals/Code/CompatLib/Include/d3dx8core.h:12` with `fatal error: 'd3d8.h' file not found`. **Fix:** always init recursively — `git submodule update --init --recursive references/fbraz3-dxvk`. All repo docs/error messages now say `--recursive` (commit `474bf2d39`).

### 4.10 ✅ FFmpeg host-contamination in the android-vulkan preset

The `android-vulkan` preset had `RTS_BUILD_OPTION_FFMPEG=ON`, violating AGENTS.md (FFmpeg must be `OFF` for Android — vcpkg `ffmpeg:arm64-android` is broken, microsoft/vcpkg#33963; video is stubbed via the Bink stub). With `ON`, the guarded `pkg_check_modules(FFMPEG REQUIRED ...)` at `Core/GameEngineDevice/CMakeLists.txt:289` found the **host** x86_64 FFmpeg via pkg-config and injected `/usr/include/x86_64-linux-gnu` into the arm64 compile commands, pulling host glibc `sys/cdefs.h` → `__GNUC_PREREQ` macro cascade → build failure. The preset also cleared `PKG_CONFIG_PATH` but not `PKG_CONFIG_LIBDIR`, so pkg-config fell back to host `/usr/lib/x86_64-linux-gnu/pkgconfig` (also found DBUS). **Fix** (commit `26c6db4e0`): preset `RTS_BUILD_OPTION_FFMPEG=OFF` (skips the guarded find entirely) + `PKG_CONFIG_LIBDIR=""` in the preset environment (fully isolates the Android cross-compile from host pkg-config).

### 4.11 ✅ DXVK Meson + packager build-chain fixes (2026-07-10)

The DXVK-from-source build (Meson cross-compile) and the APK packager each had latent bugs that surfaced when first running the full `cmake --preset android-vulkan` → `package-android-zh.sh` pipeline. All fixed + committed:

| Bug | Fix | Commit |
|-----|-----|--------|
| DXVK Meson cross-file had `aarch64-linux-android-clang` (no API level; NDK r27 ships `aarch64-linux-android24-clang`) — `dx8.cmake` set `DXVK_ANDROID_API` but the template placeholder was `@ANDROID_API@` (mismatch → empty) | Rename var to `ANDROID_API` to match the template | `66a66cab5` |
| DXVK `meson.build:156` calls `dependency('SDL3')` (capital) → pkg-config queries `SDL3.pc`, but the FetchContent shim was `sdl3.pc` (lowercase, invisible) | Add a capital `SDL3.pc` twin to the shim | `432fb054a` |
| DXVK needs `glslangValidator` (GLSL→SPIR-V), not installed | No-sudo: `apt download glslang-tools` + `dpkg-deb -x` to `~/.local/bin` (on PATH) | (env, not committed) |
| `dxvk_adapter.cpp` uses `VK_KHR_portability_subset` (Vulkan beta ext, in `vulkan_beta.h`, gated behind `VK_ENABLE_BETA_EXTENSIONS`) — §5 listed this as a fix but it was never applied. Must live in DXVK `meson.build` (`add_project_arguments`) NOT the cross-file `c_args`, because `meson setup --reconfigure` does NOT re-read cross-file `c_args` (confirmed: `build.ninja` had 0 occurrences) but DOES re-read `meson.build` | Add to `Patches/dxvk-android.patch` (regenerated from `git diff`) | `9c0219ab3` |
| Packager: OpenAL `.so` at `_deps/openal_soft-build/` (FetchContent), not `${BUILD_DIR}/openal-soft/` (silently missed → dlopen crash) | Fix the path | `bd76897f0` |
| Packager: `[[ ! -d .../.git ]]` — a submodule's `.git` is a FILE (gitlink), not a dir, so `-d` always failed | Use `-e` | `96cb37a13` |
| `mergeReleaseNativeLibs`: duplicate `libSDL3.so` (packager stages into jniLibs AND Gradle's `externalNativeBuild` emits as IMPORTED target) | `packagingOptions.jniLibs.pickFirst('**/*.so')` | `14db72004` |
| Gradle `JdkImageTransform` needs `jlink` (JDK-only); default `java` was a JRE (no jlink) — `JAVA_HOME` unset | Packager auto-detects a JDK with `bin/jlink` | `3342dad6e` |

**Result:** `cmake --build build/android-vulkan --target z_generals` builds `libmain.so`; `dxvk_d3d8_install` builds `libdxvk_d3d8.so`/`d3d9.so`; `package-android-zh.sh` produces a signed `app-release.apk` (30 MB). WSL note: background builds need `setsid` (nohup/disown get killed on shell exit); use `.ninja_lock` (not `pgrep`) as the running-build signal (pgrep self-matches the checking shell).

---

## 5. Bugs Found and Fixed (Earlier in the Port)

These were resolved in prior sessions and are documented for reference:

| Bug | Fix |
|-----|-----|
| DXVK `-msse` on aarch64 | Gate SSE flags behind x86 in meson.build |
| `VK_ENABLE_BETA_EXTENSIONS` | Add to DXVK NDK cross-file |
| DXVK SDL3 WSI soname (`libSDL3.so.0` → `libSDL3.so`) | Patch `wsi_platform_sdl3.cpp` |
| `pthread_cancel` missing in bionic | Stub C file + warning suppression |
| `sys/timeb.h` missing | Guard with `#if !defined(__ANDROID__)` |
| `std::from_chars` float missing in NDK libc++ | Disable `USE_STD_FROM_CHARS_PARSING` for Android |
| FFmpeg missing | `FFmpegAndroidStub.h` + `FFmpegFileStub.cpp` |
| DMA operator delete crash | `RTS_GAMEMEMORY_ENABLE=OFF` (now fixed properly — see 4.5) |
| `GlobalData::BuildUserDataPathFromRegistry` crash | Return `"./"` on Android |
| Audio music check forcing quit | Bypass `isMusicAlreadyLoaded()` check on Android |
| `glob()` requires API 28+ | Guard `FilterSoftwareVulkanICDs` with `#if !defined(__ANDROID__)` |

---

## 6. Key Diagnostic Technique: Logcat Instrumentation

The engine's INI parser has a generic `catch(...)` that wraps every parse
failure into a uniform "Error parsing INI file" message, hiding the real
exception. To debug, we added targeted `__android_log_print` diagnostics:

### 6.1 INI block/field failure logging (`INI.cpp`)

```cpp
#if defined(__ANDROID__)
catch (const std::exception& ex) {
    GX_INI_LOG("INI BLOCK FAILED: block='%s' excType='%s' what='%s'",
        token, typeid(ex).name(), ex.what());
    ...
} catch (...) {
    GX_INI_LOG("INI BLOCK FAILED (non-std): block='%s'", token);
    ...
}
#endif
```

This revealed that the exceptions are **non-std** (thrown as `int` enum
values like `INI_INVALID_DATA`), not `std::exception`.

### 6.2 scanIndexList unmatched token logging (`INI.cpp`)

```cpp
#if defined(__ANDROID__)
// In scanIndexList, when a token isn't found:
GX_INI_LOG("scanIndexList: token '%s' NOT FOUND in list:", token);
for (ConstCharPtrArray name = nameList; *name; name++, idx++) {
    GX_INI_LOG("  [%d] = '%s'", idx, *name);
}
#endif
```

This pinpointed `FLESHY_SNIPER` as the unmatched DamageType token.

### 6.3 Archive directory tree diagnostics (`ArchiveFile.cpp`)

Logged `addFile()` paths, `getFileListInDirectory()` traversal, and root
subdir counts to verify the BIG archive directory tree was built correctly.

**Key lesson:** logcat buffers are small (~256KB default). Verbose
per-entry logging (e.g. logging every BIG file entry) overflows the buffer
and hides the actual crash logs. Use `adb logcat -G 16M` to increase the
buffer, or filter diagnostics to specific files/conditions.

---

## 7. Current State and Next Steps

### 7.1 Where we are now (🎉 Main menu rendering + text + touch!)

The full engine init pipeline completes end-to-end and the **main menu
renders on the tablet screen** at native resolution (3392×2400) on an
Adreno 830 GPU:

```
FileSystem → BIG archives → GameData → Science → Multiplayer → Terrain →
Audio (musicLoaded=1) → GameText → FunctionLexicon → ModuleFactory →
RankInfo → PlayerTemplate → FXList → Weapon → ObjectCreationList →
Locomotor (182 parsed) → ThingFactory → ... → DXVK device creation →
Init complete → execute() main loop running
```

The 3D scene, tanks, explosions, soldiers, buildings, terrain, and the
C&C Generals Zero Hour logo all render correctly. **Menu button text is
visible** (fonts extracted from APK assets). **Touch input works** —
tapping menu buttons triggers UI navigation.

Process stats while running: ~2GB RSS, 31 threads, no crashes.

### 7.2 Remaining work

1. **Remove diagnostic instrumentation** once stable (LocoStore logs, INI
   field diagnostics, touch event logging).
2. **Performance profiling** — measure frame rate, identify bottlenecks.
3. **Audio playback** — verify OpenAL actually produces sound output.
4. **Commit the port progress**.

---

## 8. Architecture Notes

### 8.1 File system layering

The engine has a layered virtual file system:

```
FileSystem (orchestrator)
├── LocalFileSystem     — loose files on disk (std::filesystem)
└── ArchiveFileSystem   — files inside .big archives
    ├── ArchiveFile (INI.big)      — individual archive's directory tree
    ├── ArchiveFile (INIZH.big)
    ├── ArchiveFile (Audio.big)
    └── ... (27 archives total)
    └── m_rootDirectory (MERGED)   — union of all archives, used by doesFileExist/openFile
```

Two distinct directory tree types:
- `DetailedArchivedDirectoryInfo` — per-ArchiveFile trees (used by
  `getFileListInDirectory` for directory enumeration)
- `ArchivedDirectoryInfo` — the merged tree in ArchiveFileSystem (used by
  `doesFileExist`, `openFile`, `getArchiveFile`)

`loadIntoDirectoryTree()` enumerates each ArchiveFile via
`getFileListInDirectory("", "", "*", ...)` and merges into the
ArchiveFileSystem's `m_rootDirectory`.

### 8.2 File override priority

`ArchivedFileLocationMap` is a `std::multimap` allowing duplicate keys.
- `overwrite=FALSE` → new entries appended to END → first-loaded wins
- `overwrite=TRUE` → new entries inserted at FRONT → last-loaded wins

`getArchiveFile(filename, instance=0)` returns the FIRST match, so the
front entry determines which archive's version of a file is used.

### 8.3 INI parsing and exceptions

The INI parser throws raw `int` values (enum constants like
`INI_INVALID_DATA`, `INI_UNKNOWN_TOKEN`) — NOT `std::exception` subclasses.
This is why `catch(const std::exception&)` doesn't match and the generic
`catch(...)` fires. The `RELEASE_CRASH` mechanism then calls `_exit(1)`.

### 8.4 The NameKey system

`NAMEKEY("SomeName")` calls `TheNameKeyGenerator->nameToKey(name)` which
assigns sequential IDs at runtime. The key is deterministic within a
single process run — the same string always maps to the same key. Lookups
use the same mechanism, so they're consistent as long as both the
definition and reference use the exact same string.

---

## 9. Key Files Modified for Android

| File | Purpose |
|------|---------|
| `Core/GameEngine/Include/GameLogic/Damage.h` | FLESHY_SNIPER fix |
| `Core/GameEngine/Source/GameLogic/System/Damage.cpp` | FLESHY_SNIPER name list fix |
| `Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFileSystem.cpp` | BIG override fix |
| `Core/GameEngine/Source/Common/INI/INI.cpp` | from_chars guard, INI diagnostics |
| `GeneralsMD/Code/GameEngine/Source/Common/GameEngine.cpp` | Android GX_LOG, audio bypass, lang fallback |
| `GeneralsMD/Code/GameEngine/Source/Common/GameMain.cpp` | Init diagnostics |
| `GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp` | BuildUserDataPath Android fix |
| `GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Locomotor.cpp` | Locomotor diagnostics |
| `GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AIUpdate.cpp` | parseLocomotorSet diagnostics |
| `Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALAudioManager.cpp` | FFmpeg guard |
| `Core/GameEngineDevice/Include/VideoDevice/FFmpeg/FFmpegAndroidStub.h` | FFmpeg stub (created) |
| `Core/GameEngineDevice/Source/VideoDevice/FFmpeg/FFmpegFileStub.cpp` | FFmpeg stub (created) |
| `Core/GameEngineDevice/CMakeLists.txt` | Android FFmpeg stub, DXVK includes |
| `Core/GameEngine/Include/Common/System/GameMemory.h` | DMA magic cookie field |
| `Core/GameEngine/Source/Common/System/GameMemory.cpp` | Cookie init + safe delete routing |
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` | DXVK format fix, windowed presentation, logcat |
| `GeneralsMD/Code/CompatLib/CMakeLists.txt` | Android shared lib, GLM, DXVK |
| `GeneralsMD/Code/Main/CMakeLists.txt` | Android libmain.so, -llog |
| `cmake/dx8.cmake` | Android DXVK build via Meson |
| `cmake/freetype.cmake` | FreeType FetchContent for Android |
| `cmake/android-deps.cmake` | GLM FetchContent for Android |
| `cmake/gamespy.cmake` | pthread_cancel stub |
| `cmake/sdl3.cmake` | SDL_image PNG config |
| `Patches/dxvk-android.patch` | SSE gating, WSI soname, high-DPI fix |
| `GeneralsMD/Code/Main/SDL3Main.cpp` | Android entry point, VFS, env vars, font extraction |
| `GeneralsMD/Code/Main/CMakeLists.txt` | Android libmain.so, -llog, -landroid |
| `GeneralsMD/Code/GameEngineDevice/Source/SDL3GameEngine.cpp` | Touch event dispatch (SAGE_MOBILE), diagnostics |

---

## 10. Mod Support (2026-07-09)

**Status: code complete, on-device verification PENDING.** The `-mod` argv injection,
the POSIX path-separator fix, and the ModuleFactory alias seam are implemented and
committed-tree-ready, but the build loop (Task 3) and logcat matrix (Task 4) have NOT
run — the WSL toolchain (NDK r27, meson) was not provisioned in the session that wrote
this. Every claim below about code structure is verifiable from source; every claim
about runtime behavior is PENDING and must be confirmed by the logcat matrix before
this section is considered authoritative. See `docs/superpowers/plans/2026-07-08-android-mod-architecture.md`.

### 10.1 How a mod is selected at launch (precedence)

The engine already supported `-mod <path>` on desktop. On Android there is no command
line, so `GeneralsMD/Code/Main/SDL3Main.cpp` (inside `main()`, immediately before
`CommandLine::parseCommandLineForStartup()`, under `#if defined(__ANDROID__)`) resolves
a mod path from two sources, in this order:

1. **Intent extra `"mod"`** (per-launch, explicit) — read via JNI:
   `SDL_GetAndroidJNIEnv()` → `SDL_GetAndroidActivity()` → `getIntent()` →
   `getStringExtra("mod")`. Used by launcher apps and `adb shell am start --es "mod" <path>`.
2. **`GameData/mod.txt`** (persistent default) — `fopen("mod.txt","r")` relative to CWD,
   which is already GameData after the chdir block (~`SDL3Main.cpp:317-337`). One line:
   the absolute mod path. Trailing `\r\n`, spaces, tabs are trimmed (Windows-edited files).

If neither yields a path, or the path fails `access(path, R_OK)`, the game launches
vanilla with a logcat warning. The resolved path is injected as `-mod <path>` into
`__argv`/`__argc` and consumed later by `parseCommandLineForEngineInit()` → `parseMod()`.

### 10.2 Static-buffer lifetime rule (do not regress)

The mod path buffer and the rebuilt argv are **`static`**, not stack/heap:

```cpp
static char modPathBuf[512];     // pointer escapes into __argv
static char* modArgv[64];        // __argv is reassigned to point at this
static char modFlag[] = "-mod";
```

`parseMod()` dereferences `__argv` at `GameEngine::init` — long after the injection
block exits. A stack buffer would dangle; `realloc` is wrong (`__argv` is `main()`'s
argv, not heap). This mirrors the existing `-xres`/`-yres` static-buffer injection
later in the same file. **Do not convert these to non-static storage.** This was the
v1 plan's review-killing defect (dangling pointer).

### 10.3 Engine bug: POSIX path separator in `parseMod()`

`parseMod()` (`GeneralsMD` and `Generals` `CommandLine.cpp`) appended `\` to mod
directory paths with no trailing separator. On Android/POSIX, a `\`-terminated path
breaks `loadBigFilesFromDirectory`'s `opendir`. Fixed behind `#ifdef _WIN32`:

```cpp
if (!modPath.endsWith("\\") && !modPath.endsWith("/")) {
#ifdef _WIN32
    modPath.concat('\\');
#else
    modPath.concat('/');   // POSIX — opendir needs a forward slash
#endif
}
```

Applied to **both** games per the backport rule (base Generals shares this code path
via `INI.big`).

### 10.4 Loose-file contract (the non-obvious one)

**Loose files in the mod directory do NOT resolve via `LocalFileSystem` — on any
platform.** `m_modDir` is consumed only by `loadMods()` (loads `*.big` via
`loadBigFilesFromDirectory`), the video players (stubbed on Android), and `Win32Mouse`
cursors (not compiled on Android). Nothing registers the mod dir with generic file
resolution.

Therefore a mod like Xenoforce that ships loose `Art/` and `Data/` folders can either
have those **merged into the GameData tree** (`GameData/Art/`, `GameData/Data/`) — loose
files in GameData win over every archive (resolution priority 1) — or, as of Task 13
(D8a), place them directly in the mod directory (`Mods/Xenoforce/Art/`, etc.) and they
will resolve via `LocalFileSystem::setAssetFallbackPaths`, which `ArchiveFileSystem::
loadMods()` wires automatically. The fallback search order is: CWD → asset root →
case-insensitive asset root → **mod fallback paths** → case-insensitive CWD. Mod loose
files override `.big` archive contents but NOT loose files already in the GameData root.
Parent-traversal (`..`) segments in fallback paths are rejected (security). Trade-off:
loose overrides merged into GameData are NOT switched by `mod.txt` — removing the mod
means deleting the merged files. Mod-directory loose files ARE switched by `mod.txt`
(switched when the mod dir changes).

### 10.5 ModuleFactory alias seam (plan D5)

`ModuleFactory` (GeneralsMD) gained `addModuleAlias(existingName, aliasName, type)` and
an `m_aliasMap`. `findModuleTemplate()` consults the alias map before the template map,
with an 8-hop bound against circular aliases. This lets a mod refer to an existing
module type by an alternative name without recompilation. **GeneralsMD only** — Zero
Hour is the sole Android target. Note: `m_aliasMap` is not cleared by `reset()`/`init()`
(both are no-ops by design); the ModManager (plan Task 9) must add alias teardown on
mod unload or stale aliases from a previous mod will corrupt resolution. Oracle review
(2026-07-10) found and fixed a buffer overflow in `makeDecoratedNameKey`: it used `strcpy`
into a 256-byte stack buffer with no length check — mod-supplied names >254 chars could
overflow. Fixed with `snprintf`. Also added a DEBUG_LOG on 8-hop-bound exhaustion and
documented that alias-to-alias chains are rejected (aliases must point to real templates)
and that aliasing a name that is already a template intentionally shadows it (mod override).

### 10.6 Install workflow — Option C (archives + loose files)

```bash
BASE=/sdcard/Android/data/me.generalsx.zh/files/GameData
adb shell mkdir -p $BASE/Mods/Xenoforce
# .big archives -> mod dir (switchable via mod.txt)
adb push 15Xeno.big      $BASE/Mods/Xenoforce/
adb push 15PacthXeno.big $BASE/Mods/Xenoforce/
# Loose overrides (Option A): merged into GameData tree (always active while present)
adb push Art/  $BASE/Art/
adb push Data/ $BASE/Data/
# OR Loose overrides (Option B, Task 13): in the mod dir (switchable via mod.txt)
adb push Art/  $BASE/Mods/Xenoforce/Art/
adb push Data/ $BASE/Mods/Xenoforce/Data/
# Set the persistent default
adb shell "echo '$BASE/Mods/Xenoforce' > $BASE/mod.txt"
# Or per-launch override (wins over mod.txt):
adb shell am start -n me.generalsx.zh/.GameActivity --es "mod" "$BASE/Mods/Xenoforce"
```

### 10.7 Mod picker GUI (Task 12 / D7)

A **Mods** button is dynamically created on the main menu (bottom-left, via
`TheWindowManager->gogoGadgetPushButton`). Tapping it pushes `Menus/ModPickerMenu.wnd`,
which contains a ListBox + Activate + Cancel buttons. `ModPickerMenuInit` scans
`GameData/Mods/` for subdirectories (POSIX `dirent`/`stat`), populates the ListBox, and
on Activate writes the selected mod path to `mod.txt`. Cancel/Esc pops without changes.
The button is destroyed in `MainMenuShutdown` to prevent dangling pointers on reopen.
This gives users a touch-friendly way to select mods without `adb` or `mod.txt` editing.

### 10.8 Memory-budget eviction safety (Task 11 / D6 — Oracle review)

The mod-archive eviction path (`evictColdestModArchive`, triggered when the DMA budget
exceeds 512 MB) had two CRITICAL bugs found by Oracle review, both fixed (commit
`c3c7c0498`):

1. **Use-after-free**: `closeArchiveFile` deleted the `ArchiveFile` without calling
   `closeAllFiles()` first — any open `File` handles from that archive would dangle. Fixed:
   `it->second->closeAllFiles()` is now called before `delete` in both
   `StdBIGFileSystem` and `Win32BIGFileSystem`. The `DEBUG_ASSERTCRASH` that crashed
   debug builds on non-music archive close was also removed.

2. **Eviction storm**: Archives are allocated via global `new`/`delete` (`#define NEW new`
   in `GameMemoryNull.h`), NOT the DMA allocator. So evicting archives never decrements
   `theCurrentBudgetBytes`. Without a guard, every DMA allocation over budget would
   re-trigger eviction forever. Fixed: `theEvictionExhausted` flag (atomic) is set when
   `evictColdestModArchive()` returns FALSE (nothing left), with hysteresis re-arming at
   80% of the budget threshold.

**Known limitations** (documented, not blocking): eviction is first-by-map-order
(alphabetical path), NOT true LRU — no access tracking exists. It runs on the allocating
thread (main game thread in practice on Android); not safe to call from other threads.

### 10.9 Verification matrix — PENDING (on-device runbook)

The logcat matrix below has NOT been run (no device connected). When a tablet is
available, execute each scenario in order and record the actual logcat line in the
Result column. Any deviation from Expected → treat as a build failure (triage ladder).

**Setup (once):**
```bash
BASE=/sdcard/Android/data/me.generalsx.zh/files/GameData
adb install -r android/app/build/outputs/apk/release/app-release.apk
adb shell mkdir -p $BASE/Mods/Xenoforce $BASE/Data
adb push "Data/*.big" $BASE/Data/
adb logcat -G 16M          # enlarge buffer (§6) — CRITICAL, default 256KB overflows
```

| # | Scenario | Command | Expected logcat | Result |
|---|----------|---------|-----------------|--------|
| 1 | vanilla | `adb shell rm -f $BASE/mod.txt && adb shell am start -n me.generalsx.zh/.GameActivity` | no "Mod path" lines; main menu renders | _______ |
| 2 | mod.txt | `adb shell "echo '$BASE/Mods/Xenoforce' > $BASE/mod.txt" && adb shell am start -n me.generalsx.zh/.GameActivity` | `Mod path from mod.txt:` + `Injected -mod` | _______ |
| 3 | Intent extra | `adb shell rm -f $BASE/mod.txt && adb shell am start -n me.generalsx.zh/.GameActivity --es "mod" "$BASE/Mods/Xenoforce"` | `Mod path from Intent extra:` | _______ |
| 4 | precedence | set mod.txt to Xenoforce, launch with `--es "mod" "$BASE/Mods/Contra"` (any 2nd dir) | `Mod path from Intent extra:` (Intent wins) | _______ |
| 5 | invalid path | `adb shell am start -n me.generalsx.zh/.GameActivity --es "mod" "/sdcard/nonexistent"` | `Mod path not accessible, ignoring` + vanilla launch | _______ |
| 6 | CRLF mod.txt | `adb shell "printf '$BASE/Mods/Xenoforce\r\n' > $BASE/mod.txt" && adb shell am start -n me.generalsx.zh/.GameActivity` | path trimmed correctly, mod loads | _______ |
| 7 | loose override (GameData) | `adb push Art/ $BASE/Art/ && adb shell am start -n me.generalsx.zh/.GameActivity` (with mod active) | loose file in GameData/Art wins over archive | _______ |
| 8 | mod picker GUI (Task 12) | launch vanilla, tap **Mods** button (bottom-left) | ModPickerMenu opens, lists Mods/Xenoforce | _______ |
| 9 | mod picker activate | select a mod in the list, tap Activate | `mod.txt` written, menu pops | _______ |
| 10 | mod-dir loose files (Task 13) | `adb push Art/ $BASE/Mods/Xenoforce/Art/ && adb shell am start ... --es "mod" "$BASE/Mods/Xenoforce"` | loose file in Mods/Xenoforce/Art resolves (overrides .big, not GameData loose) | _______ |

**Capture per scenario:** `adb logcat -c && adb shell am start ... && adb logcat -d -s GeneralsX:V | tail -60`

**Human eye-check (only the user can answer):** menu reached? mod content visible? no visual
corruption? User pass → replace "PENDING" in the heading with "PASS" and fill the Result
column with actual logcat lines. Until these pass, this section documents intent, not
verified behavior.

### 10.10 On-device debugging findings (2026-07-10, Lenovo TB322FC / Android 16)

First real on-device run. Three findings, two fixed:

**1. SDL_free double-free crash (FIXED, commit `d2cef9631`).** The app SIGABRT'd during
init with `Scudo ERROR: invalid chunk state when deallocating`. Root cause: SDL3's
`SDL_GetAndroidExternalStoragePath`, `SDL_GetAndroidInternalStoragePath`, and
`SDL_GetAndroidCachePath` each cache their result in a function-local `static` and return
the SAME pointer on every call (see `SDL_android.c`). The bootstrap code freed these after
each use — corrupting SDL3's static cache. The next call returned the dangling pointer, and
freeing it again was a double-free. Fixed by removing all 6 `SDL_free` calls on path
results in `SDL3Main.cpp` (these are process-lifetime cached strings that must never be
freed — documented inline since SDL3 docs misleadingly say to free them). After the fix the
engine progresses through full init (critical sections, memory, Version, CommandLine,
GameMain, FileSystem, ArchiveFileSystem, fonts extracted).

**2. .big file ownership (FIXED via root `chown`).** The `.big` files pushed via `adb push`
were owned by `u0_a202` (a stale UID from a previous app install) with `-rw-rw----`. The
current app (`u0_a305`) fell into "others" → no read access → `access(R_OK)` failed on
`GameData/` (silent chdir failure) and the engine couldn't read the archives. The directory
`chmod go+rx` worked (dirs became traversable) but file `chmod` is rejected by sdcardfs
("Operation not permitted"). Definitive fix: `su -c "chown -R u0_a305:u0_a305 .../GameData/"`
(root required). After chown, the engine loads all `.big` files and parses INI successfully.

**3. Heap corruption during GameEngine::init (FIXED, commit `8ceb2f690`).** After `.big`
loading and INI parsing succeed, the engine aborted with `Scudo ERROR: corrupted chunk
header`. The crash was in `StdLocalFileSystem::doesFileExist` → `fixFilenameFromWindowsPath`
→ `std::filesystem::operator/`. Root cause: the case-insensitive std::filesystem resolution
(`operator/` + `directory_iterator`) in `fixFilenameFromWindowsPath` corrupts the heap on
Android — confirmed via heap probes proving the heap is clean before the function, and the
crash vanishing when the resolution is bypassed. Fix: added an `#if defined(__ANDROID__)`
early-return that returns the plain path (backslashes already converted). This is correct for
Android — Android is case-sensitive, and `.big` archive lookups go through `ArchiveFileSystem`
(which has its own case handling), so the loose-file case-insensitive traversal is not needed.
After the fix the engine completes full init, creates the D3D device (DXVK → Vulkan), and
enters `execute()` (the game loop). Note: the engine runs at ~4 GB RSS on a high-res tablet
display (1904×3040); a Scudo "Can't populate more pages for size class 65552" warning may
appear under memory pressure but is non-fatal.

**4. Main menu renders — CONFIRMED (2026-07-10).** After fixes 1-3, the engine boots,
completes full init, creates the D3D device (DXVK → Vulkan on Adreno), enters `execute()`,
and **renders the in-game main menu** (verified by user on Lenovo TB322FC, Android 16).
Screenshots: `on-device-screenshot.png` / `on-device-screenshot-early.png` in the repo root.

**5. Remaining stability issues (after main menu, not blockers for the milestone):**
- **DXVK SIGSEGV** — `signal 11 (SEGV_MAPERR), fault addr 0x15c in tid (dxvk-cs)`. A
  null-pointer dereference in DXVK's command-stream thread during continued rendering. No
  tombstone captured (`crash_dump64: failed to connected to tombstoned`). Needs backtrace
  capture (reproduce + read `/data/tombstones/` via root) to identify the triggering D3D8 op.
- **OOM during audio loading (FIXED, commit `dfe786d87`).** `Scudo ERROR: internal map failure
  (error desc=Out of memory)`. Root cause: the process reaches **VmSize ~19 GB** (virtual
  address space) while RSS is only ~2.5 GB — DXVK/Vulkan reserves huge virtual ranges, plus
  thread stacks and heap. When the audio system loads a file (`RAMFile::openFromArchive` →
  `operator new[]`), Scudo's secondary allocator (mmap) can't find contiguous space → SIGABRT.
  Map count was 26,775 / 65,530 (not at limit); the device has 9.6 GB physical free. Fix:
  `OpenALAudioFileCache::getBufferForFile` returns 0 (no buffer) on Android — audio playback
  doesn't work yet anyway (README: "OpenAL inits but no sound"), and skipping the RAM-intensive
  file load prevents the OOM. After the fix the engine is **stable — verified ALIVE after 170 s**
  (past the previous ~2.5 min crash mark) with no crash. The DXVK SIGSEGV (null deref 0x15c)
  did not reappear after this fix, suggesting it was the same OOM root cause (a failed Vulkan
  allocation returning null). Future work for real audio: stream via `StreamingArchiveFile`
  instead of loading into RAM, or reduce DXVK virtual reservations.

**6. Audio backend FIXED (opensl selected).** OpenAL Soft (v1.24.2) defaulted to the **"null"**
backend on Android — logcat: `Initialized backend "null"`, `Created device ..., "No Output"`.
The **opensl** (OpenSL ES) backend IS compiled in (`Supported backends: opensl, null, wave`)
but was not selected. Fix: `setenv("ALSOFT_DRIVERS", "opensl", 0)` in `SDL3Main.cpp` (per
OpenAL Soft `docs/env-vars.txt`: `ALSOFT_DRIVERS` "overrides the drivers config option").
Verified on-device: logcat now shows `Initialized backend "opensl"`, `Created device ...,
"OpenSL"`, `libOpenSLES: ...` (OpenSL ES active). The earlier `ALSOFT_BACKEND` attempt did
NOT work (OpenAL ignores it). **Only streaming remains for real audio**: audio file loading is
still skipped (`OpenALAudioCache::getBufferForFile` returns 0) to avoid the VmSize 19GB OOM
(finding 5). Implement `StreamingArchiveFile` (or reduce DXVK virtual reservations) to load
audio without OOM — then the opensl backend will produce actual sound. **Hard blocker**: the
audio/video decoder (FFmpeg) is also disabled on Android — `RTS_BUILD_OPTION_FFMPEG=OFF`,
`FFmpegFileStub.cpp` is compiled instead of `FFmpegFile.cpp`. FFmpeg can't be built for
arm64-android (upstream vcpkg issue microsoft/vcpkg#33963). So real audio requires: (1) the
opensl backend ✅ done (f9775f3bd), (2) a working FFmpeg build for arm64 (upstream-blocked),
(3) streaming to avoid the OOM. The backend fix is in place for when FFmpeg is available.
