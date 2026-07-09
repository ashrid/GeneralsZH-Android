# Android Mod Support Design

**Date:** 2026-07-08
**Version:** v2
**Status:** Revised after adversarial review round 2 (v1 failed — see Revision History)
**Approach:** Dual — Intent Extra (per-launch, highest priority) + Config File (persistent default)

---

## Revision History

| Version | Change |
|---------|--------|
| v1 (initial) | Realloc crash, backslash bug, placement bug found by first review — fixed |
| v1 (revised) | **Failed second adversarial review.** Four critical defects (below) |
| v2 (this) | All four v1 defects fixed; Goal 3 rescoped to match verified engine behavior |

**v1 defects fixed in v2:**

| # | Defect | v2 Fix |
|---|--------|--------|
| 1 | `modPathBuf[512]` was block-scoped, but its pointer is stored into `__argv` and read much later by `parseMod()` at `GameEngine::init` — **dangling pointer** | Buffer is `static` (same lifetime discipline as the existing `xresVal`/`yresVal` statics at `SDL3Main.cpp:758`) |
| 2 | Spec text and tests said "Intent extra overrides mod.txt", but the code read the Intent only when `mod.txt` was empty — **contradiction** | Code now reads the Intent extra *first*; `mod.txt` is the fallback. Text, code, and tests agree |
| 3 | Architecture diagram claimed `-mod` is parsed by `parseCommandLineForStartup()` | Corrected: `-mod` is in the `paramsForEngineInit` table (`CommandLine.cpp:1179`), consumed by `parseCommandLineForEngineInit()` at `GameEngine.cpp:540`, immediately before `loadMods()` at `GameEngine.cpp:543` |
| 4 | Spec claimed loose `Art/`/`Data/` files under the mod dir resolve via LocalFileSystem — **false**. Verified: `m_modDir` is consumed only by (a) `loadMods()` for `*.big`, (b) the video players (`FFmpegVideoPlayer.cpp:248`, stubbed on Android), (c) `Win32Mouse.cpp:384` cursors (not compiled on Android). Nothing registers the mod dir with generic file resolution — this is true on PC too | Goal 3 rescoped: `.big` archives in the mod dir are fully supported; loose-file overrides are supported via a documented merge-into-GameData workflow. Engine-level loose-file resolution from the mod dir is now an explicit Non-Goal / Future Enhancement |

## Problem Statement

The Generals Zero Hour engine supports mods via the `-mod <path>` command-line parameter, but on Android there is no way to pass this parameter because the app is launched via an Android Intent, not a command line. Users need a way to load mods like Xenoforce (which ships `15Xeno.big`, `15PacthXeno.big`, plus loose `Art/` and `Data/` folders) on Android.

## Goals

1. Enable mod support on Android with minimal code changes
2. Use the existing engine mod system (`-mod` parameter, `ArchiveFileSystem::loadMods()`)
3. Support single `.big` file mods and directory-based mods whose content is `.big` archives; provide a documented, working workflow for mods that also ship loose files
4. Allow switching between mods without recompiling

## Non-Goals

- Build a mod manager UI (can be added later)
- Auto-scan Mods directory (can be added later)
- Handle mod installation/extract on-device (can be added later)
- **Engine-level loose-file resolution from the mod directory** (new in v2 — requires touching generic file resolution, risking determinism; see Future Enhancements)

## Design

### Architecture (corrected in v2)

