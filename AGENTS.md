# GeneralsZH-Android: Instructions for AI Coding Agents

## What This Is

This is the **Android arm64-v8a port** of Command & Conquer: Generals Zero Hour — the full 2003 RTS C++ engine (~500k LOC) compiled natively for Android tablets via DXVK → Vulkan. **No emulation.** The real engine as `libmain.so`, loaded by SDL3's `nativeRunMain`.

This is the **first-ever DXVK build for Android**. The engine speaks DirectX 8; DXVK translates to Vulkan on Adreno/Mali GPUs.

**Forked from:** [ammaarreshi/Generals-Mac-iOS-iPad](https://github.com/ammaarreshi/Generals-Mac-iOS-iPad) (which itself descends from fbraz3/GeneralsX → Fighter19 → TheSuperHackers → EA's GPL v3 source release).

**Repo:** https://github.com/tarek369/GeneralsZH-Android

## Must-Load Context

Before starting work on this repo, read (in order):
1. **`android.md`** — canonical engineering log of every bug found, root cause, and fix during the Android port. **Always read this first.**
2. **`CLAUDE.md`** — quick Android-specific reference (build flags, gotchas, deploy workflow)
3. `.github/instructions/git-commit.instructions.md` — commit standards (Conventional Commits)
4. `.github/instructions/docs.instructions.md` — documentation workflow
5. `docs/DEV_BLOG/YYYY-MM-DIARY.md` — current development notes

## Platform Focus

| Platform | Status |
|----------|--------|
| **Android (arm64-v8a)** | ✅ **Active** — main menu renders, touch input works, skirmish playable. See `android.md` for status. |
| Linux (x86_64) | ⚠️ Legacy — inherited from upstream; not the focus of this fork |
| macOS (ARM64) | ⚠️ Legacy — inherited from upstream |
| iOS/iPadOS | ⚠️ Legacy — inherited from ammaarreshi's fork |
| Windows | ❌ Not a target for this fork |

**This fork's sole purpose is Android.** Multi-platform changes from upstream should be evaluated for Android impact first. Non-Android build presets exist for reference but are not actively maintained here.

## Architecture

```
┌──────────────────────────────────────────────┐
│              Game Engine (C++)                │
│         (~500k LOC, GPL v3 source)            │
├──────────────────────────────────────────────┤
│  Graphics: DirectX 8 API calls                │
│     ↓                                         │
│  DXVK (libdxvk_d3d8.so) — D3D8 → Vulkan      │
│     ↓                                         │
│  Android Vulkan Driver (Adreno / Mali)        │
├──────────────────────────────────────────────┤
│  Windowing: SDL3 (touch → synthetic mouse)    │
│  Audio: OpenAL Soft                           │
│  Video: FFmpeg (stubbed — vcpkg ffmpeg:arm64-android is broken) │
├──────────────────────────────────────────────┤
│  Android OS (arm64-v8a, API 24+)              │
└──────────────────────────────────────────────┘
```

Platform code must be isolated to `Core/GameEngineDevice/` and `Core/Libraries/Source/Platform/`. No native Win32/Cocoa/X11 calls in game logic. Android-specific code uses `#if defined(__ANDROID__)` guards or the `SAGE_MOBILE` macro.

## Golden Rules

1. **Android-first** — This fork exists for Android. Changes must not break the Android build.
2. **SDL3 everywhere** — No native platform calls in game code
3. **DXVK everywhere** — DX8 → Vulkan on all platforms (this fork inherits DXVK from upstream)
4. **OpenAL everywhere** — Cross-platform audio stack
5. **64-bit only** — arm64-v8a. No 32-bit Android path.
6. **Retail compatibility** — Rendering/audio changes must not affect gameplay determinism
7. **No band-aids** — Fix root causes, not symptoms. Document in `android.md`.
8. **Update android.md** — `android.md` is the canonical Android port log. Add findings there.
9. **Single codebase** — Game logic must remain shared. Platform code goes through device layers.

## Key Entry Points

- `GeneralsMD/Code/Main/SDL3Main.cpp` — Android entry point, SDL3 main, font extraction from APK assets
- `GeneralsMD/Code/Main/WinMain.cpp` — Game launcher (shared across platforms)
- `Core/GameEngineDevice/Source/` — Platform device layer
- `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` — DXVK device creation, format selection
- `android/app/` — Gradle project (appId `me.generalsx.zh`, minSdk 24, arm64-v8a only)
- `android/app/build.gradle` — Drives CMake build via `externalNativeBuild`

## Directory Layout

| Directory | Purpose |
|-----------|---------|
| `GeneralsMD/` | Zero Hour game code (**primary** and only Android target) |
| `Generals/` | Base Generals (backport target only when changes are shared) |
| `Core/` | Shared engine + platform libraries |
| `android/` | Gradle project, APK packaging, JNI surface |
| `references/fbraz3-dxvk/` | DXVK fork submodule (built from source via Meson) |
| `Patches/` | Android patches (dxvk-android.patch) |
| `docs/DEV_BLOG/` | Monthly development diary |
| `docs/WORKDIR/` | Active work docs (phases, planning, reports) |
| `docs/HOWTO/` | User-facing tutorials |
| `scripts/build/android/` | Android build/package scripts |

## Target Priority

1. **GeneralsXZH** (Zero Hour) — The only Android target. No base Generals on Android.
2. **GeneralsX** (Base game) — Backport only when changes are unambiguously shared.

## Build & Deploy

### Prerequisites

- **Android NDK r27** (27.1.12297006) — set `$ANDROID_NDK_HOME`
- **Android SDK** with build-tools 35.0.0+
- **CMake 3.25+** and **Ninja**
- **Meson** — for DXVK cross-compilation from the `references/fbraz3-dxvk` submodule
- **Android Studio** or Gradle 8.7+ — for APK packaging

### Build

```bash
# Clone + init submodules
git clone https://github.com/tarek369/GeneralsZH-Android.git
cd GeneralsZH-Android
git submodule update --init --recursive references/fbraz3-dxvk

# Configure + build the native engine (arm64-v8a)
cmake --preset android-vulkan
cmake --build build/android-vulkan --target z_generals

# Package the APK (stages fonts, DXVK .so, SDL3 .so, etc.)
./scripts/build/android/package-android-zh.sh [--install]
```

The APK appears at: `android/app/build/outputs/apk/release/app-release.apk`

### Required Runtime .so Libraries (all must land in `jniLibs/arm64-v8a/`)

`libdxvk_d3d8.so`, `libdxvk_d3d9.so`, `libSDL3.so`, `libSDL3_image.so`, `libopenal.so`, `libfreetype.so`, `libglm.so`, `libgamespy.so`, `libc++_shared.so`, `libmain.so`

Missing one → `dlopen failed` crash on launch.

### Deploy & Debug

```bash
# Install
adb install -r android/app/build/outputs/apk/release/app-release.apk

# Game data (push your legal copy's .big files)
adb shell mkdir -p /sdcard/Android/data/me.generalsx.zh/files/GameData/Data
adb push "Data/*.big" /sdcard/Android/data/me.generalsx.zh/files/GameData/Data/

# Enlarge logcat buffer (CRITICAL — default 256KB overflows with verbose engine logs)
adb logcat -G 16M

# Launch + capture logs
adb logcat -c
adb shell am start -n me.generalsx.zh/.GameActivity
adb logcat -d -s GeneralsX:V | tail -60
```

## Android-Specific Gotchas (NON-OBVIOUS — DO NOT REGRESS)

### 1. DXVK strip is FATAL
**Do NOT strip `libdxvk_d3d8.so` or `libdxvk_d3d9.so`** — stripping breaks Vulkan dispatch-table resolution → SIGSEGV. `app/build.gradle` explicitly keeps their debug symbols. `libmain.so` IS stripped (`--strip-debug`, ~85MB→16MB) and that's fine.

### 2. Memory allocator cookie (GameMemory)
The engine's custom DMA pool allocator used to intercept ALL `operator delete` calls, including those from OpenAL and libc++. Fixed with a **magic cookie** (`0x47454d53`) in `MemoryPoolSingleBlock::initBlock()`. Global `operator delete` checks for the cookie: present → route to pool; absent → route to `::free()`. **Do not revert or "simplify" this.** Files: `Core/GameEngine/Include/Common/GameMemory.h`, `Core/GameEngine/Source/Common/System/GameMemory.cpp`.

### 3. BIG archive override precedence (multimap ordering)
On a Complete Edition install, both `INI.big` (base) and `INIZH.big` (ZH) sit in `Data/`. `ArchiveFileSystem::loadIntoDirectoryTree` uses `std::multimap::insert(find(token), ...)` — BUT `multimap::insert` with a hint does **NOT** guarantee front-of-range placement in C++11+. So the erase-and-reinsert dance in `ArchiveFileSystem.cpp` is deliberate. **Do not "simplify" it.** See `android.md` §4.2–4.3.

### 4. FLESHY_SNIPER DamageType
`DAMAGE_FLESHY_SNIPER` was gated behind `#if RTS_GENERALS`, but ZH loads base `INI.big` which references it. Must NOT be `#if`-gated. Fixed in `Damage.h` / `Damage.cpp`. See `android.md` §4.1.

### 5. DXVK CreateDevice format selection
The engine's fullscreen format path returns `D3DFMT_UNKNOWN` on Android (no desktop display modes). Fixed by forcing the windowed format path on ALL non-Windows platforms (`#ifndef _WIN32`), plus a fallback to `D3DFMT_X8R8G8B8` when `GetAdapterDisplayMode` returns UNKNOWN. See `android.md` §4.6. Files: `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp`.

### 6. Font extraction from APK assets
APK assets are invisible to `access()`/`fopen()`. The engine's FreeType font locator probes `<CWD>/fonts/<name>.ttf`. `SDL3Main.cpp` extracts Liberation fonts (renamed to Windows names) from `AAssetManager` to `<storage>/GameData/fonts/` on first launch. Idempotent — skips if `fonts/arial.ttf` exists. See `android.md` §4.7.

### 7. Touch input (SAGE_MOBILE)
Touch input is implemented behind the `SAGE_MOBILE` macro (defined when `__ANDROID__`). A `TouchState` gesture state machine in `SDL3GameEngine.cpp` injects synthetic `SDL_EVENT_MOUSE_*` events:
- Tap → left-click
- Long-press (600ms) → right-click
- Drag → selection box
- Two-finger drag → camera pan
- Two-finger pinch → zoom

### 8. INI parser throws int enums, not std::exception
The INI parser throws raw `int` enum values (`INI_INVALID_DATA`, etc.), NOT `std::exception` subclasses. `catch(const std::exception&)` won't match — the generic `catch(...)` fires with a uniform "Error parsing INI file" that hides the real cause. Use `__android_log_print` diagnostics to debug parse failures. See `android.md` §6.

### 9. logcat buffer size
Default 256KB buffer overflows with verbose engine logging, hiding crash logs. Always `adb logcat -G 16M` before debugging.

### 10. Build directory is build/android-vulkan
The CMake preset is `android-vulkan` (binaryDir: `build/android-vulkan`). Both the Gradle SDL3 Java srcDir (`android/app/build.gradle`) and `scripts/build/android/package-android-zh.sh` `BUILD_DIR` now point at `build/android-vulkan` (reconciled 2026-07-10, plan Phase 1.5 T1/T2). References to a stale `build/android-game/` in older docs are historical — that was the pre-reconciliation name.

## Android Build Flags (Non-Default)

| Flag | Value | Why |
|------|-------|-----|
| `SAGE_DXVK_USE_LOCAL_FORK` | ON | DXVK built from `references/fbraz3-dxvk` submodule via Meson |
| `RTS_GAMEMEMORY_ENABLE` | OFF | DMA allocator conflicts with `c++_shared` |
| `RTS_BUILD_OPTION_FFMPEG` | OFF | vcpkg ffmpeg:arm64-android is broken (microsoft/vcpkg#33963) |
| `RTS_CRASHDUMP_ENABLE` | OFF | No minidump on Android |
| `RTS_ZEROHOUR` | 1 | Zero Hour is the only target |
| `ANDROID_PLATFORM` | android-24 | API 24+ for system Vulkan |
| `VCPKG_TARGET_TRIPLET` | arm64-android | arm64-v8a only |

## Touch Controls (User-Facing)

| Gesture | Action |
|---------|--------|
| Tap | Left-click (select unit, click button) |
| Tap and hold (600ms) | Right-click (context menu, deselect) |
| Drag | Left-click drag (selection box) |
| Two-finger drag | Right-click drag (camera pan) |
| Two-finger pinch | Mouse wheel (zoom in/out) |

## Code Conventions

- **Annotate changes**: `// GeneralsX @keyword author DD/MM/YYYY Description`
- **Keywords**: `@bugfix` / `@feature` / `@performance` / `@refactor` / `@tweak` / `@build`
- **English only**: All code, comments, documentation
- **No lazy code**: No empty stubs, empty catch blocks, or commented-out code
- **Android-specific guards**: Use `#if defined(__ANDROID__)` or `#if __ANDROID__`. For mobile/touch features shared with iOS, use `#ifdef SAGE_MOBILE`.

## Commit Standards

Follow Conventional Commits. Types: `feat`, `fix`, `docs`, `refactor`, `test`, `chore`, `build`, `ci`, `perf`, `style`.

```
<type>(<scope>): <description>

[body]

[footer]
```

Common scopes: `android`, `dxvk-android`, `cmake`, `graphics`, `audio`, `input`.

**Do NOT commit unless explicitly requested.**

## Documentation Workflow

1. **`android.md`** — Update for any Android-specific bug, fix, or discovery. **Most important doc for this fork.**
2. **`docs/DEV_BLOG/YYYY-MM-DIARY.md`** — Monthly diary, newest entries first
3. **`docs/WORKDIR/phases/`** — Active phase plans and checklists
4. **`docs/HOWTO/`** — User-facing tutorials
5. Never drop docs directly under `docs/` root

## Reference Repositories

- **[ammaarreshi/Generals-Mac-iOS-iPad](https://github.com/ammaarreshi/Generals-Mac-iOS-iPad)** — Direct upstream (iOS/iPadOS port, touch controls)
- **[fbraz3/GeneralsX](https://github.com/fbraz3/GeneralsX)** — Upstream multi-platform port (Linux, macOS)
- **[TheSuperHackers/GeneralsGameCode](https://github.com/TheSuperHackers/GeneralsGameCode)** — Community mainline
- **[Fighter19/CnC_Generals_Zero_Hour](https://github.com/Fighter19/CnC_Generals_Zero_Hour)** — Original Unix/64-bit port
- **[doitsujin/dxvk](https://github.com/doitsujin/dxvk)** — Upstream DXVK (this fork uses fbraz3's fork)

## Lineage & Credits

Westwood / EA Pacific (original) → EA (GPL v3 source) → TheSuperHackers (build modernization) → Fighter19 (Unix/64-bit port, SDL3) → fbraz3 (macOS/Linux port) → ammaarreshi (iOS/iPadOS port, DXVK-on-iOS, touch) → **this fork (Android arm64, first-ever DXVK-on-Android)**

DXVK, SDL3, OpenAL Soft, Liberation Fonts — the open-source load-bearing walls.

## Build Presets Reference

| Preset | Platform | Notes |
|--------|----------|-------|
| `android-vulkan` | Android arm64-v8a | **PRIMARY** — the only actively maintained preset |
| `linux64-deploy` | Linux x86_64 | Inherited from upstream; not maintained here |
| `macos-vulkan` | macOS ARM64 | Inherited from upstream; not maintained here |
| `ios-vulkan` | iOS/iPadOS | Inherited from upstream |
| `vc6` | Win32 VC6 | Legacy reference only |
| `win32` | Win32 MSVC 2022 | Legacy reference only |

## Instruction Context Loading

`AGENTS.md` is the source of truth. The `.github/instructions/` files are scoped VS Code hints — they load only when the file path matches.

| Instruction File | applyTo | Purpose |
|---|---|---|
| `generalsx.instructions.md` | `**` | Stub → points to AGENTS.md |
| `git-commit.instructions.md` | `**` | Commit/PR message standards |
| `cpp-conventions.instructions.md` | `**/*.{cpp,h,hpp,c}` | Code style, annotations, platform isolation |
| `build.instructions.md` | `cmake/**,CMakeLists.txt,CMakePresets.json` | Build presets, DXVK source of truth |
| `docs.instructions.md` | `**/*.md` | Documentation structure and workflow |
| `scripts.instructions.md` | `scripts/**` | Script organization and naming |

Update this table when instruction files are added, removed, or renamed.

## Repository Map

A full codemap is available at `codemap.md` in the project root.

Before working on any task, read `codemap.md` to understand:
- Project architecture and entry points
- Directory responsibilities and design patterns
- Data flow and integration points between modules

For deep work on a specific folder, also read that folder's `codemap.md`.
