# Workflow: Android Safe Build Loop (GeneralsZH-Android)

**Loop:** every code change to the Android port (e.g. each task of the mod-support plan) runs this cycle: preflight → build → package → deploy → logcat matrix → one human brief → docs-close.
**Trigger:** event — a committed change in the WSL tree that the user wants built. Never fires on an uncommitted tree.
**Checkpoint:** exactly one, at the end (push-right). Everything before it is autonomous.

Grounded in: `docs/superpowers/specs/2026-07-08-android-mod-support-design.md` (v2) and `docs/superpowers/plans/2026-07-08-android-mod-architecture.md` (v2). Decisions resolved by grilling 2026-07-09 (see NOTES.md).

---

## Topology (fixed decisions)

| Leg | Decision |
|-----|----------|
| Authoritative tree | WSL2 Ubuntu ext4: `/home/rashid/.projects/GeneralsZH-Android` |
| Editing | Windows Claude Code edits the WSL tree via `\\wsl.localhost\Ubuntu\home\rashid\.projects\GeneralsZH-Android` |
| Windows copy | Git-pull mirror at `C:\Users\force\.projects\GeneralsZH-Android`; synced **manually on request** via the sync script (below). Only committed state transfers. |
| Build | `cmake --preset android-vulkan` → `build/android-vulkan/` in WSL |
| adb | WSL invokes **`adb.exe`** (Windows platform-tools; drivers/server stay on Windows) |
| Build cadence | Commit-gated: every build corresponds to a git hash |
| Fix handling | Fix-forward: new commits, never amend |
| Commits | Via **WSL git** (`wsl bash -lc 'git …'`), never Windows git from PowerShell. Windows git lacks the repo identity (commits fail with "Author identity unknown"), and a failed commit leaves files staged — the next commit then silently mis-bundles them (this is how the AGENTS.md rewrite got swept into the handoff-docs commit on 2026-07-09). If you must commit from Windows, set `git config --global user.name`/`user.email` there first. |
| Docs | One docs commit per green run, closing the run |

## One-time setup (do once, in order)

1. **Migrate the repo into WSL.** In the current Windows checkout: commit or stash the pending state (modified `AGENTS.md`, untracked `codemap.md` files — user decides keep/drop at migration time). Then in WSL:
   `git clone /mnt/c/Users/force/.projects/GeneralsZH-Android ~/.projects/GeneralsZH-Android`
   `cd ~/.projects/GeneralsZH-Android && git submodule update --init references/fbraz3-dxvk`
2. **Provision the WSL toolchain:** Android NDK (export `ANDROID_NDK_HOME` in `~/.bashrc`), CMake ≥3.25, Ninja, Meson (DXVK-from-source), JDK 17 + Gradle wrapper deps, vcpkg per repo docs. Verify: `cmake --preset android-vulkan` configures cleanly.
3. **Fix the build-dir mismatch** (decision: preset wins): in `scripts/build/android/package-android-zh.sh:34` set `BUILD_DIR="${REPO_ROOT}/build/android-vulkan"` and correct the false comment at lines 32–33 (the preset builds into `build/${presetName}`, i.e. `android-vulkan`, per `CMakePresets.json:287`). Annotate with the repo's `// GeneralsX @build` convention (shell: `# GeneralsX @build ...`).
4. **Point the Windows mirror at WSL:** in the Windows checkout run
   `git remote add wsl \\wsl.localhost\Ubuntu\home\rashid\.projects\GeneralsZH-Android`