```
┌──────────────────────────────────────────────────────────┐
│  Launch                                                    │
│  Priority 1: Intent extra "mod" (per-launch, explicit)     │
│  Priority 2: GameData/mod.txt   (persistent default)       │
└───────────────────────┬────────────────────────────────────┘
                        ▼
┌──────────────────────────────────────────────────────────┐
│  SDL3Main.cpp main() — Android branch                      │
│  (chdir into GameData already done, ~line 321-335)         │
│  1. Read "mod" Intent extra via JNI (first)                │
│  2. Else read mod.txt from CWD (= GameData)                │
│  3. Validate with access(path, R_OK)                       │
│  4. Inject "-mod <path>" into __argv — STATIC buffers      │
│     BEFORE CommandLine::parseCommandLineForStartup() (666) │
└───────────────────────┬────────────────────────────────────┘
                        ▼
┌──────────────────────────────────────────────────────────┐
│  parseCommandLineForStartup()  (SDL3Main.cpp:666)          │
│  → does NOT consume -mod; unknown args are skipped         │
└───────────────────────┬────────────────────────────────────┘
                        ▼
┌──────────────────────────────────────────────────────────┐
│  GameEngine::init()  (GameEngine.cpp)                      │
│  :540 parseCommandLineForEngineInit()                      │
│       → paramsForEngineInit table (CommandLine.cpp:1179)   │
│       → parseMod() sets TheGlobalData->m_modDir (dir)      │
│         or TheGlobalData->m_modBIG (single .big)           │
│  :543 TheArchiveFileSystem->loadMods()                     │
│       → m_modBIG: openArchiveFile + loadIntoDirectoryTree  │
│       → m_modDir: loadBigFilesFromDirectory(dir,"*.big",T) │
└───────────────────────┬────────────────────────────────────┘
                        ▼
┌──────────────────────────────────────────────────────────┐
│  File Resolution Priority (verified):                      │
│  1. Loose files inside the GameData tree — HIGHEST         │
│     (LocalFileSystem checked before ArchiveFileSystem)     │
│  2. Mod archives (last-loaded wins: multimap reorder in    │
│     loadIntoDirectoryTree, ArchiveFileSystem.cpp:158-183)  │
│  3. ZH archives (INIZH.big, TexturesZH.big, ...)           │
│  4. Base Generals archives (INI.big, Textures.big, ...)    │
│                                                             │
│  NOT in this chain: loose files under the mod dir.         │
│  m_modDir is only consulted by loadMods (*.big), the       │
│  video players (stubbed on Android), and Win32 cursors     │
│  (not compiled on Android).                                │
└──────────────────────────────────────────────────────────┘
```

### Code Changes

**File modified:** `GeneralsMD/Code/Main/SDL3Main.cpp`

**Location:** Inside `main()`, after the Android GameData `chdir()` block (so `fopen("mod.txt")` resolves against GameData) and **before** `CommandLine::parseCommandLineForStartup()` at line 666. The startup parser skips unknown args, so `-mod` passes through harmlessly until `parseCommandLineForEngineInit()` consumes it. `<unistd.h>` (for `access()`) is already included — the file already calls `access()` at line 321.

