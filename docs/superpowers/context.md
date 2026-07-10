# Context — Android Port On-Device Debugging

**Last updated:** 2026-07-10 (after on-device debugging session)
**State:** Main menu renders + stable on-device. 3 boot-blocker crashes fixed. Mod injection verified.

---

## Current State

The engine boots, completes full init, creates the D3D device (DXVK→Vulkan on Adreno),
enters `execute()`, and **renders the in-game main menu** — verified on a Lenovo TB322FC
(Android 16, Adreno, 1904×3040). The engine is **stable past 170 seconds** (no crash).
Mod loading (`mod.txt` → `-mod` injection → `loadMods`) is verified on-device.

All 13 on-device-debugging commits: `d2cef9631`..`35c56ba66`.

---

## Chain of Thought — The Three Boot-Blocker Crashes

### Crash 1: "Can't even start" → SDL_free double-free

**Symptom:** SIGABRT (`Scudo: invalid chunk state when deallocating`) during init, before
any subsystem loaded.

**Isolation:** `llvm-addr2line` on the crash PC → `SDL_main+628`. Disassembly showed the
crash at a `bl SDL_free` call. The code called `SDL_GetAndroidExternalStoragePath()` etc.
multiple times and freed each result.

**Root cause:** SDL3's `SDL_GetAndroid{External,Internal,Cache}StoragePath` cache their
result in a **function-local static** (`SDL_android.c`) — returning the SAME pointer on
every call. Freeing it corrupts SDL3's cache; the next call returns the dangling pointer;
freeing it again is a double-free.

**Fix:** Remove all 6 `SDL_free` calls on path results (`d2cef9631`). These are
process-lifetime cached strings that must never be freed.

**Lesson:** SDL3's Android path functions cache in statics — contradicts SDL3's own docs
which say to free them. Documented inline so no one re-adds the free.

### Crash 2: "Boots but no main menu" → heap corruption in std::filesystem

**Symptom:** After full init, `Scudo ERROR: corrupted chunk header`. Crash in
`StdLocalFileSystem::doesFileExist` → `fixFilenameFromWindowsPath` →
`std::filesystem::operator/` → `std::string::append` (the append's reallocation frees a
buffer whose Scudo header is corrupted).