5. **Create the sync script** `sync-from-wsl.ps1` in the Windows checkout root (untracked or tracked — user's call at creation):
   ```powershell
   # Pulls committed state from the authoritative WSL repo. Run on user request only.
   git -C C:\Users\force\.projects\GeneralsZH-Android fetch wsl
   git -C C:\Users\force\.projects\GeneralsZH-Android merge --ff-only wsl/main
   ```
   `--ff-only` guarantees the mirror never diverges silently; if it refuses, the mirror was edited directly — stop and surface that.
6. **Create the preflight script** `scripts/build/android/preflight.sh` (see Guardrails).
7. **adb sanity:** from WSL, `adb.exe devices` shows the tablet; run `adb.exe logcat -G 16M` once per device boot.
8. Game data already pushed per `android.md`; mods per spec Option C.

## Guardrails — `preflight.sh` (runs first, every build)

Fails loud, cites the doc anchor, exits non-zero. **Scope:** preflight verifies source *invariants*, NOT toolchain presence — a green preflight does not mean the NDK, Meson, or DXVK submodule are installed (those are One-time setup, step 2). If preflight passes but `cmake --preset android-vulkan` fails at configure, suspect the toolchain, not the guardrails. Checks:

1. **Clean tree:** `git status --porcelain` empty (enforces commit-gating). Print the HEAD short-hash that this build will be named after. *Preflight first resets the DXVK submodule (`git -C references/fbraz3-dxvk checkout -- .`) because `cmake/dx8.cmake` applies `Patches/dxvk-android.patch` at configure time, dirtying the submodule after every build; the next configure re-applies the patch idempotently, so discarding the applied state here is safe. No-op if the submodule is uninitialized.*
2. **DXVK strip protection:** `app/build.gradle` still contains the `keepDebugSymbols` entries for `libdxvk_d3d8.so` / `libdxvk_d3d9.so` (stripping ⇒ SIGSEGV; android.md).
3. **Memory-pool cookie:** `0x47454d53` present in `Core/.../GameMemory.h/.cpp` (do-not-revert; CLAUDE.md).
4. **Multimap dance intact:** the erase-and-reinsert block and its marker comment present in `Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp` (~158–183) (android.md §4.2–4.3).
5. **No new base-INI gating:** diff of the commit being built contains no added `#if RTS_GENERALS` around enum/name-list definitions (android.md §4.1). Grep the diff, not the tree.
6. **Annotation convention:** every changed source file in the commit's diff contains a `GeneralsX @` annotation dated with the commit.
7. **Packager sanity:** `BUILD_DIR` in `package-android-zh.sh` ends in `android-vulkan` (protects the setup fix from regression).

## The loop (per run, autonomous until the brief)

1. **Preflight** — `bash scripts/build/android/preflight.sh` (WSL). Any failure stops the run before compiling.
2. **Build** — `cmake --build build/android-vulkan --target z_generals 2>&1 | tee build-$(git rev-parse --short HEAD).log`
3. **On build failure — triage ladder:**
   a. Read the **first** compiler error in the log, not the last.
   b. Fix as a **new commit** (annotated); re-enter the loop at step 1.
   c. Clean reconfigure (`rm -rf build/android-vulkan && cmake --preset android-vulkan`) **only** when `CMakeLists.txt`/preset/NDK/submodule changed since last green, or the error is provably cache-borne — never as a first resort (full rebuild ≈ an hour).
   d. Two failed fix attempts on the **same** error → stop, question the premise, switch to systematic debugging. Do not attempt a third variation of the same approach.
4. **Package + install** — `bash scripts/build/android/package-android-zh.sh --install` (verifies all 10 runtime `.so` libs staged; missing one ⇒ `dlopen` crash).
5. **Logcat matrix** — run the change's verification scenarios (for mod support: plan Task 4, scenarios 1–7) with:
   `adb.exe shell am force-stop me.generalsx.zh && adb.exe logcat -c` → launch per scenario → `adb.exe logcat -d -s GeneralsX:V`
   Capture actual lines per scenario. Any machine-checkable deviation from Expected → treat as build failure (triage ladder, step 3).
   *Note:* scenarios needing `DEBUG_LOG` output (e.g. mod-plan scenario 7's `Mod dir is '...'`) require a debug-configured build — flag in the brief if skipped on RelWithDebInfo.
6. **CHECKPOINT — the brief (the only human touch):** one message containing: commit hash built, build status + fix-chain summary (if any), staged-libs verification, per-scenario logcat verdict with actual lines, and the **eye-check list** — the on-screen confirmations only the user can give (menu reached, mod content visible, no visual corruption). User answers pass/fail.
   - **Fail** → the fix is a new commit; re-enter at step 1.
   - **Pass** → step 7.
7. **Docs-close (green runs only):** ONE commit updating `android.md` (measured logcat lines, not claims), `docs/DEV_BLOG/2026-07-DIARY.md` (whole run incl. what the fix chain fixed), `README.md` if user-facing. Convention amended: docs update before the **closing** commit of a run, not every commit.
8. **Sync (on user request only):** user asks → run `sync-from-wsl.ps1` on the Windows side.

## Failure contracts

- Preflight failure: report the violated invariant + doc anchor; no build happens.
- `--ff-only` sync refusal: the Windows mirror was edited directly — surface, never force.
- APK retention: keep the previous green APK as `app-release-<hash>.apk` before overwriting, so a bad run can be rolled back on-device without rebuilding.
- Logcat default buffer (256KB) hides crashes — `-G 16M` is part of setup; if diagnostics look truncated, re-check it before debugging ghosts.
