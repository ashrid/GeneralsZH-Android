# Android Mod Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Version:** v5
**Source spec:** `docs/superpowers/specs/2026-07-08-android-mod-support-design.md` (v2)
**Workflow:** every task runs through `docs/WORKDIR/android-safe-build-loop.md` (preflight → commit-gated build → triage ladder → package → logcat matrix → one human brief → docs-close). Execution environment per `docs/WORKDIR/2026-07-09-wsl-handoff.md`: the WSL tree is authoritative; all builds/packaging/preflight run inside WSL (`wsl.exe -d Ubuntu -e bash -lc '...'`); adb is `adb.exe` (Windows platform-tools).

**Goal:** Make the existing `-mod` engine mod system usable on Android via Intent-extra / `mod.txt` argv injection, and verify it end-to-end with the Xenoforce mod.

**Architecture:** Two small changes: (1) a POSIX path-separator fix in `parseMod()` (both games), and (2) an Android-only block in `SDL3Main.cpp` `main()` that resolves a mod path (Intent extra first, `mod.txt` fallback) and injects `-mod <path>` into `__argv` using the established static-buffer pattern. Everything downstream (`parseCommandLineForEngineInit` → `parseMod` → `loadMods`) is existing, unmodified engine code.

**Tech Stack:** C++ (engine), JNI via SDL3 (`SDL_GetAndroidJNIEnv`/`SDL_GetAndroidActivity`), CMake `android-vulkan` preset + `package-android-zh.sh`, adb/logcat verification.

---

## Revision History

