# Command & Conquer: Generals Zero Hour — Android

<img src="assets/android-screenshot.png" alt="Generals Zero Hour main menu running on Android tablet" width="600">

**The full 2003 RTS engine running natively on Android tablets** — not emulation,
not a compatibility layer. The real C++ engine compiled for arm64, rendering
DirectX 8 → [DXVK](https://github.com/doitsujin/dxvk) → Vulkan on Adreno/Mali GPUs.
This is the **first-ever DXVK build for Android**.

> ⚠️ **You must own a legal copy of the game.** No game assets are included or
> distributed. See [How to Play](#how-to-play) below.

---

## Status

| Feature | Status |
|---------|--------|
| Engine init (all subsystem stores) | ✅ |
| DXVK D3D8→Vulkan rendering | ✅ |
| Main menu renders with text | ✅ |
| Touch input (tap, drag, pinch) | ✅ |
| On-device stability (no crash) | ✅ stable past 170s (3 boot crashes fixed) |
| Mod loading (`mod.txt` / Intent extra) | ✅ verified on-device |
| Audio playback | ⚠️ Backend fixed (opensl); decoder blocked (FFmpeg can't build for arm64 — upstream vcpkg#33963) |
| Full gameplay session (skirmish) | ✅ (OnePlus Pad 2) |

**Tested on:**
- OnePlus Pad 2 (Snapdragon 8 Gen 3, Adreno 830, 3392×2400)
- Lenovo TB322FC (Android 16, Adreno, 1904×3040) — on-device debugging + crash fixes (2026-07-10)

---

## On-Device Debugging (2026-07-10) — Chain of Thought

First real on-device run on the Lenovo TB322FC revealed three boot-blocker crashes.
Each was root-caused, fixed, and verified. The chain of thought:

1. **"Can't even start" → SDL_free double-free.** The engine SIGABRT'd during init.
   Disassembly (`llvm-addr2line` on the crash PC) + SDL3 source reading revealed that
   `SDL_GetAndroid{External,Internal,Cache}StoragePath` cache their results in
   function-local **statics** — returning the SAME pointer every call. The bootstrap code
   freed these after each use, corrupting SDL3's cache; the next call returned a dangling
   pointer, and freeing it again was a double-free. **Fix:** remove all `SDL_free` calls
   on path results (`d2cef9631`). → Engine now boots through full init.

2. **"Boots but no main menu" → heap corruption.** After init, `Scudo ERROR: corrupted
   chunk header`. HWASAN/ASan can't load via SDL3's dlopen model (TLS IE conflict); MTE
   via `wrap` is ignored for non-debuggable apps; GWP-ASan is too probabilistic. Heap
   probes (1000 malloc/free cycles between subsystems) proved the heap was *clean* before
   `doesFileExist`. An **early-return diagnostic** (bypass the case-insensitive
   `std::filesystem` resolution in `fixFilenameFromWindowsPath`) made the crash vanish →
   the corruption was *inside* that std::filesystem code. **Fix:** early-return on
   `__ANDROID__` (the resolution is unneeded — Android is case-sensitive; `.big` archives
   handle lookups) (`8ceb2f690`). → Engine now renders the main menu.

3. **"Renders but crashes after ~2.5 min" → OOM.** `Scudo ERROR: internal map failure
   (Out of memory)`. The process reaches **VmSize ~19GB** (DXVK/Vulkan virtual
   reservations) while RSS is only ~2.5GB. When the audio system loads a file into RAM
   (`RAMFile::openFromArchive` → `new[]`), the mmap can't find contiguous space. **Fix:**
   skip audio file loading on Android (`getBufferForFile` returns 0) — audio playback
   doesn't work yet anyway (`dfe786d87`). → Engine now **stable past 170s**, no crash.

4. **Audio "no sound" root cause → backend.** OpenAL Soft defaulted to the **null** backend
   (`Created device "No Output"`). The opensl backend is compiled in but not selected.
   **Fix:** `ALSOFT_DRIVERS=opensl` env var (`f9775f3bd`). Verified: `Initialized backend
   "opensl"`, `Created device "OpenSL"`. (Real audio still needs FFmpeg — see Status.)

**Key lesson:** when on-device sanitizers are unavailable (dlopen limitation), an
**early-return diagnostic** that bypasses the suspected code path is the fastest isolation
technique — if the crash moves or vanishes, you've found the culprit subsystem.

Full details: [`android.md` §10.10](android.md) (findings 1-6, all root causes + fixes).

---

## How to Play

### What you need

1. **An Android tablet** with:
   - **arm64-v8a** architecture (all modern tablets)
   - **Android 7.0+** (API 24+, for system Vulkan support)
   - A **Vulkan-capable GPU** (Adreno 7xx+, Mali-G77+, or equivalent)
   - **~3GB free RAM** for the game process *(estimate, not yet measured on Android — the DXVK→Vulkan layer and modern high-res displays add overhead over the 2003 game's 128MB target; the iOS port measures ~3GB resident, and Android is expected to be similar. Not a memory leak.)*
   - **~2.5GB storage** for game data

2. **A legal copy of C&C Generals Zero Hour**:
   - [Steam](https://store.steampowered.com/app/2732960/) (~$5 on sale, includes base game + Zero Hour)
   - Or any retail/EA App/Origin install
   - You need the **Complete Edition** or both Generals + Zero Hour installed

### Step 1: Download the APK

Grab the latest APK from the [**Releases page**](../../releases).

### Step 2: Install the APK

```bash
# Enable "Install from unknown sources" for your file manager first
adb install GeneralsZH-full.apk

# Or transfer the APK to your tablet and tap it in Files
```

### Step 3: Copy your game data

The game needs its `.big` archive files on your tablet's filesystem:

```bash
# Create the game data directory on the tablet
adb shell mkdir -p /sdcard/Android/data/me.generalsx.zh/files/GameData/Data

# Copy ALL .big files from your PC install to the tablet
# (from your Generals install directory, typically):
#   C:\Program Files (x86)\Steam\steamapps\common\Generals\
adb push "*.big" /sdcard/Android/data/me.generalsx.zh/files/GameData/Data/

# The fonts are bundled in the APK and extract automatically on first launch.
```

**Required .big files** (copy all of these from your install's `Data/` folder):

| File | Contents |
|------|----------|
| `INI.big` | Base game INI data (weapons, objects, etc.) |
| `INIZH.big` | Zero Hour INI data |
| `Textures.big` / `TexturesZH.big` | Game textures |
| `Audio.big` / `AudioZH.big` | Sound effects |
| `Music.big` / `MusicZH.big` | Music tracks |
| `MapsZH.big` | Map data |
| `Terrain.big` / `TerrainZH.big` | Terrain data |
| `W3D.big` / `W3DZH.big` | 3D models |
| `English.big` / `EnglishZH.big` | English text/speech |
| `Speech*.big` | Voice-over files |
| `Window.big` / `WindowZH.big` | UI textures |
| `ShadersZH.big` | Shaders |

### Step 4: Play

Launch **"Generals ZH"** from your app drawer. The main menu should appear
within a few seconds.

---

## Touch Controls

The touch input system maps touchscreen gestures to the RTS mouse semantics the
2003 engine expects:

| Gesture | Action |
|---------|--------|
| **Tap** | Left-click (select unit, click button) |
| **Tap and hold (600ms)** | Right-click (context menu, deselect) |
| **Drag** | Left-click drag (selection box) |
| **Two-finger drag** | Right-click drag (camera pan) |
| **Two-finger pinch** | Mouse wheel (zoom in/out) |

---

## Build from Source

### Prerequisites

- **Android NDK r27** (27.1.12297006)
- **Android SDK** with build-tools 35.0.0
- **CMake 3.25+** and **Ninja**
- **Meson** (for DXVK cross-compilation)
- **Android Studio** or Gradle 8.7+ (for the APK packaging)

### Build steps

```bash
# Clone
git clone https://github.com/tarek369/GeneralsZH-Android.git
cd GeneralsZH-Android

# Initialize the DXVK fork submodule
git submodule update --init --recursive references/fbraz3-dxvk

# Configure + build the native engine (arm64-v8a)
cmake --preset android-vulkan
cmake --build build/android-vulkan --target z_generals

# Package the APK (stages fonts, DXVK .so, SDL3 .so, etc.)
./scripts/build/android/package-android-zh.sh

# The signed APK appears at:
#   android/app/build/outputs/apk/release/app-release.apk
```

For more details, see [`android.md`](android.md) — the complete engineering log
of every bug found and fixed during the port.

---

## Mods

The engine supports mods via the `-mod <path>` command-line argument. On Android,
this is wired through two mechanisms (see `android.md` §10 for full details):

1. **`mod.txt`** (persistent default): write a mod's directory path to
   `GameData/mod.txt` on the device. The engine reads it on launch and injects
   `-mod <path>` automatically.

2. **Intent extra** (per-launch override): `adb shell am start -n me.generalsx.zh/.GameActivity --es "mod" "/sdcard/Android/data/me.generalsx.zh/files/GameData/Mods/YourMod"`

The Intent extra takes precedence over `mod.txt`. If neither is present, the game
launches vanilla. Mod `.big` archives are loaded by the engine's existing
`loadMods()` — no recompile needed to switch mods.

**Loose files:** overrides under `$BASE/Mods/YourMod/` (e.g. `Art/`, `Data/`) resolve
automatically via `setAssetFallbackPaths`, which `loadMods()` wires when the mod dir
is loaded. They override `.big` archive contents but not loose files already in the
GameData root. Alternatively, merge loose files directly into the GameData tree (always
active while present, but not switchable via `mod.txt`).

---

## What This Port Involved

Getting a 2003 Windows DirectX 8 game running natively on Android required:

1. **DXVK for Android aarch64** — DXVK had never been built for Android. Required
   gating x86 SSE flags, fixing the SDL3 WSI soname, and a high-DPI WSI patch.
2. **Android NDK cross-compilation** of the 500k LOC engine — resolving every
   missing POSIX function (`pthread_cancel`, `glob`, `sys/timeb`), every libc++
   difference (`std::from_chars` float overload missing), and every assumption
   the engine made about having a writable filesystem.
3. **BIG archive file override** — the engine's archive system loaded base
   Generals data instead of Zero Hour data because of a `multimap::insert` hint
   ordering bug. Fixed with erase-and-reinsert to guarantee override precedence.
4. **Memory allocator coexistence** — the engine's custom DMA allocator
   intercepted all `operator delete` calls, including those from OpenAL and
   libc++. Fixed with a magic cookie check to distinguish engine allocations.
5. **DXVK device creation** — the `CreateDevice` call failed because the engine's
   fullscreen format-selection path returned `D3DFMT_UNKNOWN` on Android (no
   desktop display modes). Fixed by forcing the windowed format path on all
   non-Windows platforms.
6. **Font extraction** — Android APK assets are invisible to `fopen()`. The
   engine's FreeType font locator expects `fonts/*.ttf` on the filesystem.
   Added JNI-based extraction from `AAssetManager` on first launch.

**Full bug list with root causes and fixes:** [`android.md`](android.md)

---

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
│  Audio: OpenAL Soft (opensl backend via ALSOFT_DRIVERS) │
│  Video: FFmpeg (stubbed — vcpkg ffmpeg:arm64-android broken, upstream #33963) │
├──────────────────────────────────────────────┤
│  Android OS (arm64-v8a, API 24+)              │
└──────────────────────────────────────────────┘
```

The engine speaks DirectX 8. DXVK translates those calls to Vulkan. The Android
Vulkan driver renders to the screen. No Wine, no QEMU, no emulation — the engine
itself is compiled as a native Android shared library (`libmain.so`).

---

## Lineage & Credits

This port stands on a chain of community work:

- **Westwood / EA Pacific** — the original game
- **EA** — the GPL v3 source release
- **[TheSuperHackers](https://github.com/TheSuperHackers/GeneralsGameCode)** — community mainline: build modernization, VC6→modern toolchain, cross-platform groundwork
- **[Fighter19](https://github.com/Fighter19/CnC_Generals_Zero_Hour)** — original Unix/64-bit port: SDL3, DXVK approach, FreeType text rendering
- **[fbraz3/GeneralsX](https://github.com/fbraz3/GeneralsX)** — macOS/Linux port integrating the above
- **[ammaarreshi/Generals-Mac-iOS-iPad](https://github.com/ammaarreshi/Generals-Mac-iOS-iPad)** — iOS/iPadOS port (DXVK-on-iOS, touch controls, app lifecycle)
- **This fork** — the Android arm64 port
- **DXVK, SDL3, OpenAL Soft, Liberation Fonts** — the open-source load-bearing walls

Engine code is **GPL v3** (EA's source release → the chain above → this fork).
Game assets are not included, not licensed here, and not distributed.

---

## Legal

This project does not include or distribute any game assets, data files, or
 copyrighted game content. It is an engine port that requires the user to
 provide their own legally-obtained copy of Command & Conquer Generals Zero Hour.

Command & Conquer is a trademark of Electronic Arts Inc. This project is not
affiliated with or endorsed by Electronic Arts.
