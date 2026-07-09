# Context — Continue Android Mod Support Work

**Last session:** 2026-07-09
**To continue:** Read this file, then execute plan v2 Task 1 (see "Execute Next")

---

## Where We Left Off

Spec and plan are at **v2** (rewritten in place 2026-07-08 after both v1 documents failed adversarial review). No code has been written yet. Ready to execute the v2 plan.

### Deliverables (current)

1. `docs/superpowers/specs/2026-07-08-android-mod-support-design.md` — **Spec v2**: all four v1 review defects fixed
2. `docs/superpowers/plans/2026-07-08-android-mod-architecture.md` — **Plan v2**: 5 baseline tasks + Deferred Backlog (D1–D8)
3. `docs/superpowers/README.md` — planning workspace overview (written pre-v2; may still describe the v1 wave structure — verify before trusting)

### v1 → v2: What the Review Found and How v2 Fixed It

| v1 defect | v2 fix |
|-----------|--------|
| `modPathBuf[512]` block-scoped but its pointer escapes into `__argv`, read later by `parseMod()` at `GameEngine::init` — dangling | Buffer (and everything escaping into `__argv`) is `static` |
| Text said "Intent overrides mod.txt", code did the opposite | Code reads Intent extra FIRST; `mod.txt` is fallback. Text/code/tests agree |
| Diagram claimed `-mod` parsed by `parseCommandLineForStartup` | Corrected: `-mod` is in `paramsForEngineInit` (`CommandLine.cpp:1179`), consumed at `GameEngine.cpp:540`, `loadMods()` at :543 |
| Spec claimed loose `Art/`/`Data/` under mod dir resolve — false on every platform | Goal 3 rescoped: `.big` mods fully supported; loose overrides = documented merge-into-GameData workflow; engine-level mod-dir loose resolution = explicit Non-Goal |
| Plan built 18 tasks on a baseline no task implemented; P5.1 already solved (`file_compat.h`); Wave 3 ran P5.2 concurrently with its dependency P3.1; "confirm resolved questions"; implemented spec Non-Goals (GUI, profiles, LRU) | Plan v2 = 5 tasks implementing only the spec baseline; v1 architecture preserved as Deferred Backlog D1–D8 with ordering fixed and P5.1 dropped |

### Key Verified Facts (re-verified against code 2026-07-08)

- `m_modDir` has exactly **three consumers**: `loadMods()` (`*.big` only, `ArchiveFileSystem.cpp:227-251`), video players (`FFmpegVideoPlayer.cpp:248`, stubbed on Android), `Win32Mouse.cpp:384` cursors (not compiled on Android). Loose mod-dir files never resolve — on any platform.
- GameData `chdir()` happens at `SDL3Main.cpp:321-335` — before both parse calls, so `fopen("mod.txt")` relative to CWD works.
- Existing static-buffer argv injection pattern (`-xres`/`-yres`): `SDL3Main.cpp:758-778`. It runs AFTER `parseCommandLineForStartup` (line 666) and works because those flags are engine-init-parsed. `-mod` injection goes BEFORE line 666 (also fine — startup parser skips unknown args).
- Backslash bug in `parseMod()`: `GeneralsMD .../CommandLine.cpp:1091-1092`, identical in `Generals .../CommandLine.cpp:1089-1090`.
- Multimap erase-and-reinsert override dance: `ArchiveFileSystem.cpp:158-183`.

### Design Decisions Locked in v2

| Decision | Choice |
|----------|--------|
| Precedence | Intent extra `"mod"` (per-launch, explicit) > `GameData/mod.txt` (persistent default) |
| argv modification | Static buffer pattern (NOT realloc — `__argv` is `main()`'s argv) |
| Injection point | Inside `main()`, after GameData chdir, before `parseCommandLineForStartup()` (line 666) |
| Path separator | `/` on non-Windows in `parseMod()` (`#ifdef _WIN32` split), both games |
| Loose files | Merge into GameData tree (loose beats all archives); NOT switchable per-mod; engine change deferred (D8) |
| TDD harness | Deferred (D1) — no test infra exists; verification = on-device logcat/behavior matrix |
| mod.txt robustness | Trim trailing `\r\n`/spaces/tabs (Windows-edited files); `access(path, R_OK)` before injection |

## Execute Next — Plan v2 Tasks (sequential)

1. **Task 1:** `parseMod()` POSIX separator fix (GeneralsMD + Generals CommandLine.cpp) → commit
2. **Task 2:** Mod-path injection block in `SDL3Main.cpp` (complete code is in the plan/spec — use it verbatim) → commit
3. **Task 3:** Build (`cmake --build build/android-vulkan --target z_generals`) + `bash scripts/build/android/package-android-zh.sh --install` (watch the `BUILD_DIR` mismatch caveat: script expects `build/android-game/`)
4. **Task 4:** 7-scenario on-device verification matrix (vanilla / mod.txt / Intent / precedence / invalid path / CRLF / loose-file workflow) — exact adb commands + expected logcat in the plan. Any deviation → stop and debug, don't proceed
5. **Task 5:** Docs (android.md mod section with measured results, DEV_BLOG entry, README "Mods" subsection) → commit

Execution mode not yet chosen: subagent-driven (fresh subagent per task) vs inline. Ask the user or default to subagent-driven per the plan header.

## Deferred Backlog (do NOT execute; preserved decisions)

D1 adversarial test harness · D2 RSS/fd telemetry · D3 ModManager/manifest/chains (BIG stays 32-bit on-disk, 64-bit in-memory) · D4 sandbox-safe config (strictly after D3) · D5 ModuleFactory alias seam (aliases only, no plugins) · D6 memory budget + LRU (graceful degradation) · D7 GUI/profiles/rollback/status (spec Non-Goals) · D8 engine loose-file resolution from mod dir (needs determinism review). Details in the plan's appendix.

## Non-Negotiable Invariants (DO NOT TOUCH)

1. `ArchiveFileSystem.cpp:158-183` — multimap erase-and-reinsert dance
2. `GameMemory.h/cpp` — magic cookie `0x47454d53`
3. BIG on-disk format — 32-bit big-endian

## Gotchas to Remember

- `adb logcat -G 16M` before every debug session (default 256KB overflows)
- `__argv` from `main()` is NOT heap — never `realloc`; every pointer escaping into it must be `static`
- INI parser throws `int` enums, not `std::exception` — use `__android_log_print` diagnostics
- DXVK `libdxvk_d3d8.so`/`libdxvk_d3d9.so` must NOT be stripped
- File paths use `/` on Android, not `\`
- `_stat`/`_S_IFDIR` portability is ALREADY solved (`file_compat.h` + define at `CommandLine.cpp:44`) — do not re-add a portability layer

## Side Notes

- 2026-07-09: user asked about `skill/grillme` — it does not exist anywhere on disk. Only reference is a dangling `skill(name="grill-me")` line at `~/.config/opencode/AGENTS.md:62` (OpenCode config, plan stress-testing tier). Options offered: recreate it, use the plan-reviewer agent instead, or remove the dangling reference. No decision yet.
