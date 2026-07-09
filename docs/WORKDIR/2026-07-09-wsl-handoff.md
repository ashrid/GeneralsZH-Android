# WSL Handoff — 2026-07-09

**For the Claude Code session working in this tree.** The session runs on **Windows** Claude Code with its working directory set to `\\wsl.localhost\Ubuntu\home\rashid\.projects\GeneralsZH-Android` — Claude is NOT installed inside WSL. This is now the **authoritative repo** (decision record: `docs/WORKDIR/android-safe-build-loop.md`). The Windows checkout at `C:\Users\force\.projects\GeneralsZH-Android` is a read-only git-pull mirror, synced only when the user asks.

## Session ground rules (UNC-path specifics)

- **Git from Windows works here** — the `safe.directory` exception for this UNC path was added to the user's global git config on 2026-07-09 (with permission). If `dubious ownership` ever reappears, that exception was lost.
- **All builds, packaging, and preflight run inside WSL**, never on the Windows side: `wsl.exe -d Ubuntu -e bash -lc '<command>'` (or rely on the shell mapping the UNC cwd into WSL). Compiling over the UNC/9p boundary would be slow and wrong — the whole point of this tree is native ext4 builds.
- adb: call `adb.exe` (Windows platform-tools) — works from both PowerShell and WSL bash.
- File I/O over `\\wsl.localhost` is slower than local NTFS; prefer targeted reads/greps over tree-wide scans.
- Windows-side Claude auto-memory is keyed to the old project path and won't load here — the in-repo docs (this file, the workflow spec, `android.md`, `CLAUDE.md`) are the context carriers.

## Where things stand

- **Confirmed and pending implementation:** the Android mod support spec v2 (`docs/superpowers/specs/2026-07-08-android-mod-support-design.md`) and plan v2 (`docs/superpowers/plans/2026-07-08-android-mod-architecture.md`). No code from them is written yet.
- **Confirmed workflow:** `docs/WORKDIR/android-safe-build-loop.md` — the build loop every change runs through (preflight → commit-gated build → triage ladder → package → logcat matrix → one human brief → docs-close). Grilled and user-confirmed 2026-07-09.
- **Migration done today:** cloned from the Windows checkout at `f5d0b8c6b` (main), origin set to `https://github.com/tarek369/GeneralsZH-Android.git`, uncommitted state carried over (`M AGENTS.md` + 382 untracked codemap.md files), submodules NOT yet initialized.

## Repo-critical gotcha discovered during migration

The object store contains CRLF-committed files (legacy Windows codebase, no `.gitattributes`). With `core.autocrlf=true` on Linux, **4796 files appear phantom-modified**. This clone is repaired: local config has `core.autocrlf=false` and `core.filemode=false` (matching the Windows repo). **Never enable autocrlf here**, and any future clone of this repo on Linux needs the same two settings before trusting `git status`.

## One-time setup remaining (workflow spec, steps 2–8)

1. Provision toolchain: Android NDK (`ANDROID_NDK_HOME`), CMake ≥3.25, Ninja, Meson, JDK 17, vcpkg. Gate: `cmake --preset android-vulkan` configures cleanly.
2. `git submodule update --init references/fbraz3-dxvk` (network).
3. Fix `scripts/build/android/package-android-zh.sh:34`: `BUILD_DIR` → `build/android-vulkan`; correct the false comment at :32–33 (preset binaryDir is `build/${presetName}`, `CMakePresets.json:287`). Confirmed decision — don't re-ask.
4. Create `scripts/build/android/preflight.sh` per the workflow spec's Guardrails section.
5. adb: use `adb.exe` (Windows platform-tools) from WSL; `adb.exe devices` then `adb.exe logcat -G 16M` once per device boot.

Then: implement the mod plan Tasks 1–5 through the build loop.

## Windows-side pieces (already in place)

- `sync-from-wsl.ps1` in the Windows checkout root — fetch + ff-only merge from the `wsl` remote. User invokes it by asking their Windows Claude session; never run automatically.
- Windows repo has remote `wsl` → this tree.
