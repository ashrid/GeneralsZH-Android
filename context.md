# Current Progress & Context

*Last updated: 2026-07-11*

A quick-status companion to [`README.md`](README.md). For the full story, read
[`android.md`](android.md) (canonical engineering log) first.

## TL;DR

The Android port boots, renders the main menu, accepts touch input, loads mods, and
**skirmish is playable**. Current focus: a **skirmish gameplay bug** — the enemy AI
spawns as a neutral faction. Root cause is diagnosed; the fix is pending a runtime
diagnostic log from a booting build.

## What works

- Engine init (all subsystem stores), DXVK D3D8→Vulkan rendering, main menu with text.
- Touch input (tap / drag / pinch), on-device stability (3 boot crashes fixed 2026-07-10).
- Mod loading (`mod.txt` / Intent extra) — verified on-device.
- **Skirmish boots and is playable** end-to-end.

## Current focus — skirmish enemy-AI bug

**Symptom:** in skirmish, the enemy team spawns as a **neutral faction** — no AI
controller, no victory condition, structures are capturable, units won't auto-attack.
The player's chosen color (e.g. red) isn't applied to the enemy.

**Root cause (diagnosed, fix pending):** the AI `Player` object is never created from its
GameInfo slot during `GameLogic::startNewGame`, so the map's player-2 start objects fall
back to **neutral ownership**. The whole skirmish-start path is correct by inspection, so
the defect is in *runtime data* (the AI slot's state at game-start), not logic.

**Why not fixed yet:** confirming the exact runtime condition needs a `[SKIRMISH]` log
from a **booting** instrumented build. The instrumented source is ready (16 probes in
`Player.cpp`, `PlayerList.cpp`, `GameLogic.cpp`), but the maintainer's build environment
hits an **Adreno driver boot crash** (SIGSEGV in `vkCreateAndroidSurfaceKHR` — a driver
defect, exhaustively ruled out as engine/DXVK-fixable; 9+ attempts + Oracle consult).
A build from a working environment boots fine.

**Next step:** rebuild the instrumented engine in a working env → start a skirmish →
capture `adb logcat -d -s GeneralsX:V > skirmish-diag.log` → apply the pre-built decision
tree (`slot[N]: state=? isOccupied=?` is the key line) → fix → remove probes.
Runbook: [`docs/WORKDIR/support/SKIRMISH_LOG_CAPTURE.md`](docs/WORKDIR/support/SKIRMISH_LOG_CAPTURE.md).

## Known limitations

- **Audio playback:** OpenAL backend fixed (opensl selected), but the **decoder is
  blocked** — FFmpeg can't build for arm64-android (upstream vcpkg#33963). Real audio
  needs a working FFmpeg build or an alternate decoder.
- **Video:** FFmpeg stubbed (same upstream blocker).
- **Boot crash in some build environments:** Adreno driver defect in
  `vkCreateAndroidSurfaceKHR` (see README §Skirmish Bug Investigation). Affects
  instrumented/rebuilt apks from the affected env; the release build boots.

## Key docs

- [`README.md`](README.md) — full status, how-to-play, build, chain-of-thought.
- [`android.md`](android.md) — canonical engineering log of every Android bug found + fixed (**read first**).
- [`docs/WORKDIR/support/SKIRMISH_LOG_CAPTURE.md`](docs/WORKDIR/support/SKIRMISH_LOG_CAPTURE.md) — runbook to capture the skirmish diagnostic log (immediate next step).
- [`AGENTS.md`](AGENTS.md) — instructions for AI coding agents on this repo.

## Recent session (2026-07-11)

1. Confirmed skirmish boots + is playable.
2. Discovered + diagnosed the enemy-AI-spawns-neutral bug (root cause: AI Player not
   created from its slot → neutral ownership).
3. Built `[SKIRMISH]` instrumentation (16 probes) to pinpoint the exact runtime condition.
4. Hit + exhaustively characterized a boot crash in the maintainer's build env (Adreno
   driver defect, not engine-fixable).
5. Wrote the capture runbook + this progress doc. Awaiting a `[SKIRMISH]` log from a
   working build environment to finish the fix.