**Isolation (the hard part — on-device sanitizers are all blocked):**
- HWASAN/ASan: can't load via SDL3's dlopen model (`TLS symbol "(null)" ... IE access model`)
- MTE via `wrap` property: ignored for non-debuggable apps
- GWP-ASan: probabilistic, didn't catch it in 8 runs
- Heap probes (1000 malloc/free cycles after each subsystem): **survived** — proving the
  heap was clean *before* `doesFileExist`. This initially MISLED us into thinking the
  corruption was from earlier `.big`/INI loading. (Key insight: probes only check the
  *freed* chunk's own header, not adjacent overflowed chunks — so they can miss corruption
  they don't directly free.)
- **The breakthrough:** an early-return diagnostic in `fixFilenameFromWindowsPath` that
  bypasses ALL the `std::filesystem` case-insensitive resolution code. The crash **vanished**.
  → The corruption was *inside* the resolution code (operator/ + directory_iterator), not
  from earlier loading.

**Fix:** Early-return on `__ANDROID__` — return the plain path (backslashes already
converted). The resolution is unneeded: Android is case-sensitive, and `.big` archive
lookups go through `ArchiveFileSystem` (which has its own case handling) (`8ceb2f690`).

**Lesson:** When sanitizers are unavailable, an **early-return diagnostic** that bypasses
the suspected code path is the fastest isolation technique. Also: heap probes are
inconclusive for overflow detection (they check freed-chunk headers, not adjacent ones) —
don't over-trust a "clean" probe result.

### Crash 3: "Renders but crashes after ~2.5 min" → OOM

**Symptom:** `Scudo ERROR: internal map failure (error desc=Out of memory)` ~2.5 min after
the main menu renders (when menu music starts). Tombstone: `RAMFile::openFromArchive` →
`operator new[]` → Scudo `MapAllocator` mmap fails.

**Root cause:** The process reaches **VmSize ~19GB** (virtual address space) while RSS is
only ~2.5GB — DXVK/Vulkan reserves huge virtual ranges for GPU memory management. When the
audio system loads a file into RAM (`RAMFile::openFromArchive` does `new[size]` for the
entire file), Scudo's secondary allocator (mmap) can't find contiguous space. Device has
15GB RAM / 9.6GB free; map count 26775/65530 (not at limit) — it's virtual-address
fragmentation/exhaustion, not physical RAM.

**Fix:** `OpenALAudioFileCache::getBufferForFile` returns 0 (no buffer) on Android — audio
playback doesn't work yet anyway (no FFmpeg decoder), and skipping the RAM-intensive file
load prevents the OOM (`dfe786d87`). → Engine now stable past 170s, no crash.

### Bonus: Audio "no sound" root cause → null backend

**Symptom:** README said "Audio playback ❌ no sound yet." OpenAL initializes but produces
no audio.

**Root cause:** OpenAL Soft (v1.24.2) defaulted to the **null** backend — logcat:
`Initialized backend "null"`, `Created device "No Output"`. The **opensl** (OpenSL ES)
backend IS compiled in (`Supported backends: opensl, null, wave`) but was not selected.

**Fix attempt 1 (failed):** `ALSOFT_BACKEND=opensl` — OpenAL ignores this env var.
**Fix attempt 2 (success):** `ALSOFT_DRIVERS=opensl` — found via OpenAL Soft
`docs/env-vars.txt` ("overrides the drivers config option"). Verified: `Initialized backend
"opensl"`, `Created device "OpenSL"`, `libOpenSLES` active (`f9775f3bd`).

**Remaining audio blocker:** real audio needs FFmpeg (the audio/video decoder), which is
**disabled** on Android (`RTS_BUILD_OPTION_FFMPEG=OFF`, `FFmpegFileStub.cpp` compiled).
FFmpeg can't build for arm64-android — upstream vcpkg issue microsoft/vcpkg#33963. When
FFmpeg is available + streaming is implemented (to avoid the OOM), the opensl backend will
produce actual sound.

---

## What's Verified On-Device

| Scenario | Result |
|----------|--------|
| Engine boots through full init | ✅ |
| Loads all `.big` files + parses INI | ✅ |
| Creates D3D device (DXVK→Vulkan on Adreno) | ✅ |
| Main menu renders | ✅ (user confirmed) |
| Stable past 170s (no crash) | ✅ |
| Touch input delivered | ✅ (MotionEvent reaches GameActivity) |
| Mod injection (`mod.txt` → `-mod` → `loadMods`) | ✅ (§10.9 scenario 2 PASS) |
| OpenAL opensl backend selected | ✅ (`ALSOFT_DRIVERS=opensl`) |

## What's Next

1. **Real audio** — BLOCKED upstream (FFmpeg vcpkg#33963). Backend (opensl) is fixed &
   ready. When FFmpeg builds for arm64-android: re-enable `getBufferForFile` (undo the
   `#if __ANDROID__ return 0`), implement `StreamingArchiveFile` to avoid the VmSize 19GB
   OOM, then audio will play.
2. **Verification matrix** (android.md §10.9) — scenarios 1, 3-10 not yet run. Scenario 2
   (mod.txt) PASS. The rest need interactive menu testing (touch navigation).
3. **Memory investigation** — VmSize 19GB is high (DXVK/Vulkan reservations). If stability
   issues arise under memory pressure, investigate reducing DXVK virtual reservations
   (dxvk.conf tuning) or thread stack sizes.

## Key Files Changed This Session

- `GeneralsMD/Code/Main/SDL3Main.cpp` — SDL_free fix + ALSOFT_DRIVERS=opensl
- `Core/GameEngineDevice/Source/StdDevice/Common/StdLocalFileSystem.cpp` — std::filesystem bypass
- `Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALAudioCache.cpp` — audio file load skip
- `Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFileSystem.cpp` — bounds-check + closeAllFiles
- `android.md §10.10` — findings 1-6 (all root causes + fixes)
- `android.md §10.9` — verification matrix (scenario 2 PASS)

## Non-Negotiable Invariants (DO NOT TOUCH)

1. `ArchiveFileSystem.cpp:158-183` — multimap erase-and-reinsert dance
2. `GameMemory.h/cpp` — magic cookie `0x47454d53`
3. BIG on-disk format — 32-bit big-endian
4. `libdxvk_d3d8.so`/`libdxvk_d3d9.so` must NOT be stripped (breaks Vulkan dispatch)