| Version | Change |
|---------|--------|
| v1 | 18-task / 6-wave architecture plan (ModManager, memory budget, GUI, profiles). **Failed adversarial review:** no task implemented the spec baseline it claimed as its foundation; P5.1 solved an already-solved problem (`file_compat.h` + `_S_IFDIR` define exist at `CommandLine.cpp:44`); Wave 3 dispatched P5.2 concurrently with its dependency P3.1; execution step 1 said "confirm the 4 open questions" although all were marked RESOLVED; GUI/profiles/eviction implemented the spec's declared Non-Goals |
| v2 | Plan implements the spec v2 baseline only. v1's architecture phases moved to the Deferred Backlog appendix (pruned of the already-solved P5.1, dependency ordering fixed, resolved decisions preserved) |
| v3 | Aligned with the WSL migration (2026-07-09) and the confirmed safe-build-loop workflow: added Task 0 (remaining one-time setup — toolchain, submodule, packager `BUILD_DIR` fix, `preflight.sh`, adb); Task 3 rewritten to run the build loop (preflight, tee'd commit-named log, triage ladder) and its stale `build/android-game` caveat removed; Task 4 commands switched to `adb.exe`; Task 5 framed as the loop's docs-close commit |
| v4 | Deferred backlog feasibility study completed (2026-07-09) via 6 parallel explore agents. All 8 items reassessed against actual codebase. D5 rated Trivial (15-35 LOC), D2/D3/D4/D6 rated Practical, D1 rated Hard (greenfield test infra), D7 rated Worth Exploring, D8 split into D8a (cheap ~20-line fallback) and D8b (proper layered resolution). Phase 2 added — Tasks 6-13 pull all feasible items into active plan. User decisions: D1 uses doctest; D7 uses dynamic main-menu button (proven pattern via `updateNotifyButton`); D8a ships first, D8b deferred pending D8a validation. |
| v5 (this) | Adversarial audit (2026-07-10) of the committed baseline (Tasks 0.3/0.4, 1, 2, 6 + preflight hardening `d17b70bbe`–`ab31ec48c` + fontconfig gating `2b3d3b7d4`) via a 4-member hyperplan team — 3 rounds: independent analysis → cross-attack → defend/refine/concede. 40 raw findings → **21 survived (F1–F21)**, 5 withdrawn, 5 verified clean. **Phase 1.5 added** — Tasks T1–T9 remediate the committed baseline before the Task 3 build can succeed. Severity: 1 CRITICAL (preflight check5 regex misses `#ifdef`/`#ifndef`), 3 HIGH (Gradle stale `build/android-game` srcDir, check6 no-op for annotated files, hardcoded `darwin-x86_64` NDK path), 7 MEDIUM, 10 LOW. Lead did NOT write the remediation plan — bundle was handed to the `plan` agent (ses_0b723304). |

## Why no TDD harness in this plan

The repo has no test infrastructure (`Core/Tests/` does not exist), and both changes are thin platform glue around engine singletons (`TheGlobalData`, `TheLocalFileSystem`) that only run meaningfully on-device. Verification is therefore: compile → package → an explicit on-device logcat/behavior matrix (Task 4) with exact commands and expected output. Building a host-side harness is Deferred Backlog item D1 — doing it first would invert the effort/value ratio for a two-file change.

## File Structure

| File | Change | Responsibility |
|------|--------|----------------|
| `GeneralsMD/Code/GameEngine/Source/Common/CommandLine.cpp:1091-1092` | Modify | `parseMod()` — platform-correct trailing separator for mod dirs |
| `Generals/Code/GameEngine/Source/Common/CommandLine.cpp:1089-1090` | Modify | Same fix, base game (backport per AGENTS.md) |
| `GeneralsMD/Code/Main/SDL3Main.cpp` (before line 666) | Modify | Android-only mod-path resolution + `-mod` argv injection |
| `android.md` | Modify | New "Mod support" section: workflow, precedence, loose-file contract |
| `docs/DEV_BLOG/2026-07-DIARY.md` | Modify | Diary entry |
| `README.md` | Modify | One short user-facing "Mods" subsection |

No files are created. No engine resolution logic changes — determinism is untouched.

---

### Task 0: One-time environment setup (workflow spec steps 2–8)

Remaining setup from `docs/WORKDIR/android-safe-build-loop.md` §One-time setup (step 1, the WSL migration, is done). All commands run inside WSL. No mod code is written until every gate below passes.

**Files:**
- Modify: `scripts/build/android/package-android-zh.sh:32-34`
- Create: `scripts/build/android/preflight.sh`

- [ ] **Step 1: Provision the WSL toolchain**

Android NDK (export `ANDROID_NDK_HOME` in `~/.bashrc`), CMake ≥3.25, Ninja, Meson (DXVK-from-source), JDK 17, vcpkg per repo docs.
Gate: `cmake --preset android-vulkan` configures cleanly.

- [ ] **Step 2: Init the DXVK submodule**

Run: `git submodule update --init --recursive references/fbraz3-dxvk` (network).

- [ ] **Step 3: Fix the packager build-dir mismatch (decision already confirmed — don't re-ask)**

In `scripts/build/android/package-android-zh.sh:34` set `BUILD_DIR="${REPO_ROOT}/build/android-vulkan"`, and correct the false comment at lines 32–33 (the preset builds into `build/${presetName}` = `build/android-vulkan`, per `CMakePresets.json:287`). Annotate `# GeneralsX @build`.

- [ ] **Step 4: Create `scripts/build/android/preflight.sh`**

Per the workflow spec's Guardrails section — 7 checks: tracked-tree clean (untracked files tolerated — see workflow §Guardrails) + HEAD hash, DXVK `keepDebugSymbols` intact, memory-pool cookie `0x47454d53` present, ArchiveFileSystem multimap dance intact, no new base-INI gating in the commit's diff, `GeneralsX @` annotation in every changed source file, packager `BUILD_DIR` ends in `android-vulkan`. Fails loud, cites the doc anchor, exits non-zero.

- [ ] **Step 5: adb sanity**

`adb.exe devices` shows the tablet; `adb.exe logcat -G 16M` once per device boot. (Game data already pushed per android.md; Xenoforce mod files per spec Option C.)

- [ ] **Step 6: Commit the setup changes**

```bash
git add scripts/build/android/package-android-zh.sh scripts/build/android/preflight.sh
git commit -m "build(android): fix packager BUILD_DIR to preset path; add preflight guardrails

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 1: POSIX path separator fix in `parseMod()`

**Files:**
- Modify: `GeneralsMD/Code/GameEngine/Source/Common/CommandLine.cpp:1089-1092`
- Modify: `Generals/Code/GameEngine/Source/Common/CommandLine.cpp:1087-1090`

- [ ] **Step 1: Apply the fix in GeneralsMD**

In `parseMod()`, the dir branch currently reads (GeneralsMD lines 1089-1092):

```cpp
	if (statBuf.st_mode & _S_IFDIR)
	{
		if (!modPath.endsWith("\\") && !modPath.endsWith("/"))
			modPath.concat('\\');
```

Replace the two `endsWith`/`concat` lines with:

```cpp
		if (!modPath.endsWith("\\") && !modPath.endsWith("/"))
		{
#ifdef _WIN32
			modPath.concat('\\');
#else
			// GeneralsX @bugfix Claude 08/07/2026 POSIX path separator for mod dir —
			// a '\'-terminated path breaks loadBigFilesFromDirectory's opendir on Android
			modPath.concat('/');
#endif
		}
```

- [ ] **Step 2: Apply the identical fix in Generals**

Same edit in `Generals/Code/GameEngine/Source/Common/CommandLine.cpp` (the `endsWith`/`concat` pair at lines 1089-1090; the surrounding `parseMod()` is byte-identical to GeneralsMD's).

- [ ] **Step 3: Verify no other `concat('\\')` remains in either `parseMod`**

Run: `grep -n "concat('\\\\\\\\')" GeneralsMD/Code/GameEngine/Source/Common/CommandLine.cpp Generals/Code/GameEngine/Source/Common/CommandLine.cpp`
Expected: matches only inside the new `#ifdef _WIN32` branches.

- [ ] **Step 4: Commit**

```bash
git add GeneralsMD/Code/GameEngine/Source/Common/CommandLine.cpp Generals/Code/GameEngine/Source/Common/CommandLine.cpp
git commit -m "fix(mod): use POSIX separator for -mod directory paths on non-Windows

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: Mod path injection in `SDL3Main.cpp`

**Files:**
- Modify: `GeneralsMD/Code/Main/SDL3Main.cpp` — insert inside `main()`, after the Android GameData `chdir()` block (ends ~line 340) and before `CommandLine::parseCommandLineForStartup()` (line 666). Place it immediately before the `parseCommandLineForStartup()` call so the whole argv story is settled before any parsing.

**Why this placement works:** `fopen("mod.txt", "r")` is relative to CWD, and the Android branch has already `chdir()`'d into GameData (`SDL3Main.cpp:321-335`). `-mod` is NOT consumed by `parseCommandLineForStartup` (it's in the `paramsForEngineInit` table, `CommandLine.cpp:1179`); the startup parser skips unknown args, so early injection is harmless and both parsers see one consistent argv. `<unistd.h>` is already included (the file calls `access()` at line 321).

**Lifetime rule (the v1 review-killer):** every buffer whose pointer escapes into `__argv` must be `static` — `parseMod()` dereferences it at `GameEngine::init` (`GameEngine.cpp:540`), long after this block exits. Same discipline as the existing `-xres`/`-yres` injection at `SDL3Main.cpp:758-778`.

- [ ] **Step 1: Insert the injection block**

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

- [ ] **Step 2: Sanity-check the anchors**

Run: `grep -n "parseCommandLineForStartup\|static char modPathBuf" GeneralsMD/Code/Main/SDL3Main.cpp`
Expected: `modPathBuf` line number is smaller than the `CommandLine::parseCommandLineForStartup();` line number.

- [ ] **Step 3: Commit**

```bash
git add GeneralsMD/Code/Main/SDL3Main.cpp
git commit -m "feat(android): inject -mod from Intent extra or GameData/mod.txt

Intent extra takes precedence (per-launch, explicit); mod.txt is the
persistent default. Static buffers only — pointers escape into __argv
and are read at GameEngine::init.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: Build and package (via the safe build loop)

**Files:** none (build only). All steps run inside WSL; the tree must be clean (Tasks 1–2 committed) — preflight enforces this.

- [ ] **Step 1: Preflight**

Run: `bash scripts/build/android/preflight.sh`
Expected: all 7 guardrail checks pass; prints the HEAD short-hash this build is named after. Any failure stops the run before compiling.

- [ ] **Step 2: Build the engine**

Run: `cmake --build build/android-vulkan --target z_generals 2>&1 | tee build-$(git rev-parse --short HEAD).log`
Expected: exits 0, `libmain.so` relinked. On failure, follow the workflow's triage ladder: read the *first* compiler error; fix as a new commit and re-enter at preflight; clean reconfigure only if provably cache-borne; two failed attempts on the same error → stop and switch to systematic debugging.

- [ ] **Step 3: Package + sign + install**

Run: `bash scripts/build/android/package-android-zh.sh --install`
Expected: script verifies all 10 runtime `.so` libs staged, `assembleRelease` succeeds, APK signed with debug keystore, installed to the connected device. Keep the previous green APK as `app-release-<hash>.apk` before overwriting.

---

### Task 4: On-device verification matrix

**Files:** none (verification only). Prereqs: game data already pushed per android.md; a mod's `.big` files pushed to `$BASE/Mods/Xenoforce/`.

All adb commands use `adb.exe` (Windows platform-tools — works from PowerShell and WSL bash).

```bash
BASE=/sdcard/Android/data/me.generalsx.zh/files/GameData
adb.exe logcat -G 16M     # once per device boot — default 256KB buffer hides the interesting lines
```

Helper to relaunch clean and grab the mod lines:

```bash
adb.exe shell am force-stop me.generalsx.zh && adb.exe logcat -c
# ... launch per scenario ...
adb.exe logcat -d -s GeneralsX:V | grep -i "mod"
```

- [ ] **Scenario 1 — vanilla:** no `mod.txt`, launch `adb.exe shell am start -n me.generalsx.zh/.GameActivity`.
Expected: NO `Mod path` / `Injected -mod` lines; game reaches main menu.

- [ ] **Scenario 2 — mod.txt:** `adb.exe shell "echo '$BASE/Mods/Xenoforce' > $BASE/mod.txt"`, launch without extra.
Expected logcat: `Mod path from mod.txt: /sdcard/.../Mods/Xenoforce` and `Injected -mod ... (argc=N)`. In-game: Xenoforce content visible (modded menu/units).

- [ ] **Scenario 3 — Intent extra:** `adb.exe shell rm $BASE/mod.txt`, launch with `--es "mod" "$BASE/Mods/Xenoforce"`.
Expected logcat: `Mod path from Intent extra: ...` and `Injected -mod ...`.

- [ ] **Scenario 4 — precedence:** `mod.txt` = `$BASE/Mods/Xenoforce`, launch with `--es "mod" "$BASE/Mods/Contra"` (any second valid dir works; an empty dir is fine for the log assertion).
Expected logcat: `Mod path from Intent extra: .../Contra`; NO `from mod.txt` line. This is the spec's precedence contract — Intent wins.

- [ ] **Scenario 5 — invalid path:** launch with `--es "mod" "/sdcard/nonexistent"`.
Expected logcat: `Mod path not accessible, ignoring: /sdcard/nonexistent`; game launches vanilla.

- [ ] **Scenario 6 — CRLF mod.txt:** `adb.exe shell "printf '$BASE/Mods/Xenoforce\r\n' > $BASE/mod.txt"`, launch without extra.
Expected: same as Scenario 2 (trailing `\r` trimmed; path passes `access()`).

- [ ] **Scenario 7 — loose-file workflow (spec Goal 3):** push one recognizable texture/INI override from the mod's loose `Art/` or `Data/` into the GameData tree (e.g. `adb.exe push Art/ $BASE/Art/`), relaunch with the mod active.
Expected: the loose override is visible in-game (loose files beat all archives — resolution priority 1). Then remove it and verify the archive version returns. This validates the documented merge-into-GameData contract; loose files under `$BASE/Mods/Xenoforce/` are expected to do nothing.

- [ ] **Record results + the human brief (the loop's one checkpoint):** capture the actual logcat lines per scenario, then deliver ONE brief: commit hash built, build status + fix-chain summary, staged-libs verification, per-scenario logcat verdict with actual lines, and the eye-check list only the user can answer (menu reached, mod content visible, no visual corruption). Any machine-checkable deviation from Expected → treat as build failure (triage ladder), do not proceed to docs claiming success. User pass → Task 5; fail → fix as a new commit and re-enter the loop at preflight.

*Note:* scenarios needing `DEBUG_LOG` output require a debug-configured build — flag in the brief if skipped on RelWithDebInfo.

---

### Task 5: Documentation (the loop's docs-close — green runs only)

Runs only after the Task 4 brief comes back pass. ONE commit closes the run (workflow step 7: docs update before the closing commit of a run, not every commit).

> **Status (2026-07-10):** Device-independent docs are DONE (android.md §10.4/10.6/10.7/10.8
> + DEV_BLOG entry, commit `539dc302f`). The loose-file contract was updated to reflect
> Task 13 (D8a): mod-dir loose files now resolve via `setAssetFallbackPaths`. Only the
> verification-matrix results (Step 1's Task 4 portion) remain, gated on a connected device.

**Files:**
- Modify: `android.md` — new "Mod support" section
- Modify: `docs/DEV_BLOG/2026-07-DIARY.md` — entry for 08/07/2026
- Modify: `README.md` — short "Mods" subsection under install instructions

- [x] **Step 1: android.md section (device-independent portion DONE)** — document: the injection point and precedence (Intent > mod.txt), the static-buffer lifetime rule, the loose-file contract (Task 13/D8a now IMPLEMENTED: mod-dir loose files resolve via `setAssetFallbackPaths`, wired in `loadMods()`; `.big` archives still via `loadBigFilesFromDirectory`; video players stubbed; Win32 cursors not compiled). Verification matrix results from Task 4 — PENDING (device-blocked). Option C full-install commands — DONE.
- [ ] **Step 1 (remaining): verification matrix results** — gated on Task 4 on-device logcat.

- [ ] **Step 2: DEV_BLOG entry** — what shipped, the v1→v2 review findings (dangling pointer, precedence contradiction, wrong parse stage, loose-file gap), links to spec/plan v2.

- [ ] **Step 3: README "Mods" subsection** — user-facing: mod.txt workflow (Option A), Intent extra one-liner, loose-files-merge caveat.

- [ ] **Step 4: Commit**

```bash
git add android.md docs/DEV_BLOG/2026-07-DIARY.md README.md
git commit -m "docs(android): mod support workflow, precedence, and verification results

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Success Criteria

1. All 7 verification scenarios pass with the exact expected logcat lines / in-game behavior
2. Xenoforce `.big` content demonstrably active in-game via `mod.txt` alone (no recompile to switch mods)
3. Vanilla launch is behavior-preserving when no mod is configured (the injection block still executes JNI calls to probe the Intent extra, but no `-mod` is injected and no game state changes — "byte-identical" was overstated; see audit F20)
4. Legacy `-mod <path>` on desktop platforms unchanged (no non-Android code paths touched except the `#ifdef`'d separator fix)
5. android.md, DEV_BLOG, README updated with measured results, not claims

---

## Phase 1.5 — Adversarial Audit Remediation (2026-07-10)

> **GATE:** Tasks T1–T8 must land and T9 must pass **before Task 3 (build) can succeed.** F2 (Gradle srcDir) and F4 (NDK host path) currently break the build/package path on the primary WSL dev platform; F1 (preflight regex) is a guardrail gap that could silently let a regression through.

### Why this section exists

Before building Task 3+ on top of the committed baseline, the executed work was put through a 4-member adversarial hyperplan audit (3 rounds: independent analysis → cross-attack → defend/refine/concede). The Lead orchestrated but did **not** write the remediation plan — the distilled finding bundle was handed to the `plan` agent (ses_0b723304), which owns sequencing, parallelization, and verification gates. This section is that agent's output, verbatim in structure.

### Committed baseline at audit time (all on `main`)

| Task | Commit | Status |
|------|--------|--------|
| 0.3 + 0.4 | `1ad4b2c0d` | ✅ committed — audited (preflight, packager BUILD_DIR) |
| 1 | `93a800398` | ✅ committed — audited (parseMod POSIX separator) |
| 2 | `5ad15e9ac` | ✅ committed — audited (SDL3Main `-mod` injection) |
| 6 | `66a7bc903` | ✅ committed — audited (ModuleFactory alias seam) |
| preflight hardening | `d17b70bbe` … `ab31ec48c` (5 commits) | ✅ committed — audited |
| fontconfig gating | `2b3d3b7d4` | ✅ committed — audited |

### Audit methodology

4-member adversarial team, each with a distinct lens (hyperplan roster — `deep` omitted; audit is critique, not autonomous implementation):

| Member | Lens | Model |
|--------|------|-------|
| logic-auditor | memory safety, object lifetime, JNI ref mgmt, logic correctness | Kimi (max) |
| spec-subverter | contract violations, comment-vs-code lies, spec flaws | MiMo v2.5-pro |
| integration-breaker | downstream breakage, caller/callee impact, cross-platform regression | DeepSeek v4-pro |
| edge-assaulter | input fuzzing — empty/huge/unicode/CRLF/overflow/concurrent | DeepSeek v4-flash |

40 raw findings → cross-attack → defense → **21 survived (F1–F21)**, **5 withdrawn** (attacked successfully), **5 verified clean** (2+ members agree).

### Surviving findings — CRITICAL

| ID | Finding | File:line | Task |
|----|---------|-----------|------|
| F1 | preflight check5 regex misses `#ifdef` / `#ifndef` / `#if !` forms of `RTS_GENERALS` gating. `#ifdef RTS_GENERALS` already exists at `ControlBar.cpp:3562,3565` — a new such gate would pass preflight and could reintroduce the FLESHY_SNIPER-class bug (android.md §4.1). Adversarial red-path test only probes `#if RTS_GENERALS`. | `preflight.sh:70` | T7 + T8 |

### Surviving findings — HIGH

| ID | Finding | File:line | Task |
|----|---------|-----------|------|
| F2 | Gradle SDL3 Java srcDir points at stale `build/android-game/_deps/sdl3-src/...`. The `.cxx` fallback (lines 120-129) fails on fresh checkout (no `.cxx` before first build) → SDL3 Java sources missing → Java compile error (GameActivity can't find SDLActivity superclass). | `android/app/build.gradle:116` | T1 |
| F3 | check6 is a no-op for existing annotated files: `git show HEAD:${f} \| grep "GeneralsX @"` searches the whole file, not the diff. `SDL3Main.cpp` has 19 existing annotations → any modification passes check6 without a new annotation. Adversarial test only covers a *new* unannotated file. | `preflight.sh:85` | T7 + T8 |
| F4 | Packager hardcodes `darwin-x86_64` NDK host path. Line 89 silently skips `libc++_shared.so` on WSL/Linux → missing `.so` → `dlopen` crash at launch. Line 95 `llvm-strip` not found + `set -euo pipefail` → unconditional script abort on the primary dev platform. Line 37 default `ANDROID_NDK_HOME` is macOS-only. | `package-android-zh.sh:89,95` | T2 |

### Surviving findings — MEDIUM

| ID | Finding | File:line | Task |
|----|---------|-----------|------|
| F5 | mod.txt UTF-8 BOM not stripped. Windows Notepad saves BOM by default → the most common mod-editing workflow (Windows → Android) fails silently: BOM bytes become path prefix, `access()` fails, logcat shows garbled path. | `SDL3Main.cpp:740` | T6 |
| F6 | Intent extra path is NOT trimmed, while mod.txt IS (lines 743-748). Same buffer, inconsistent preprocessing based on source. Trailing space kills the mod silently. | `SDL3Main.cpp:706-709` | T6 |
| F7 | `snprintf` / `fgets` silent truncation at 512 bytes with no warning. A truncated path could accidentally match a different directory prefix. | `SDL3Main.cpp:709,740` | T6 |
| F8 | Stale `build/android-game` doc references contradict the BUILD_DIR fix. Users following docs hit "No such file or directory". | `android.md`, `README.md`, `AGENTS.md` | T3 |
| F9 | DXVK submodule reset silently discards the developer's local work (`checkout -- . 2>/dev/null \|\| true`). Legit non-patch DXVK changes vanish with zero feedback. | `preflight.sh:27` | T7 |
| F10 | check7 validates the packager's BUILD_DIR but not Gradle's — both must stay in sync. | `preflight.sh:101` | T7 |
| F11 | Trailing backslash on POSIX escapes the Task 1 fix: `!endsWith("\\") && !endsWith("/")` is FALSE for a trailing `\`, so no separator is appended. On POSIX `\` is a literal filename char → `opendir` fails. | `CommandLine.cpp:1091` (both copies) | T4 |

### Surviving findings — LOW

| ID | Finding | File:line | Task |
|----|---------|-----------|------|
| F12 | Self-alias (`aliasKey == existingKey`) accepted — wastes 8 lookup iterations every resolution. | `ModuleFactory.cpp:714` (both copies) | T5 |
| F13 | Empty `aliasName` accepted — registers an alias for the empty-string key. | `ModuleFactory.cpp:714` (both copies) | T5 |
| F14 | `m_aliasMap` never cleared by `reset()` (intentionally a no-op). Stale aliases from a previously-loaded mod persist within a session. Documented debt for the Task 9 ModManager teardown. | `ModuleFactory.h:74` | T5 (debt doc) |
| F15 | preflight merge-commit blind spot: `HEAD~1` compares against first parent only. | `preflight.sh:69` | T7 (comment) |
| F16 | preflight initial-commit bypass: check5 skipped when HEAD has no parent. | `preflight.sh:65-76` | T7 (comment) |
| F17 | Stale adversarial-test header comment (still says check1 rejects untracked files). | `test-preflight-adversarial.sh:1-12` | T8 |
| F18 | Hardcoded `\\` in `FFmpegVideoPlayer`/`BinkVideoPlayer`/`Win32Mouse` modDir paths — same root cause as Task 1, but dead code on Android (stubbed/not compiled). Latent if ever un-stubbed. | (3 files) | doc only |
| F19 | check1 relaxation (now tolerates untracked files via `--untracked-files=no`) is undocumented in this plan — Success Criteria still says "clean tree". | `plan` §Success Criteria | T3 |
| F20 | "byte-identical" vanilla contract overstated — the Task 2 block executes JNI calls even in vanilla mode. Behavior is unchanged but the wording is imprecise. | `plan` §Success Criteria #3 | T3 |
| F21 | CESU-8 overlong NUL (`\xC0\x80`) from JNI unguarded — Java embedded NUL encodes to an overlong UTF-8 sequence that `access()` rejects or treats as a literal byte. | `SDL3Main.cpp:709` | T6 |

### Withdrawn findings (attacked successfully — no action)

| ID | Finding | Why withdrawn |
|----|---------|---------------|
| E3 | `\v`/`\f` not trimmed from mod.txt | Practically impossible; `isspace()` hardens for free |
| E5 | `__argc ≥ 62` drops argv entries | Unreachable on Android (SDL3 provides 1–5 args) |
| E10 | 3-node circular alias resolves to wrong module | Code implements the plan's bounded-by-design 8-hop guard correctly |
| E13 | `m_aliasMap` not serialized | Template map is also not serialized; aliases are runtime-only; `xfer()` is correct |
| E17 | BUILD_DIR whitespace brittleness | Bash does not permit whitespace around `=` |

### Verified clean (confirmed correct by 2+ members — no action)

- **Task 2 argv injection flow**: static-buffer lifetime correct, `__argv`/`__argc` bridge consumed correctly by both `parseCommandLineForStartup` and `parseCommandLineForEngineInit`.
- **Task 2 JNI ref management**: every local ref (`cls`, `intent`, `intentCls`, `modKey`, `modValue`) released; `GetStringUTFChars`/`ReleaseStringUTFChars` paired on every path including early exits; pending exceptions cleared.
- **Task 2 modArgv bounds**: `n < 61` is exactly correct for 64 slots (61 originals + `-mod` + path + null).
- **Task 1 POSIX separator**: GeneralsMD/Generals byte-identical, Win32 path unchanged, `AsciiString::concat(char)` correct overload.
- **Task 6 ModuleFactory alias**: `findModuleTemplate` remains pure/idempotent for the non-alias case; no downstream breakage; 8-hop bound prevents infinite loops.
- **Fontconfig gating**: strictly `arm64-android` only (`!windows & !ios & !android`); no linux64/macos collateral.

### Remediation task graph

```
Wave 1 (7 parallel — no shared files, start immediately):
├── T1: Gradle SDL3 java srcDir → android-vulkan          (F2)        [quick]
├── T2: NDK host-tag detection in packager                 (F4)        [unspecified-low]
├── T3: Doc path reconciliation                            (F8,F19,F20)[writing]
├── T4: POSIX backslash normalize in parseMod ×2           (F11)       [unspecified-low]
├── T5: ModuleFactory alias guards + debt doc ×2           (F12-14)    [unspecified-low]
├── T6: SDL3Main mod-path hardening                        (F5-7,F21)  [unspecified-high]
└── T8: Extend adversarial preflight test — RED            (F1,F3,F17) [unspecified-low]

Wave 2 (after Wave 1):
└── T7: Harden preflight.sh — GREEN                        (F1,F3,F9,F10,F15,F16) [unspecified-high]
         depends: T8 (makes its probes pass) + T1 (F10 validates the gradle value)

Wave 3 (after Wave 2):
└── T9: Final verification gate (preflight + adversarial + build + residue)
```

**Critical path: T8 → T7 → T9.** TDD order: T8 (intentionally red on the new probes) lands **before** T7 (green).

---

### Task T1: Fix Gradle SDL3 Java srcDir (F2)

**Files:** `android/app/build.gradle:116`

Replace the stale srcDir:
```groovy
// GeneralsX @bugfix Claude 10/07/2026 F2: srcDir pointed at stale build/android-game
def sdl3JavaSrc = file("../../build/android-vulkan/_deps/sdl3-src/android-project/app/src/main/java")
```
**QA:** `grep -n 'sdl3-src' android/app/build.gradle` shows `android-vulkan`; no `android-game` on the srcDir line.

---

### Task T2: Detect NDK host tag in packager (F4)

**Files:** `scripts/build/android/package-android-zh.sh:37,89,95`

Add host detection after `cd "${REPO_ROOT}"`, then use `${NDK_HOST_TAG}` at lines 89 and 95 in place of the hardcoded `darwin-x86_64`:
```bash
# GeneralsX @bugfix Claude 10/07/2026 F4: NDK prebuilt dir is host-specific;
# hardcoding darwin-x86_64 silently drops libc++_shared.so on WSL/Linux.
case "$(uname -s)" in
    Darwin)               NDK_HOST_TAG="darwin-x86_64"  ;;
    Linux)                NDK_HOST_TAG="linux-x86_64"   ;;
    MINGW*|MSYS*|CYGWIN*) NDK_HOST_TAG="windows-x86_64" ;;
    *) echo "ERROR: unsupported host OS: $(uname -s)" >&2; exit 1 ;;
esac
```
Also add a Linux fallback default for `ANDROID_NDK_HOME` at line 37 (e.g. `${HOME}/Android/Sdk/ndk/27.1.12297006`).
**QA:** `bash -n scripts/build/android/package-android-zh.sh` (syntax OK); `grep -n 'darwin-x86_64' package-android-zh.sh` returns only the `Darwin)` arm.

---

### Task T3: Reconcile doc build-dir references (F8, F19, F20)

**Files:** `README.md:135-136`, `AGENTS.md` §10, `android/codemap.md`, `android.md:59,62,84-105`, `docs/WORKDIR/android-safe-build-loop.md` §Guardrails, `docs/superpowers/plans/2026-07-08-android-mod-architecture.md` §Success Criteria

Mechanical token-replace `android-game` → `android-vulkan` (and `cmake --preset android-game` → `android-vulkan`) in active instruction paths. AGENTS.md §10 rewritten: both Gradle (post-T1) and packager now use `android-vulkan`; keep a one-line historical note. The obsolete manual zipalign prose in `android.md:84-105` gets a pointer to the automated script rather than a full rewrite.

- **F19:** add a note to `android-safe-build-loop.md` §Guardrails that check1 tolerates untracked files.
- **F20:** soften Success Criteria #3 "byte-identical" → "behavior-preserving in vanilla mode (JNI calls still execute; no gameplay-affecting change)".
**QA:** `grep -rn 'android-game' README.md AGENTS.md android.md android/codemap.md docs/WORKDIR/android-safe-build-loop.md` → only intentional historical mentions.

---

### Task T4: Normalize backslash separators in parseMod on POSIX (F11)

**Files:** `GeneralsMD/Code/GameEngine/Source/Common/CommandLine.cpp:1091` + `Generals/Code/GameEngine/Source/Common/CommandLine.cpp:1091`

In `parseMod()`, before the existing `if (!endsWith...)` block, drop a trailing Windows-style backslash on POSIX so `opendir` does not see a literal `\` filename:
```cpp
#ifndef _WIN32
		// GeneralsX @bugfix Claude 10/07/2026 F11: a Windows-style trailing '\' must be
		// treated as a separator on POSIX, else opendir() sees a literal '\' filename.
		if (modPath.endsWith("\\"))
		{
			modPath = modPath.reverseSubstr(modPath.getLength() - 1); // drop trailing '\'
		}
#endif
		if (!modPath.endsWith("\\") && !modPath.endsWith("/"))
		{
#ifdef _WIN32
			modPath.concat('\\');
#else
			modPath.concat('/');
#endif
		}
```
**API note:** executor must verify `AsciiString` exposes `reverseSubstr`/`getLength` (grep `Include/Common/AsciiString.h`); if absent, use `str()` + manual truncate. No casts to silence types.
**QA:** `cmake --build build/android-vulkan --target z_generals` compiles both copies.

---

### Task T5: Guard module alias registration (F12, F13, F14)

**Files:** `GeneralsMD/Code/GameEngine/Source/Common/Thing/ModuleFactory.cpp:714` + `Generals/.../Thing/ModuleFactory.cpp:714`, and `ModuleFactory.h:74` (both copies)

In `addModuleAlias()`, after computing `existingKey`/`aliasKey`, reject empty and self-aliases:
```cpp
	// GeneralsX @bugfix Claude 10/07/2026 F13: reject empty alias name
	if (aliasName.isEmpty())
	{
		DEBUG_CRASH(("addModuleAlias: empty alias name"));
		return;
	}
	// GeneralsX @bugfix Claude 10/07/2026 F12: reject self-alias (wastes findModuleTemplate iterations)
	if (aliasKey == existingKey)
	{
		DEBUG_CRASH(("addModuleAlias: alias '%s' identical to existing name", aliasName.str()));
		return;
	}
```
F14 — document the intentional no-op in `ModuleFactory.h:74`:
```cpp
	virtual void reset() override { }					///< We don't reset during the lifetime of the app
	// GeneralsX @tweak Claude 10/07/2026 F14: m_aliasMap intentionally NOT cleared here (reset is a
	// no-op by design; alias lifetime = app lifetime). If reset semantics change, clear m_aliasMap too. Task 9 debt.
```
**API note:** verify `AsciiString::isEmpty()` exists; if absent, use `getLength()==0`.
**QA:** incremental build compiles; guards present in both copies.

---

### Task T6: Harden SDL3Main mod-path parsing (F5, F6, F7, F21)

**Files:** `GeneralsMD/Code/Main/SDL3Main.cpp` (mod-path block, lines 700-757)

Four sub-fixes in the existing injection block:

**F5 — strip UTF-8 BOM** (after `fgets` at ~740, before the trim loop):
```cpp
// GeneralsX @bugfix Claude 10/07/2026 F5: strip UTF-8 BOM (Windows Notepad default)
if ((unsigned char)modPathBuf[0]==0xEF && (unsigned char)modPathBuf[1]==0xBB && (unsigned char)modPathBuf[2]==0xBF) {
    memmove(modPathBuf, modPathBuf+3, strlen(modPathBuf)-2); // shift rest + NUL
}
```
**F6 — trim the Intent path** (after `snprintf` at ~709): apply the same trailing-whitespace trim already used for mod.txt (lines 743-748). Extract a small static `trimTrailingWs(char*)` helper to avoid duplication, or duplicate the loop.
**F7 — warn on truncation** (after both snprintf and fgets populate `modPathBuf`):
```cpp
// GeneralsX @bugfix Claude 10/07/2026 F7: warn on silent truncation
if (strlen(modPathBuf) >= sizeof(modPathBuf)-2) {
    __android_log_print(ANDROID_LOG_WARN, "GeneralsX", "Mod path truncated to %zu bytes", sizeof(modPathBuf));
}
```
**F21 — reject CESU-8 overlong NUL** (after obtaining `modPath` from JNI, before snprintf):
```cpp
// GeneralsX @bugfix Claude 10/07/2026 F21: reject CESU-8 overlong NUL (\xC0\x80) from Java
if (strstr(modPath, "\xC0\x80") != nullptr) {
    __android_log_print(ANDROID_LOG_WARN, "GeneralsX", "Mod path contains embedded NUL (CESU-8); ignoring");
    modPath = nullptr;
}
```
**QA:** incremental build compiles; `grep -n '0xEF.*0xBB.*0xBF\|CESU-8\|truncated to' SDL3Main.cpp` shows all guards.

---

### Task T7: Harden preflight.sh (F1, F3, F9, F10, F15, F16) — GREEN

**Files:** `scripts/build/android/preflight.sh` (depends: T8 red, T1 gradle value)

**F1 — expand check5 regex** (line 70) to cover all four forms:
```bash
git diff HEAD~1 HEAD | grep -E '^\+[[:space:]]*#if(def|ndef)?[[:space:]]+(![[:space:]]*)?(defined[[:space:]]*\([[:space:]]*)?RTS_GENERALS([^[:alnum:]_]|$)'
```
**F3 — check6** (line 85): require an annotation among the diff's *added* lines, not the whole file:
```bash
added="$(git diff HEAD~1 HEAD -- "${f}" | grep -E '^\+[^+]')"
if [[ -n "${added}" ]] && ! echo "${added}" | grep -q "GeneralsX @"; then
    echo "  missing annotation in new lines: ${f}" >&2; MISSING=1
fi
```
**F9 — dxvk reset** (line 27): warn loudly before discarding:
```bash
if [[ -n "$(git -C references/fbraz3-dxvk status --porcelain)" ]]; then
    echo "  NOTE: discarding DXVK submodule local changes (cmake re-applies Patches/dxvk-android.patch on next configure)." >&2
fi
git -C references/fbraz3-dxvk checkout -- . 2>/dev/null || true
```
**F10 — add check7b** after check7 (validates the value T1 set):
```bash
grep -E 'build/android-vulkan/[^"]*sdl3-src' "${GRADLE}" >/dev/null \
    || fail "build.gradle SDL3 java srcDir must point at build/android-vulkan (F2)."
```
**F15/F16:** add comments on check5/check6 acknowledging the merge-commit first-parent limitation and strengthening the initial-commit skip echo to `WARNING`.
**QA:** `bash scripts/build/android/preflight.sh` → PASS; `bash scripts/build/android/test-preflight-adversarial.sh` → `ALL GUARDRAILS PROVEN ADVERSARIALLY` (0 WEAK).

---

### Task T8: Extend adversarial preflight test (F1, F3, F17) — RED

**Files:** `scripts/build/android/test-preflight-adversarial.sh`

**F1** — add three probes after the existing check5 block (each a separate `commit_violation` / `run_pf` / `expect_fail`): `#ifdef RTS_GENERALS`, `#ifndef RTS_GENERALS`, `#if !RTS_GENERALS`.
**F3** — add a check6 probe: append an unannotated line to an *existing annotated* file (e.g. `ArchiveFileSystem.cpp`), `commit_violation`, `expect_fail "check6" "unannotated change in existing file" "annotation"`.
**F17** — fix the stale header comment (lines 1-12): check1 now *tolerates* untracked files, *rejects* tracked changes.
**QA:** `bash -n` syntax OK; running the extended test against the **current** (pre-T7) preflight reports the new probes as WEAK — this is the intended red state.

---

### Task T9: Final verification gate (no commit)

Run by the orchestrator (not delegated). Prove the whole remediation is coherent end-to-end.

1. `bash scripts/build/android/preflight.sh` → PASS.
2. `bash scripts/build/android/test-preflight-adversarial.sh` → exit 0, 0 WEAK.
3. `cmake --build build/android-vulkan --target z_generals` → compiles (validates T4/T5/T6 C++).
4. Residue greps (all must return nothing actionable):
   - `grep -rn 'darwin-x86_64' scripts/build/android/package-android-zh.sh` → only the `Darwin)` arm.
   - `grep -rn 'build/android-game' README.md AGENTS.md android.md android/codemap.md` → only historical notes.
   - `grep -n 'android-game' android/app/build.gradle` → empty.

---

### Commit strategy (Phase 1.5)

One Conventional Commit per task, on `main`. TDD order: **T8 before T7.** Source changes carry `// GeneralsX @bugfix Claude 10/07/2026 F<n>` inline annotations.

| # | Task | Commit message |
|---|------|----------------|
| 1 | T1 | `fix(android): point gradle SDL3 java srcDir at android-vulkan build dir` |
| 2 | T2 | `fix(android): detect NDK host tag so packager runs on WSL/Linux` |
| 3 | T3 | `docs(android): reconcile build dir refs to android-vulkan` |
| 4 | T4 | `fix(input): normalize backslash separators in mod path on POSIX` |
| 5 | T5 | `fix(game-logic): guard module alias registration against self/empty names` |
| 6 | T6 | `fix(android): harden mod path parsing (BOM strip, trim, truncation, NUL)` |
| 7 | T8 | `test(build): extend adversarial preflight probes (ifdef/ifndef/modified-file)` |
| 8 | T7 | `fix(build): harden preflight regex, annotation diff, dxvk-reset, gradle check` |

Each commit is independently revertible. The only cross-commit dependency is T7 ← (T8 logic, T1 value).

### Success Criteria (Phase 1.5)

1. All 8 commits land on `main`, each passing its per-task QA gate.
2. T9 final gate green: preflight PASS + adversarial ALL-PROVEN (0 WEAK) + incremental build compiles + zero actionable residue greps.
3. F1–F17 resolved; F14 and F18 documented as known debt; the 5 withdrawn findings untouched.
4. The committed baseline (Tasks 0.3/0.4, 1, 2, 6) is safe to build Task 3 on top of.

---

## Phase 2 — Architecture Deepening (feasibility-verified, 2026-07-09)

Feasibility study completed against actual codebase (6 parallel explore agents). All 8 deferred items reassessed; feasible items pulled into active tasks below. Items that remain deferred are only D8b (proper layered resolution — gated on D8a validation) and any UX polish beyond the baseline ModPicker.

**User decisions applied:**
- D1: use **doctest** (header-only, lighter than gtest for a repo with no test culture)
- D7: **dynamic main-menu button** (proven pattern via `updateNotifyButton` in `MainMenu.cpp:864-908`)
- D8: **cheap route first** (D8a — extend `setAssetRootPath` to vector of fallback roots); D8b deferred pending D8a validation

**New dependencies discovered during feasibility study:**
- D4 needs **zero new JNI** — `SDL_GetAndroidInternalStoragePath()` already wraps `getFilesDir()` (called at `SDL3Main.cpp:312`)
- D5 rated **Trivial** (15-35 LOC, 2 files) — factory is a single `std::map::find`, aliases are copy-one-entry
- D6 downgraded from Hard to **Practical** — only 30-50 open fds (18 BIGs + transient), LRU is defensive, not mandatory
- D3 has an **orphan `closeArchiveFile` seam** (declared + implemented, never called) — the exact hook a ModManager needs

### File Structure (Phase 2 additions)

| File | Change | Responsibility |
|------|--------|----------------|
| `GeneralsMD/Code/GameEngine/Include/Common/ModuleFactory.h` | Modify | D5: `addModuleAlias()` API + `m_aliasMap` member |
| `GeneralsMD/Code/GameEngine/Source/Common/Thing/ModuleFactory.cpp` | Modify | D5: alias implementation + `findModuleTemplate` alias consult |
| `Core/GameEngine/Source/Common/System/GameMemory.cpp` | Modify | D2: always-on telemetry counters (`MEMORY_TELEMETRY_ENABLED` when `__ANDROID__`) |
| `GeneralsMD/Code/Main/SDL3Main.cpp` | Modify | D2: telemetry sampler hook; D4: hybrid storage path split |
| `tests/archive/CMakeLists.txt` | Create | D1: doctest test target |
| `tests/archive/archive_override_test.cpp` | Create | D1: multimap override regression test |
| `tests/fixtures/generate_test_big.py` | Create | D1: synthetic .big file generator for test corpus |
| `GeneralsMD/Code/GameEngine/Source/Common/System/ArchiveFileSystem.cpp` | Modify | D3: close path (clean `m_rootDirectory` on `closeArchiveFile`); D8a: mod-dir push to asset fallback vector |
| `GeneralsMD/Code/GameEngine/Include/Common/ArchiveFileSystem.h` | Modify | D3: `unloadMod()` / `getOpenArchiveCount()` API |
| `GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/MainMenu.cpp` | Modify | D7: dynamic Mods button (mirrors `updateNotifyButton` at :864-908) |
| `GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/ModPickerMenu.cpp` | Create | D7: mod picker screen (ListBox + activate/cancel) |
| `GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp` | Modify | D4: sandbox `m_userDataDir` override for `__ANDROID__` |
| `GeneralsMD/Code/GameEngine/Source/Common/System/SaveGame/GameState.cpp` | Modify | D4: route save writes to sandbox path |
| `GeneralsMD/Code/GameEngine/Source/Common/Recorder.cpp` | Modify | D4: route replay writes to sandbox path |
| `Core/GameEngine/Source/Common/UserPreferences.cpp` | Modify | D4: route prefs writes to sandbox path |
| `Core/GameEngine/Source/Common/System/LocalFileSystem.cpp` | Modify | D8a: `setAssetFallbackPaths` vector extension |
| `Core/GameEngine/Include/Common/LocalFileSystem.h` | Modify | D8a: `setAssetFallbackPaths` API |
| `android.md` | Modify | Phase 2 findings |
| `docs/DEV_BLOG/2026-07-DIARY.md` | Modify | Diary entry |

No files are deleted. No engine determinism paths are touched except D8a (which adds a fallback path that is only consulted after the existing cwd check fails).

---

### Task 6: ModuleFactory alias seam (D5 — 15-35 LOC, no dependencies)

The factory is `std::map<NameKeyType, ModuleTemplate>` with a single `find()` lookup. An alias is copying one entry to a new key. Two files, zero migration of existing modules.

**Files:**
- Modify: `GeneralsMD/Code/GameEngine/Include/Common/ModuleFactory.h`
- Modify: `GeneralsMD/Code/GameEngine/Source/Common/Thing/ModuleFactory.cpp`

- [ ] **Step 1: Add `addModuleAlias()` to the public API**

In `ModuleFactory.h`, after the `addModule` macro block (~line 107), add:
```cpp
void addModuleAlias(const AsciiString& existingName,
                    const AsciiString& aliasName,
                    ModuleType type);
```
plus `std::map<NameKeyType, NameKeyType> m_aliasMap;` as a private member.

- [ ] **Step 2: Implement `addModuleAlias()`**

In `ModuleFactory.cpp`, add the function (~10 lines):
- Call `findModuleTemplate(existingName, type)` to get the source entry
- Compute the decorated name keys for both names
- Insert `m_aliasMap[aliasKey] = existingKey`

- [ ] **Step 3: Modify `findModuleTemplate()` to consult aliases**

Before the existing `m_moduleTemplateMap.find(namekey)`, check `m_aliasMap`:
```cpp
auto aliasIt = m_aliasMap.find(namekey);
if (aliasIt != m_aliasMap.end())
    namekey = aliasIt->second;  // resolve alias to canonical name
```
**Guard:** iterate (or limit depth) to prevent circular aliases.

- [ ] **Step 4: Commit**

```bash
git add GeneralsMD/Code/GameEngine/Include/Common/ModuleFactory.h \
        GeneralsMD/Code/GameEngine/Source/Common/Thing/ModuleFactory.cpp
git commit -m "feat(mod): add ModuleFactory alias registration seam

Adds addModuleAlias(existingName, aliasName, type) enabling mods
to register alternative names for existing module types without
recompilation. Alias resolution is a single map lookup in the
existing findModuleTemplate hot path — O(1) overhead.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 7: RSS/fd telemetry (D2 — ~100 LOC, prerequisite for D6)

Add always-on (release-safe) memory and fd counters via Android bionic APIs. Uses the DMA magic cookie (`0x47454d53`) already proven in `GameMemory.cpp:906`.

**Files:**
- Modify: `Core/GameEngine/Source/Common/System/GameMemory.cpp`
- Modify: `GeneralsMD/Code/Main/SDL3Main.cpp`

- [ ] **Step 1: Add `MEMORY_TELEMETRY_ENABLED` gate**

In `GameMemory.cpp`, add a new gate (default ON when `__ANDROID__`):
```cpp
#if defined(__ANDROID__) && !defined(MEMORY_TELEMETRY_ENABLED)
#define MEMORY_TELEMETRY_ENABLED 1
#endif
```
Add lightweight atomics: `theCurrentBudgetBytes`, `thePeakBudgetBytes`, updated in `allocateBytesDoNotZeroImplementation` and `freeBytes`.

- [ ] **Step 2: Add process telemetry sampler**

New function `sampleProcessTelemetry()` in `GameMemory.cpp`:
- `getrusage(RUSAGE_SELF, &ru)` → `ru_maxrss` (**bionic: bytes, NOT KB — footgun**)
- `opendir("/proc/self/fd")` → count entries → fd count
- `getrlimit(RLIMIT_NOFILE, &rlim)` → fd ceiling
- Return a `ProcessTelemetry` struct

- [ ] **Step 3: Wire periodic sampling in SDL3Main.cpp**

In the main loop or as a once-per-N-frames hook, call `sampleProcessTelemetry()` and log via `__android_log_print(ANDROID_LOG_INFO, "GeneralsX", ...)`.

- [ ] **Step 4: Commit**

```bash
git add Core/GameEngine/Source/Common/System/GameMemory.cpp \
        GeneralsMD/Code/Main/SDL3Main.cpp
git commit -m "feat(android): add RSS/fd process telemetry for mod memory budgeting

Adds MEMORY_TELEMETRY_ENABLED gate (always-on on Android) with
lightweight budget counters. Samples RSS via getrusage (bionic:
ru_maxrss is in bytes) and fd count via /proc/self/fd.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 8: Adversarial test harness (D1 — ~500-1000 LOC, greenfield)

Build a doctest-based host-side (Linux) test harness for `ArchiveFileSystem`. Targets the multimap override logic at `ArchiveFileSystem.cpp:158-183` — the non-negotiable invariant.

**Files:**
- Create: `tests/archive/CMakeLists.txt`
- Create: `tests/archive/archive_override_test.cpp`
- Create: `tests/fixtures/generate_test_big.py`
- Modify: root `CMakeLists.txt` — `enable_testing()` + `include(CTest)`
- Modify: `vcpkg.json` — add doctest

- [ ] **Step 1: Add doctest to vcpkg.json and CMake**

```json
{ "name": "doctest" }
```
In root `CMakeLists.txt`: `enable_testing()` + `include(CTest)` + `add_subdirectory(tests)`.

- [ ] **Step 2: Create synthetic .big file generator**

`tests/fixtures/generate_test_big.py`: generates minimal BIG-format archives with controlled overlap. Two archives: `test_base.big` (contains `Art/foo.tga`) and `test_override.big` (contains `Art/foo.tga` with different content). Test verifies the override wins.

- [ ] **Step 3: Write `archive_override_test.cpp`**

Link against `corei_gameengine_private` + `corei_gameengine_public`. Tests:
1. `loadIntoDirectoryTree` overwrite=TRUE → last-loaded archive wins
2. `loadIntoDirectoryTree` overwrite=FALSE → first-loaded archive wins
3. Multimap erase-and-reinsert preserves insertion order (the Android fix)
4. `getArchiveFile` returns correct archive for instance lookups
5. Adversarial: corrupted BIG header → graceful failure (no crash)
6. Adversarial: 1000-archive stress → no fd exhaustion
7. Adversarial: order-sensitivity across 3+ overlapping archives

MemoryPool must be initialized once via a `main()` wrapper or doctest fixture.

- [ ] **Step 4: Configure and run on Linux host**

```bash
cmake --preset linux64-deploy -DBUILD_TESTING=ON
cmake --build build/linux64-deploy --target z_test_archive
cd build/linux64-deploy && ctest --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add tests/ vcpkg.json CMakeLists.txt
git commit -m "test(archive): add adversarial doctest harness for ArchiveFileSystem

Golden regression: locks the multimap erase-and-reinsert dance
(ArchiveFileSystem.cpp:158-183) with 7 adversarial tests.
Uses doctest (header-only) and synthetic .big file generator.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 9: ModManager subsystem (D3 — 200-400 LOC, central dependency)

Wire the orphan `closeArchiveFile()` seam into a minimal ModRegistry. The engine already has `FileInstance` machinery for per-instance lookup; the gap is a teardown path and a registry above `loadMods()`.

**Files:**
- Modify: `GeneralsMD/Code/GameEngine/Include/Common/ArchiveFileSystem.h`
- Modify: `GeneralsMD/Code/GameEngine/Source/Common/System/ArchiveFileSystem.cpp`

- [ ] **Step 1: Implement close path**

In `ArchiveFileSystem::closeArchiveFile()`, after closing the archive fd:
- Remove entries from `m_rootDirectory` that belong to the closed archive
- Clear the archive from `m_archiveFileMap`
- Invalidate `m_fileExist` cache entries for the closed archive's files

- [ ] **Step 2: Add `unloadMod()` public API**

```cpp
bool unloadMod(const AsciiString& modPath);
Int  getOpenArchiveCount() const;
```
`unloadMod` calls `closeArchiveFile` on the mod's entry in `m_archiveFileMap`.

- [ ] **Step 3: Add simple ModRegistry class** (can be a small struct in ArchiveFileSystem.h)

```cpp
struct ModRegistry {
    std::vector<AsciiString> m_activeMods;  // in load order
    bool loadMod(const AsciiString& path);
    bool unloadMod(const AsciiString& path);
    bool unloadAllMods();
};
```
Hooks into the existing `loadMods()` — after loading, registers the mod path in `m_activeMods`.

**Alias cleanup (review finding 2026-07-09):** `ModuleFactory::m_aliasMap` (added in Task 6) is never cleared by `reset()`/`init()` (both are no-ops by design). When `unloadMod`/`unloadAllMods` runs (mod switching, D7 picker), it must also clear any aliases the unloaded mod registered via `addModuleAlias`. Track per-mod alias registrations (e.g. a `std::vector<NameKeyType>` of alias keys per `ModRegistry` entry) and call a new `ModuleFactory::removeModuleAlias(aliasName, type)` on unload. Without this, switching from Mod A (which aliased Foo→Bar) to Mod B leaves the stale Foo→Bar alias active, silently corrupting Mod B's module resolution.

- [ ] **Step 4: Commit**

```bash
git add GeneralsMD/Code/GameEngine/Include/Common/ArchiveFileSystem.h \
        GeneralsMD/Code/GameEngine/Source/Common/System/ArchiveFileSystem.cpp
git commit -m "feat(mod): add ModManager subsystem with unload and registry

Wires the orphan closeArchiveFile() seam (declared but never called)
into a full close path that cleans m_rootDirectory and the
m_fileExist cache. Adds ModRegistry for tracking active mods.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 10: Sandbox-safe config (D4 — 200-400 LOC, depends on D3)

Route user-writable data (Save/, Replays/, prefs, screenshots) to Android sandboxed internal storage while keeping .big archives on external storage. **Zero new JNI** — SDL3's `SDL_GetAndroidInternalStoragePath()` already wraps `getFilesDir()`.

**Files:**
- Modify: `GeneralsMD/Code/Main/SDL3Main.cpp`
- Modify: `GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp`
- Modify: `GeneralsMD/Code/GameEngine/Source/Common/System/SaveGame/GameState.cpp`
- Modify: `GeneralsMD/Code/GameEngine/Source/Common/Recorder.cpp`
- Modify: `Core/GameEngine/Source/Common/UserPreferences.cpp`

- [ ] **Step 1: Expose sandbox path via env var**

In `SDL3Main.cpp`, after the existing chdir block, add:
```cpp
#if defined(__ANDROID__)
const char *intFiles = SDL_GetAndroidInternalStoragePath();
if (intFiles) {
    char configPath[512];
    snprintf(configPath, sizeof(configPath), "%s/config", intFiles);
    mkdir(configPath, 0755);  // idempotent
    setenv("GENERALSX_USER_CONFIG_PATH", configPath, 1);
    SDL_free((void*)intFiles);
}
#endif
```

- [ ] **Step 2: Override `m_userDataDir` for Android in GlobalData.cpp**

In `BuildUserDataPathFromRegistry()`'s `#elif defined(__ANDROID__)` branch (line 1453):
```cpp
const char *configPath = getenv("GENERALSX_USER_CONFIG_PATH");
if (configPath && configPath[0])
    userDataDir = configPath;
else
    userDataDir = "./";
```

- [ ] **Step 3: Route write sites to use the new path**

In `GameState.cpp`, `Recorder.cpp`, `UserPreferences.cpp`: swap `getPath_UserData()` for `getConfigPath_UserData()` inside `#if defined(__ANDROID__)` guards. The read-only .big archive reads stay on CWD (external GameData).

- [ ] **Step 4: Idempotent migration on first launch**

If `GENERALSX_USER_CONFIG_PATH` exists but is empty, copy existing Save/Replays/prefs from CWD to sandbox (one-time upgrade path for existing users).

- [ ] **Step 5: Commit**

```bash
git add GeneralsMD/Code/Main/SDL3Main.cpp \
        GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp \
        GeneralsMD/Code/GameEngine/Source/Common/System/SaveGame/GameState.cpp \
        GeneralsMD/Code/GameEngine/Source/Common/Recorder.cpp \
        Core/GameEngine/Source/Common/UserPreferences.cpp
git commit -m "feat(android): route user config to sandboxed internal storage

Uses SDL_GetAndroidInternalStoragePath() (getFilesDir() equivalent)
— zero new JNI. Keep .big archives on external for adb push compat.
One-time migration copies existing config on first launch.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 11: Memory budget + LRU (D6 — ~150 LOC, depends on D2 telemetry)

Add a configurable memory budget with graceful eviction. Only 30-50 fds in steady state (18 BIGs + transient) — LRU is defensive, not mandatory. Budget check is one if-statement behind the existing `allocateBytesDoNotZeroImplementation` hook.

**Files:**
- Modify: `Core/GameEngine/Source/Common/System/GameMemory.cpp`

- [ ] **Step 1: Add budget gate to allocation path**

In `allocateBytesDoNotZeroImplementation`, after the existing MSGNEW:
```cpp
#if MEMORY_TELEMETRY_ENABLED
if (theCurrentBudgetBytes > CNC_MEMORY_BUDGET_MB * 1024 * 1024) {
    TheArchiveFileSystem->evictColdestArchive();
}
#endif
```

- [ ] **Step 2: Add `evictColdestArchive()` to ArchiveFileSystem**

Iterate `m_archiveFileMap` in insertion order (oldest = coldest), call `closeArchiveFile` on the first non-system archive. System archives (base game, ZH) are never evicted.

- [ ] **Step 3: Add fd cap to `openArchiveFile`**

Reject new archive opens when `getOpenArchiveCount() >= CNC_MAX_OPEN_ARCHIVES` (default 8, env-var overridable). Log a clear warning to logcat.

- [ ] **Step 4: Verify no determinism impact**

Budget checks are off the simulation path — they only fire on allocation, which is already non-deterministic (heap fragmentation). Archive eviction is transparent to game state because `openFile` re-extracts from the parent BIG on demand.

- [ ] **Step 5: Commit**

```bash
git add Core/GameEngine/Source/Common/System/GameMemory.cpp \
        GeneralsMD/Code/GameEngine/Source/Common/System/ArchiveFileSystem.cpp
git commit -m "feat(mod): add memory budget and LRU archive eviction

Budget gated by CNC_MEMORY_BUDGET_MB env var. Evicts coldest
archive (by insertion order) when budget exceeded. System archives
(base/ZH) are never evicted. fd cap at CNC_MAX_OPEN_ARCHIVES.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 12: Mod picker GUI (D7 — 700-1500 LOC, depends on D3)

Add a "Mods" button to the main menu (dynamic C++ creation — proven pattern via `updateNotifyButton` at `MainMenu.cpp:864-908`) and a ModPicker screen with ListBox + activate/cancel.

**Files:**
- Modify: `GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/MainMenu.cpp`
- Create: `GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/ModPickerMenu.cpp`

- [ ] **Step 1: Add dynamic "Mods" button to main menu**

In `MainMenuInit()`, after the existing button setup (~line 540), use the proven `gogoGadgetPushButton` pattern:
```cpp
// GeneralsX @feature Claude 09/07/2026 dynamic mods button (pattern from updateNotifyButton)
modsID = TheNameKeyGenerator->nameToKey("MainMenu.wnd:ButtonMods");
// Position below the existing buttons, use TheDisplay->getWidth()/Height() for scaling
```
Wire click → `TheShell->push("Menus/ModPickerMenu.wnd")` in `MainMenuSystem()`.

- [ ] **Step 2: Write ModPickerMenu.cpp**

Pattern: copy `ExtrasMenu.cpp` (241 lines) as skeleton.
- Init: scan `GameData/Mods/` for subdirs, populate ListBox
- System: GBM_SELECTED on activate → write mod path to `mod.txt` + call `TheShell->pop()`
- Cancel → `TheShell->pop()` without changes
- Optional: mark currently-active mod with a checkmark via `GadgetListBoxAddEntry` flags

- [ ] **Step 3: Profile persistence via OptionPreferences**

Store active mod path in `OptionPreferences` (already supports arbitrary key/value pairs):
```cpp
(*TheOptionPreferences)["ActiveMod"] = modPath;
```
Restore on next launch — ModPicker preselects the saved mod.

- [ ] **Step 4: Crash rollback**

On successful launch, write a `mod-loaded-ok` sentinel file. On crash, clear the active mod on next launch (prevent crash loops). Hook into `TheGameEngine->setQuitting` for the "success" marker.

- [ ] **Step 5: Commit**

```bash
git add GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/MainMenu.cpp \
        GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/ModPickerMenu.cpp
git commit -m "feat(android): add mod picker GUI with main-menu button

Dynamic button creation via gogoGadgetPushButton (proven pattern).
ModPicker screen with ListBox, profile persistence, and crash
rollback sentinel. Touch input works via existing synthetic mouse.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 13: Engine loose-file resolution (D8a — ~20 LOC, depends on D3 working)

Cheap path: extend the existing `setAssetRootPath` seam to a `std::vector<AsciiString>` of fallback roots. Push the active mod dir as a fallback when a mod is loaded. D8b (proper layered resolution with per-lookup disk I/O) is deferred pending D8a validation in the wild.

**Files:**
- Modify: `Core/GameEngine/Include/Common/LocalFileSystem.h`
- Modify: `Core/GameEngine/Source/Common/System/LocalFileSystem.cpp`

- [ ] **Step 1: Extend `setAssetRootPath` to vector**

In `LocalFileSystem.h`:
```cpp
static void setAssetFallbackPaths(const std::vector<AsciiString>& paths);
```
Replace the single `s_assetFallbackPath` with `std::vector<AsciiString> s_assetFallbackPaths`.

- [ ] **Step 2: Modify the fallback lookup in `StdLocalFileSystem::openFile`**

Iterate `s_assetFallbackPaths` when the cwd check fails. First match wins.

- [ ] **Step 3: Push mod dir to fallback paths on mod load**

In `ModRegistry::loadMod()`, after successful archive loading:
```cpp
std::vector<AsciiString> paths;
paths.push_back(modPath + "/Art");
paths.push_back(modPath + "/Data");
TheLocalFileSystem->setAssetFallbackPaths(paths);
```
On unload, clear the fallback paths.

- [ ] **Step 4: Verify no regression**

Loose files in GameData must still beat archives. The mod-dir fallback is consulted only after the cwd check fails. Existing resolution order: cwd → fallback paths (new) → archives. Mod-dir loose files are priority 2 (behind cwd, ahead of archives).

- [ ] **Step 5: Commit**

```bash
git add Core/GameEngine/Include/Common/LocalFileSystem.h \
        Core/GameEngine/Source/Common/System/LocalFileSystem.cpp
git commit -m "feat(mod): extend loose-file resolution to mod directory (cheap path)

Extends setAssetRootPath to vector of fallback paths. Mod dir
loose files resolve after GameData cwd but before archives.
D8b (per-lookup layered resolution) deferred pending validation.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Success Criteria (Phase 2)

1. All Phase 1 criteria still pass (mod loading via `mod.txt` and Intent extra works on device)
2. D5: `addModuleAlias` resolves correctly in `findModuleTemplate`; circular alias guard prevents infinite loops
3. D2: RSS and fd count log to logcat once per ~60 frames; `ru_maxrss` reported in bytes (bionic-correct)
4. D1: 7 adversarial archive tests pass on `linux64-deploy` host; `ctest` exits 0
5. D3: mod can be loaded, unloaded, and reloaded without restarting the engine
6. D4: Save/Replays/prefs written to `/data/data/me.generalsx.zh/files/config/`; existing external config migrated on first launch
7. D6: `CNC_MEMORY_BUDGET_MB=512` env var triggers archive eviction when crossed; fd cap at `CNC_MAX_OPEN_ARCHIVES=8` rejects excess opens
8. D7: Mods button appears on main menu; ModPicker lists installed mods; activating a mod writes `mod.txt` and works on next launch; crash rollback clears active mod
9. D8a: loose `Art/foo.tga` in mod dir overrides the same file in archives; removal returns archive version; GameData cwd loose files still win over mod dir

---

## Deferred Backlog (from plan v1 — ITEMS MARKED ACTIVE ARE NOW IN PHASE 2)

Items below are either fully deferred or gated on validation of their Phase 2 counterpart. Items pulled into Phase 2 are marked ✅ with the task that covers them.

| ID | Status | Item (v1 ref) | Resolved decisions to honor |
|----|--------|---------------|-----------------------------|
| D1 | ✅ Task 8 | Host unit-test harness, adversarial (P0.1) | doctest; synthetic .big generator; golden regression on multimap dance |
| D2 | ✅ Task 7 | RSS/fd telemetry (P0.2) | Always-on on Android via `MEMORY_TELEMETRY_ENABLED`; bionic `ru_maxrss` is bytes |
| D3 | ✅ Task 9 | ModManager subsystem + manifest + mod chains (P3.1-P3.3) | BIG stays 32-bit on-disk, 64-bit in-memory only; orphan `closeArchiveFile` seam wired |
| D4 | ✅ Task 10 | Sandbox-safe config via SDL3 `SDL_GetAndroidInternalStoragePath()` (P5.2) | Zero new JNI; hybrid split (external .big, internal config); strictly after D3 |
| D5 | ✅ Task 6 | ModuleFactory alias seam (P3.4) | `std::map` alias lookup in `findModuleTemplate`; circular guard; no dynamic plugin types |
| D6 | ✅ Task 11 | Memory budget + LRU eviction + fd cap (P2.x) | Graceful degradation; env-var budget; system archives never evicted |
| D7 | ✅ Task 12 | Mod-picker GUI, profiles, crash rollback, load-status (P4.x) | Dynamic main-menu button; OptionPreferences for profiles; crash sentinel |
| D8a | ✅ Task 13 | Engine loose-file resolution (cheap path) | Vector extension of `setAssetRootPath`; cwd still wins; D8b deferred |
| D8b | Deferred | Proper per-lookup layered resolution | Gated on D8a validation; needs determinism review of resolution order; see Future Enhancements in spec v2 |

These were v1's Phases 0-5. They are real ideas, but they implement the spec's declared Non-Goals or exceed "minimal code changes"; none blocks baseline mod support. Recorded here so the resolved design decisions aren't lost. Dependency ordering fixed from v1 (D4 strictly after D3; v1's P5.1 dropped entirely — `file_compat.h` + the `_S_IFDIR` define at `CommandLine.cpp:44` already solve it).

