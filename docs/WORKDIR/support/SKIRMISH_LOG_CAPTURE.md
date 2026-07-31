# Skirmish Diagnostic — Capture the `[SKIRMISH]` Log

**Purpose:** Capture the runtime `[SKIRMISH]` probe log from a **booting** instrumented
build, so the neutral-enemy bug can be pinpointed and fixed.

**Why this guide exists:** The `[SKIRMISH]` instrumentation is already in the source
(3 files), but the maintainer's build environment hits an Adreno driver defect at boot
(`vkCreateAndroidSurfaceKHR` null-deref) and cannot produce a booting APK. A build from
a working environment boots fine. So: build it where it boots, start a skirmish, capture
the log, share it.

**Time:** ~20–40 min (mostly build time).

---

## Prerequisites

- Android NDK r27 (`$ANDROID_NDK_HOME`), Android SDK + build-tools 35
- CMake ≥ 3.25, Ninja, Meson
- DXVK submodule initialized:
  ```bash
  cd ~/.projects/GeneralsZH-Android
  git submodule update --init --recursive references/fbraz3-dxvk   # only if not already done
  ```
- A connected device via `adb` (`adb devices` shows it), with the game's `.big` data
  already in `/sdcard/Android/data/me.generalsx.zh/files/GameData/Data/` (it's already
  there from prior sessions — no need to re-push).

---

## Step 1 — Confirm the instrumentation is in the source

```bash
cd ~/.projects/GeneralsZH-Android
git status --short
```

You should see exactly these 3 files modified (the `[SKIRMISH]` probes) plus the
pre-existing DXVK submodule:

```
 M GeneralsMD/Code/GameEngine/Source/Common/RTS/Player.cpp
 M GeneralsMD/Code/GameEngine/Source/Common/RTS/PlayerList.cpp
 M GeneralsMD/Code/GameEngine/Source/GameLogic/System/GameLogic.cpp
 m references/fbraz3-dxvk
```

If those 3 are there, you're set. (If not, stop and ask — the probes need re-adding.)

Optional sanity check that the probes compiled into the binary (after Step 2):
```bash
strings build/android-vulkan/GeneralsMD/Code/Main/libmain.so | grep SKIRMISH | head
# expect lines like: [SKIRMISH]   slot[%d]: state(int)=%d isOccupied=%d ...
```

---

## Step 2 — Build the instrumented engine

**Option A — fresh/clean (recommended; wipes stale artifacts):**
```bash
cd ~/.projects/GeneralsZH-Android
rm -rf build/android-vulkan
cmake --preset android-vulkan
cmake --build build/android-vulkan --target z_generals
```

**Option B — incremental (faster; rebuilds the 3 files + DXVK from reverted source):**
```bash
cmake --build build/android-vulkan --target z_generals
```

Either way the 3 instrumented files compile and `libmain.so` contains the probes.

---

## Step 3 — Package + install the APK

```bash
./scripts/build/android/package-android-zh.sh --install
```

This stages `libmain.so` + DXVK + SDL3 + fonts, strips, signs, and installs onto the
connected device in one step. (Your `.big` game data is untouched.)

If `--install` doesn't work, do it manually:
```bash
./scripts/build/android/package-android-zh.sh
adb install -r android/app/build/outputs/apk/release/app-release.apk
```

---

## Step 4 — Prepare logcat (critical)

The default logcat buffer (256 KB) overflows with the engine's verbose boot logs and
hides the `[SKIRMISH]` lines. Enlarge it:

```bash
adb logcat -G 16M
adb logcat -c
```

---

## Step 5 — Launch and start a skirmish

```bash
adb shell am start -n me.generalsx.zh/.GameActivity
```

Then on the device:
1. Wait for the **main menu** to render.
2. Tap **Skirmish**.
3. Configure: **you = human** (any faction), **slot 1 = AI** (Easy/Normal/Brutal),
   and give the AI a distinct color (e.g. **red**) — same setup as when you saw the bug.
4. Tap **Play**.
5. Let the game run **~15 seconds** once in-game (so the `[SKIRMISH]` probes fire during
   game-start).

> If the game **crashes at boot** instead of reaching the menu, that's Outcome B
> (Step 7) — capture anyway.

---

## Step 6 — Capture the log

```bash
adb logcat -d -s GeneralsX:V > skirmish-diag.log
```

