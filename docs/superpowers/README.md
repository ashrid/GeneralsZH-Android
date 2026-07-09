# Android Mod Support — Planning Workspace

**Last updated:** 2026-07-08
**Status:** Planning complete, awaiting Wave 1 execution

---

## What This Is

This directory contains the design specs and implementation plans for enabling mod support on the Generals Zero Hour Android port.

The engine already supports mods via `-mod <path>` — the work here is about:
1. **Wiring** the `-mod` parameter into Android's launch mechanism (spec)
2. **Expanding** the architecture for large mods, 64-bit safety, memory management, and UX (plan)

## Documents

| File | Purpose | Status |
|------|---------|--------|
| `specs/2026-07-08-android-mod-support-design.md` | Spec: argv-injection + config file for passing `-mod` on Android | ✅ Revised (post-review) |
| `plans/2026-07-08-android-mod-architecture.md` | Plan: 6-phase, 18-task architecture improvements | ✅ Ready for execution |
| `context.md` | Session handoff file — read this to continue work | ✅ |

## Quick Summary

### Spec: Getting `-mod` to Work on Android

**Two approaches:**
- **Config file** (primary): user writes mod path in `mod.txt`, launches normally
- **Intent extra** (secondary): `adb shell am start --es "mod" "/path/to/mod"`

**Files to change:** `SDL3Main.cpp`, `CommandLine.cpp`

**Critical bugs found & fixed in reviews:**
- `realloc()` on non-heap `__argv` → static buffer pattern instead
- Backslash path separator on Android → use `/` on non-Windows
- Code placement after command-line parsing → must run BEFORE parsing

### Plan: Architecture Improvements

**6 phases, 6 waves:**

| Phase | Focus | Risk | Tasks |
|-------|-------|------|-------|
| 0 | Adversarial test harness + telemetry | LOW | 3 |
| 1 | 64-bit type safety + bounds checks | MEDIUM | 2 |
| 2 | Memory budget + LRU eviction + fd limits | HIGH | 4 |
| 3 | ModManager + chains + conflict detection | HIGH | 5 |
| 4 | UX: picker, profiles, rollback, status | MEDIUM | 5 |
| 5 | Polish: portability, sandbox, GameMemory audit | LOW-MED | 3 |

**Decisions made:**
- ModuleFactory: minimal alias registration (no plugin system)
- BIG format: 32-bit on-disk stays (backwards compatible), 64-bit in-memory
- Tests: adversarial design (break-it-first, not happy-path)
- Memory: graceful eviction, hard ceiling as last resort

## Non-Negotiable Invariants

1. **Override-precedence multimap dance** in `ArchiveFileSystem.cpp:158-183` — do not simplify
2. **GameMemory DMA magic cookie** (`0x47454d53`) — do not revert
3. **BIG on-disk format** stays 32-bit big-endian
