# GeneralsZH-Android: Instructions for AI Coding Agents

## Scope and Source of Truth

This repository is the Android arm64-v8a port of Command & Conquer: Generals
Zero Hour. The original C++ engine runs natively as `libmain.so`; SDL3 loads it
through `nativeRunMain`, and DXVK translates DirectX 8 to Vulkan. There is no
emulation path.

- The only active target is Zero Hour in `GeneralsMD/` on Android.
- This fork is Android-first. Evaluate upstream or cross-platform changes for
  Android impact before accepting them.
- Preserve retail compatibility and gameplay determinism. Rendering, audio,
  storage, and platform fixes must not alter shared gameplay behavior.
- Do not distribute game assets. Test only with a legally obtained copy.

### Authoritative Worktree

The authoritative checkout is the WSL worktree:

```text
/home/rashid/.projects/GeneralsZH-Android
```

- Run Git, builds, tests, and edits from that Linux path.
- Do not edit the project through `\\wsl.localhost\...`, a Windows mirror, or a
  9P/UNC mount. These paths can be stale, diverge from Git, or cause slow and
  unreliable tool behavior.
- Before editing and before reporting results, run `git status --short` in the
  WSL worktree. Preserve unrelated changes and untracked files.
- Treat a device observation as valid only when it identifies the installed APK
  and the source revision/build that produced it.

## Context Loading

Read only the context needed for the task; do not turn trivial documentation or
single-file work into a full repository-reading exercise.

| Task | Required context |
|---|---|
| Any task | This file and the WSL worktree status |
| Android runtime, build, packaging, storage, or device work | Relevant sections of `android.md` and `CLAUDE.md` |
| C++, CMake, Gradle, scripts, or multi-module work | The applicable scoped instruction file and the relevant folder `codemap.md` |
| Unfamiliar architecture or flow | Root `codemap.md`, then the relevant folder map |
| Documentation work | `.github/instructions/docs.instructions.md` |
| Commit or PR work | `.github/instructions/git-commit.instructions.md` |

`android.md` is the long-form Android engineering log. `CLAUDE.md` is a quick
reference. The current WSL code, recent commits, and fresh verification evidence
take precedence when either historical document is stale. `context.md` is not an
authoritative source of project status.

## Architecture and Boundaries

```text
Game engine (C++)
  DirectX 8 -> DXVK -> Vulkan
  SDL3 for windowing/input
  OpenAL Soft for audio
  Android API 24+, arm64-v8a only
```

- Keep platform-specific code in `Core/GameEngineDevice/` or
  `Core/Libraries/Source/Platform/`.
- Do not add Win32, Cocoa, X11, or Android-native calls to game logic.
- Use `#if defined(__ANDROID__)` for Android-only behavior. Use `SAGE_MOBILE`
  only for behavior intentionally shared with iOS/mobile platforms.
- `GeneralsMD/` is the primary Android target. Backport to `Generals/` only for
  changes that are unambiguously shared.

## Build and Device Work

### Build Contract

- Android ABI: `arm64-v8a`; minimum API: 24.
- Build with `cmake --preset android-vulkan` and
  `cmake --build build/android-vulkan --target z_generals` unless the task
  requires a narrower command.
- Package with `scripts/build/android/package-android-zh.sh`.
- All runtime libraries must reach `android/app/src/main/jniLibs/arm64-v8a/`:
  `libdxvk_d3d8.so`, `libdxvk_d3d9.so`, `libSDL3.so`, `libSDL3_image.so`,
  `libopenal.so`, `libfreetype.so`, `libglm.so`, `libgamespy.so`,
  `libc++_shared.so`, and `libmain.so`.
- `libdxvk_d3d8.so` and `libdxvk_d3d9.so` must retain debug symbols. Stripping
  either breaks Vulkan dispatch-table resolution. Stripping debug symbols from
  `libmain.so` is permitted.
- Keep `RTS_GAMEMEMORY_ENABLE=OFF`, `RTS_BUILD_OPTION_FFMPEG=OFF`,
  `RTS_CRASHDUMP_ENABLE=OFF`, and the local Android DXVK fork unless the task
  explicitly changes and validates that contract.

### Android 16 Storage and SAF

- Android 16 blocks direct ADB writes into the app-owned
  `Android/data/me.generalsx.zh/...` tree. Do not rely on `adb mkdir` or
  `adb push` there in runbooks or validation.
- Loose legal content such as `SkirmishScripts.scb` enters through the app's
  Storage Access Framework importer. Do not add a fallback that bypasses SAF.