This grabs every `GeneralsX`-tagged line: boot + the `[SKIRMISH]` probes + any crash
context. Drop it in the **repo root** (`~/.projects/GeneralsZH-Android/skirmish-diag.log`)
and report it ready.

Quick check that the probes fired:
```bash
# Windows PowerShell:
findstr /c:"[SKIRMISH]" skirmish-diag.log
# or Linux/WSL:
grep SKIRMISH skirmish-diag.log
```
Expect lines like `[SKIRMISH] startNewGame: m_gameMode(int)=...` and
`[SKIRMISH]   slot[N]: state(int)=...`.

---

## Step 7 — Two outcomes

- **It booted and you see `[SKIRMISH]` lines** → the `slot[N]: state=? isOccupied=?`
  line is the decision point (see legend below). The fix follows directly.
- **It crashed at boot** (no `[SKIRMISH]` lines; logcat shows
  `SIGSEGV ... CreateAndroidSurfaceKHR`) → the driver defect reproduced in this env too.
  Send the log anyway; the next mitigation is disabling `VK_EXT_surface_maintenance1`
  in `references/fbraz3-dxvk/src/dxvk/dxvk_extensions.h` (line ~345, instance ext).

---

## `[SKIRMISH]` line legend (what each probe reveals)

| Log line | Reveals |
|---|---|
| `startNewGame: m_gameMode(int)=? isGAME_SKIRMISH=? TheGameInfo=?` | Is it in skirmish mode with a non-null game info? |
| `slot[N]: state=? isOccupied=? isHuman=? isAI=? team=? color=?` | **The key line** — is the AI slot occupied and colored at game-start? |
| `slot-loop i=N: enemies=[...] allies=[...]` | Was an enemies string built for the AI? |
| `PlayerList::newGame: numSides=?` | How many sides became players? |
| `created player idx=N: playerType=?` | Did an AI Player actually get created? |
| `rel-side i=N: enemies=[...] foundP=?` | Were the ENEMIES relationships applied? |
| `enemy-tok=... foundP2=?` | Did each enemy name resolve to a player? |
| `initFromDict: ... skirmishMatched=? forceHuman=?` | Did the map have a skirmish side for the AI faction? |

### SlotState enum values (for `slot[N]: state=?`)
`SLOT_OPEN=0, SLOT_CLOSED=1, SLOT_EASY_AI=2, SLOT_MED_AI=3, SLOT_BRUTAL_AI=4, SLOT_PLAYER=5`

### Decision tree (signature → root cause → fix location)

| Branch | Log signature | Root cause | Fix location |
|---|---|---|---|
| **A** | `isGAME_SKIRMISH=0` or `TheGameInfo=0x0` | Never entered skirmish mode / `TheSkirmishGameInfo` null | `SkirmishGameOptionsMenu.cpp` `reallyDoStart()` → `MSG_NEW_GAME` handler |
| **B** | `slot[N]: state=0/1 isOccupied=0` (AI slot OPEN/CLOSED) | AI slot state didn't persist from GUI → `closeOpenSlots` closed it | Slot-config callback / `setState` persistence (SkirmishGameOptionsMenu) |
| **C** | `slot[N]: state=5 isAI=0` (AI slot became PLAYER) | Slot-state cycling landed on human | Slot-state cycle callback |
| **D** | `slot[N]` correct AI, **but** `initFromDict: skirmishMatched=0 forceHuman=1` | Map has no skirmish side for the AI faction → dormant human | `Player.cpp:807-822` skirmish-side match, or the map's skirmish definitions |
| **E** | `created player: playerType=COMPUTER`, **but** `enemy-tok foundP2=0` or `enemies=[]` | AI Player exists but ENEMIES relationship not applied | `PlayerList.cpp:204-211` name-key lookup, or `GameLogic.cpp:1448-1484` enemiesString |
| **F** | All correct (slot occupied, player created, ENEMIES set) yet bug persists | H1 wrong; issue elsewhere | Pivot to deeper instrumentation |

**Most likely (given the "red color not applied" evidence):** Branch **B** (GUI/touch
didn't persist the AI slot — Android-specific) or **D** (map skirmish-side mismatch).

---

## After the fix

Once the root cause is confirmed and fixed, the 3 instrumentation files are reverted
(clean source restored). The `[SKIRMISH]` probes are diagnostic-only and not meant to
ship.