| ID | Item (v1 ref) | Resolved decisions to honor if/when picked up |
|----|---------------|-----------------------------------------------|
| D1 | Host unit-test harness, adversarial (P0.1) | Tests must try to break: corrupted BIG headers, order-sensitivity, stress reload; golden regression locking the multimap dance (`ArchiveFileSystem.cpp:158-183`) |
| D2 | RSS/fd telemetry (P0.2) | Prereq for any memory work; measure before changing |
| D3 | ModManager subsystem + manifest + mod chains (P3.1-P3.3) | BIG stays 32-bit on-disk, 64-bit in-memory only; >4GB detection with clear error |
| D4 | Sandbox-safe config via JNI `getFilesDir()` (P5.2) | Strictly after D3 (v1 wave bug: was dispatched concurrently with its dependency) |
| D5 | ModuleFactory alias seam (P3.4) | Minimal alias registration only — new names mapping to existing types; no dynamic plugin types |
| D6 | Memory budget + LRU eviction + fd cap (P2.x) | Graceful degradation (evict/warn/continue); hard ceiling only as last-resort abort |
| D7 | Mod-picker GUI, profiles, crash rollback, load-status (P4.x) | Spec Non-Goals; require D3 first |
| D8 | Engine loose-file resolution from mod dir | New in v2 (from spec Future Enhancements); needs determinism review of resolution order |