- The importer must remain API-24 compatible. Do not use API-26 `java.nio.file`
  APIs such as `Files.move` without deliberate desugaring and device validation.
- Every success, cancellation, and `ActivityNotFoundException` path must release
  the SDL startup gate so the app cannot remain stuck at launch.

### Device QA

- Increase the logcat buffer before verbose diagnostics:
  `adb logcat -G 16M`.
- Use bounded commands for install, launch, and log capture; do not leave
  logcat, emulators, or watchers running after a task.
- For a device claim, record the exact APK path/revision, launch result, focused
  app or screenshot evidence, and filtered logcat output. A successful install
  alone is not a runtime verification.
- Treat lint as a regression gate: distinguish an established baseline from
  findings introduced by the change, fix introduced findings, and never silence
  warnings merely to obtain a clean command.

## Android Gotchas: Do Not Regress

1. **Allocator ownership:** `MemoryPoolSingleBlock` uses magic cookie
   `0x47454d53` so global `delete` routes engine allocations to the pool and
   external/libc++ allocations to `::free()`. Do not simplify this split.
2. **BIG precedence:** Zero Hour loads base and ZH archives. The
   erase-and-reinsert sequence in `ArchiveFileSystem` deliberately makes the
   last-loaded ZH archive win; `std::multimap::insert` hints do not guarantee it.
3. **Base INI compatibility:** ZH still reads base `INI.big`; values such as
   `DAMAGE_FLESHY_SNIPER` must not be hidden behind `#if RTS_GENERALS`.
4. **DXVK presentation:** all non-Windows builds need the windowed presentation
   path and a usable fallback format when Android exposes no desktop modes.
5. **APK assets:** bundled fonts are not filesystem paths. Extract them through
   `AAssetManager` to `<GameData>/fonts/`; keep extraction idempotent.
6. **INI exceptions:** parser failures are raw integer enums, not necessarily
   `std::exception`. Use targeted Android logging to identify the real token.
7. **Android logging:** when Android diagnostics are necessary, use bounded,
   task-specific `__android_log_print` probes. Remove temporary probes once the
   evidence is captured; do not retain broad per-entry logging.
8. **Touch:** preserve the `SAGE_MOBILE` gesture path that converts touch to
   synthetic SDL mouse events: tap, long press, drag, two-finger pan, and pinch.

## Code and Documentation Rules

- Use English in code, comments, and documentation.
- For user-facing C++ changes, add a concise `GeneralsX` annotation with a valid
  keyword: `@bugfix`, `@feature`, `@performance`, `@refactor`, `@tweak`, or
  `@build`.
- Do not add empty stubs, empty catch blocks, commented-out code, speculative
  compatibility paths, or diagnostic logging without a defined removal point.
- Fix root causes; do not mask Android failures with catch-all fallbacks.
- Record material Android discoveries and fixes in `android.md`. Update the
  monthly development diary only for meaningful session progress; do not create
  ceremonial diary entries for trivial documentation edits.
- Keep active work under `docs/WORKDIR/`, user instructions under `docs/HOWTO/`,
  and historical reference under `docs/ETC/`. Do not add working files directly
  under `docs/` root.

## Git Discipline

- Do not commit, amend, rebase, push, or change unrelated files unless the user
  explicitly requests it.
- When asked to commit, use Conventional Commits and keep commits focused.
- Before reporting completion, inspect the WSL diff and status so the report
  separates this work from pre-existing changes.

## Reference Map

| Path | Responsibility |
|---|---|
| `GeneralsMD/` | Zero Hour game code and the only Android target |
| `Generals/` | Base Generals; backport target only |
| `Core/` | Shared engine, device, graphics, and platform libraries |
| `android/` | Gradle project, manifest, Java activity/importer, APK packaging |
| `cmake/` | Android/DXVK dependency and toolchain configuration |
| `scripts/build/android/` | Native build and APK packaging scripts |
| `references/fbraz3-dxvk/` | Android DXVK fork submodule |
| `Patches/` | Android DXVK patches |

## Relevant Instruction Files

| Instruction file | Applies to | Purpose |
|---|---|---|
| `.github/instructions/generalsx.instructions.md` | All files | Points to this contract |
| `.github/instructions/cpp-conventions.instructions.md` | C/C++ files | Code annotations and platform isolation |
| `.github/instructions/build.instructions.md` | CMake files | Android build and DXVK rules |
| `.github/instructions/docs.instructions.md` | Markdown files | Documentation placement and workflow |
| `.github/instructions/scripts.instructions.md` | `scripts/` | Script organization |
| `.github/instructions/git-commit.instructions.md` | Commit/PR work | Conventional Commit rules |