```cpp
#if defined(__ANDROID__)
	// GeneralsX @feature Claude 08/07/2026 Android mod support: resolve a mod path from the
	// "mod" Intent extra (per-launch override) or GameData/mod.txt (persistent default) and
	// inject "-mod <path>" into __argv. Consumed later by parseCommandLineForEngineInit().
	{
		// static: a pointer into this buffer is stored in __argv and dereferenced by
		// parseMod() at GameEngine::init — long after this block exits. Stack would dangle.
		static char modPathBuf[512];
		modPathBuf[0] = '\0';

		// Priority 1: Intent extra (explicit per-launch: launcher apps, adb --es "mod" ...)
		JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
		if (env != nullptr)
		{
			jobject activity = (jobject)SDL_GetAndroidActivity();
			if (activity != nullptr)
			{
				jclass cls = env->GetObjectClass(activity);
				jmethodID getIntent = (cls != nullptr)
					? env->GetMethodID(cls, "getIntent", "()Landroid/content/Intent;") : nullptr;
				if (getIntent != nullptr && !env->ExceptionCheck())
				{
					jobject intent = env->CallObjectMethod(activity, getIntent);
					if (intent != nullptr && !env->ExceptionCheck())
					{
						jclass intentCls = env->GetObjectClass(intent);
						jmethodID getStringExtra = (intentCls != nullptr)
							? env->GetMethodID(intentCls, "getStringExtra",
							                   "(Ljava/lang/String;)Ljava/lang/String;") : nullptr;
						if (getStringExtra != nullptr && !env->ExceptionCheck())
						{
							jstring modKey = env->NewStringUTF("mod");
							jstring modValue = (jstring)env->CallObjectMethod(intent, getStringExtra, modKey);
							if (env->ExceptionCheck())
							{
								env->ExceptionClear();
							}
							else if (modValue != nullptr)
							{
								const char *modPath = env->GetStringUTFChars(modValue, nullptr);
								if (modPath != nullptr && modPath[0] != '\0')
								{
									snprintf(modPathBuf, sizeof(modPathBuf), "%s", modPath);
									__android_log_print(ANDROID_LOG_INFO, "GeneralsX",
										"Mod path from Intent extra: %s", modPathBuf);
								}
								if (modPath != nullptr)
									env->ReleaseStringUTFChars(modValue, modPath);
								env->DeleteLocalRef(modValue);
							}
							if (modKey != nullptr)
								env->DeleteLocalRef(modKey);
						}
						if (intentCls != nullptr)
							env->DeleteLocalRef(intentCls);
						env->DeleteLocalRef(intent);
					}
					if (env->ExceptionCheck())
						env->ExceptionClear();
				}
				if (env->ExceptionCheck())
					env->ExceptionClear();
				if (cls != nullptr)
					env->DeleteLocalRef(cls);
			}
		}

		// Priority 2: mod.txt in GameData (CWD — the chdir above already landed there)
		if (modPathBuf[0] == '\0')
		{
			FILE *modFile = fopen("mod.txt", "r");
			if (modFile != nullptr)
			{
				if (fgets(modPathBuf, sizeof(modPathBuf), modFile) != nullptr)
				{
					// Trim trailing whitespace including \r\n — the file may be Windows-edited
					size_t len = strlen(modPathBuf);
					while (len > 0 && (modPathBuf[len-1] == '\n' || modPathBuf[len-1] == '\r' ||
					                   modPathBuf[len-1] == ' '  || modPathBuf[len-1] == '\t'))
					{
						modPathBuf[--len] = '\0';
					}
					if (modPathBuf[0] != '\0')
					{
						__android_log_print(ANDROID_LOG_INFO, "GeneralsX",
							"Mod path from mod.txt: %s", modPathBuf);
					}
				}
				fclose(modFile);
			}
		}

		// Inject "-mod <path>" — static-buffer pattern (NOT realloc: __argv is main()'s argv,
		// not heap; same pattern as the -xres/-yres injection at SDL3Main.cpp:758-778)
		if (modPathBuf[0] != '\0')
		{
			if (access(modPathBuf, R_OK) == 0)
			{
				static char* modArgv[64];
				static char modFlag[] = "-mod";
				int n = 0;
				for (int i = 0; i < __argc && n < 61; ++i)
					modArgv[n++] = __argv[i];
				modArgv[n++] = modFlag;
				modArgv[n++] = modPathBuf;
				modArgv[n] = nullptr;
				__argv = modArgv;
				__argc = n;
				__android_log_print(ANDROID_LOG_INFO, "GeneralsX",
					"Injected -mod %s (argc=%d)", modPathBuf, __argc);
			}
			else
			{
				__android_log_print(ANDROID_LOG_WARN, "GeneralsX",
					"Mod path not accessible, ignoring: %s", modPathBuf);
			}
		}
	}
#endif
```

Differences from v1, beyond the four review fixes: the `activity` null check was missing; `DeleteLocalRef(modValue)` was called even when `modValue` was null; `\r` was not trimmed (Windows-edited `mod.txt` would produce a path ending in `\r` that fails `access()`); a success log line after injection was added for the verification matrix.

### Engine Bug Fix: Backslash Path Separator (unchanged from v1 — verified correct)

**Files modified:**
- `GeneralsMD/Code/GameEngine/Source/Common/CommandLine.cpp` — `parseMod()`, lines 1091-1092
- `Generals/Code/GameEngine/Source/Common/CommandLine.cpp` — `parseMod()`, lines 1089-1090 (identical bug; backport per AGENTS.md rules)

**Current code (broken on Android/POSIX — appends `\` which the VFS tokenizer at `loadIntoDirectoryTree` handles, but raw `fopen`/`opendir` paths do not):**
```cpp
if (!modPath.endsWith("\\") && !modPath.endsWith("/"))
	modPath.concat('\\');
```

**Fixed code:**
```cpp
if (!modPath.endsWith("\\") && !modPath.endsWith("/"))
{
#ifdef _WIN32
	modPath.concat('\\');
#else
	// GeneralsX @bugfix Claude 08/07/2026 POSIX path separator for mod dir
	modPath.concat('/');
#endif
}
```

### Mod Directory Structure

```
/sdcard/Android/data/me.generalsx.zh/files/GameData/
├── mod.txt                          ← optional: one line, the mod path
├── Mods/
│   ├── Xenoforce/
│   │   ├── 15Xeno.big               ← loaded by loadMods() (*.big)
│   │   └── 15PacthXeno.big          ← loaded by loadMods() (*.big)
│   ├── Contra/
│   └── RiseOfTheReds/
├── Art/                             ← mod loose-file overrides go HERE (merged),
├── Data/                            ←   NOT under Mods/<name>/ — see below
│   ├── INI.big
│   ├── INIZH.big
│   └── ...
└── ...
```

**Loose files (v2, honest contract):** the engine does not resolve loose files from the mod directory — on any platform. Loose overrides work only through `LocalFileSystem`, which resolves against the GameData tree (CWD). For mods like Xenoforce that ship `Art/` and `Data/` folders, the supported workflow is to merge those folders into `GameData/Art/` and `GameData/Data/`. Loose files then win over every archive (resolution priority 1). Trade-off: loose overrides are not switched by `mod.txt` — removing the mod means deleting the merged files. This mirrors how such mods were installed on PC.

### User Workflow

**Option A: Persistent default via mod.txt (recommended)**

```bash
# One line, the absolute mod path (no trailing spaces; LF or CRLF both fine)
adb shell "echo '/sdcard/Android/data/me.generalsx.zh/files/GameData/Mods/Xenoforce' > /sdcard/Android/data/me.generalsx.zh/files/GameData/mod.txt"
adb shell am start -n me.generalsx.zh/.GameActivity
# Disable: delete mod.txt (or empty it)
adb shell rm /sdcard/Android/data/me.generalsx.zh/files/GameData/mod.txt
```

**Option B: Per-launch via Intent extra (overrides mod.txt; for launcher apps / testing)**

```bash
adb shell am start -n me.generalsx.zh/.GameActivity \
  --es "mod" "/sdcard/Android/data/me.generalsx.zh/files/GameData/Mods/Xenoforce"
# Vanilla launch (mod.txt absent) — no extra:
adb shell am start -n me.generalsx.zh/.GameActivity
```

**Option C: Full Xenoforce install (archives + loose files)**

```bash
BASE=/sdcard/Android/data/me.generalsx.zh/files/GameData
adb shell mkdir -p $BASE/Mods/Xenoforce
# .big archives → mod dir (switchable via mod.txt)
adb push 15Xeno.big      $BASE/Mods/Xenoforce/
adb push 15PacthXeno.big $BASE/Mods/Xenoforce/
# Loose overrides → merged into the GameData tree (always active while present)
adb push Art/  $BASE/Art/
adb push Data/ $BASE/Data/
adb shell "echo '$BASE/Mods/Xenoforce' > $BASE/mod.txt"
```

### Edge Cases and Safety

**Handled by existing engine code:**
- **Mod path doesn't exist:** `parseMod()` checks `TheLocalFileSystem->doesFileExist()` and returns early (`CommandLine.cpp:1075`)
- **Not stat-able:** `_stat()` (aliased to `stat` on POSIX via `file_compat.h` + the `_S_IFDIR` define at `CommandLine.cpp:44`) guards dir-vs-file classification
- **Absolute vs relative:** `parseMod()` treats paths starting with `/`, `\`, or containing `:` as absolute; otherwise prefixes `getPath_UserData()` — Android workflows always pass absolute paths
- **Archive conflicts:** `loadIntoDirectoryTree(archive, TRUE)` makes the last-loaded archive win via the deliberate multimap erase-and-reinsert (`ArchiveFileSystem.cpp:158-183` — do not simplify)

**Safety in the injection code:**
- **Lifetime:** every buffer whose pointer escapes into `__argv` is `static`
- **Path validation:** `access(path, R_OK)` before injection; inaccessible paths log a warning and launch vanilla
- **JNI hygiene:** null check on every JNI result including `activity`; `ExceptionCheck()` after every call with `ExceptionClear()` on failure; `DeleteLocalRef` only on non-null refs
- **CRLF robustness:** trailing `\r\n`, spaces, and tabs trimmed from `mod.txt`
- **argv capacity:** injection copies at most 61 existing args + 2 + null terminator into `modArgv[64]`

**Known limitations:**
- One mod at a time (single `-mod` parameter)
- Loose-file overrides are not switched per-mod (see Mod Directory Structure)
- `m_modDir` video lookup (`FFmpegVideoPlayer.cpp:248`) is moot on Android — video is stubbed (`FFmpegFileStub.cpp`)

## Verification

Run with `adb logcat -G 16M` first (per android.md — verbose diagnostics overflow the default buffer).

1. **Vanilla:** no `mod.txt`, no extra → no "Mod path" lines in logcat; game reaches main menu
2. **mod.txt:** valid path in `mod.txt`, launch without extra → logcat shows `Mod path from mod.txt:` and `Injected -mod` — verify mod content visible in game (e.g. Xenoforce menu/units)
3. **Intent extra:** delete `mod.txt`, launch with `--es "mod" <path>` → logcat shows `Mod path from Intent extra:` and `Injected -mod`
4. **Precedence:** `mod.txt` = path A, launch with extra = path B → logcat shows `Mod path from Intent extra: <B>` and no mod.txt line (Intent wins — matches this spec's stated contract)
5. **Invalid path:** extra points at a nonexistent dir → logcat shows `Mod path not accessible, ignoring:`; game launches vanilla
6. **CRLF:** write `mod.txt` with CRLF line ending → path still injected (no trailing `\r`)
7. **Backslash fix:** with a directory mod, verify `parseMod` produces a `/`-terminated dir (DEBUG_LOG `Mod dir is '...'` in debug builds) and `loadMods` loads its `.big`s
8. **Loose-file workflow:** merge a known texture override into `GameData/Art/` → verify it wins over archive content in-game

## Future Enhancements

- **Engine-level loose-file resolution from the mod dir:** register `m_modDir` with generic file resolution so `Mods/<name>/Art|Data` overrides work and switch with the mod. Touches `FileSystem`/`LocalFileSystem` resolution order — needs determinism review; deliberately excluded from this spec
- **Auto-scan Mods directory** and mod-picker touch UI
- **Mod installation:** .zip extraction from Downloads
- **Multiple mods:** mod chains with defined precedence

## References

- `android.md` §4.2-4.3: BIG archive override precedence
- `Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp:227-251`: `loadMods()`; `:119-225`: `loadIntoDirectoryTree()` multimap dance
- `GeneralsMD/Code/GameEngine/Source/Common/CommandLine.cpp:1060-1105`: `parseMod()`; `:1179`: `-mod` in `paramsForEngineInit`
- `GeneralsMD/Code/GameEngine/Source/Common/GameEngine.cpp:540-543`: engine-init parse → `loadMods()` sequence
- `GeneralsMD/Code/Main/SDL3Main.cpp:758-778`: existing `-xres`/`-yres` static-buffer injection pattern; `:321-335`: GameData `chdir`
