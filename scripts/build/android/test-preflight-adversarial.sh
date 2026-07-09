#!/usr/bin/env bash
# test-preflight-adversarial.sh — RED-PATH test for preflight.sh.
#
# A guardrail is only proven once it FAILS on its violation, not merely passes on a
# clean tree. For each of the 7 preflight checks this script introduces the genuine
# violation, runs preflight, and asserts it exits non-zero with the expected message.
# Any check that does NOT catch its violation is reported as a WEAK-CHECK finding.
#
# Safety: refuses on a dirty tracked tree; saves ORIG_HEAD; restores on exit (trap).
# Stages ONLY the specific violation file(s) — never `git add -A`/`.`, which would
# sweep THIS script into a violation commit and get it deleted by the reset.
#
# GeneralsX @build android-port 09/07/2026
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "${REPO_ROOT}"

PF="scripts/build/android/preflight.sh"
GRADLE="android/app/build.gradle"
GM_CPP="Core/GameEngine/Source/Common/System/GameMemory.cpp"
AFS="Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp"
PKG="scripts/build/android/package-android-zh.sh"

PASS=0; WEAK=0
RESULTS=()

if [[ -n "$(git status --porcelain | grep -v '^??')" ]]; then
	echo "REFUSE: tracked working-tree changes present — commit/stash first." >&2
	exit 2
fi
ORIG_HEAD="$(git rev-parse HEAD)"
cleanup() {
	git reset --hard "${ORIG_HEAD}" >/dev/null 2>&1 || true
	git -C references/fbraz3-dxvk checkout -- . >/dev/null 2>&1 || true
}
trap cleanup EXIT

run_pf() { set +e; PF_OUT="$(bash "${PF}" 2>&1)"; PF_EXIT=$?; set -e; }

expect_fail() {
	local check="$1" desc="$2" substr="$3"
	if [[ ${PF_EXIT} -ne 0 ]] && echo "${PF_OUT}" | grep -q "${substr}"; then
		RESULTS+=("  PASS  | ${check} | ${desc}"); PASS=$((PASS+1))
	else
		RESULTS+=("  WEAK  | ${check} | ${desc} — NOT caught (exit=${PF_EXIT})"); WEAK=$((WEAK+1))
		echo "    >>> preflight output for ${check}:" >&2
		echo "${PF_OUT}" | tail -4 | sed 's/^/        /' >&2
	fi
}

# Stage ONLY the named files — never the whole tree (would swallow this script).
commit_violation() {
	git add -- "$@"
	git commit -q -m "VIOLATION (adversarial test — discarded)" --no-verify
}
revert() { cleanup; }

echo "=== adversarial preflight test — baseline HEAD ${ORIG_HEAD:0:9} ==="
echo "(violations committed/reset locally; nothing reaches origin)"
echo ""

# --- Check 1: dirty tracked file (uncommitted) must fail ---
echo "violation" >> android.md
run_pf; expect_fail "check1" "dirty tracked file" "tree not clean"
git checkout -- android.md

# --- Control: untracked file must NOT fail check1 (check1 relaxed to tracked-only) ---
echo "scratch note" > _untracked_scratch.tmp
run_pf
if [[ ${PF_EXIT} -eq 0 ]]; then
	RESULTS+=("  PASS  | check1-relax | untracked file tolerated"); PASS=$((PASS+1))
else
	RESULTS+=("  WEAK  | check1-relax | untracked file rejected (exit=${PF_EXIT}) — relaxation not working"); WEAK=$((WEAK+1))
	echo "    >>> preflight output:" >&2; echo "${PF_OUT}" | tail -4 | sed 's/^/        /' >&2
fi
rm -f _untracked_scratch.tmp

# --- Control: dirty SUBMODULE only — reset must clean it, check1 must still pass ---
echo "simulated-patch" >> references/fbraz3-dxvk/RELEASE
sync  # flush the append before git reads it (defensive against WSL/Win FS lag)
run_pf
if [[ ${PF_EXIT} -eq 0 ]]; then
	RESULTS+=("  PASS  | reset  | dirty submodule cleaned (check1 passes)"); PASS=$((PASS+1))
else
	RESULTS+=("  WEAK  | reset  | dirty submodule NOT cleaned (exit=${PF_EXIT})"); WEAK=$((WEAK+1))
	echo "    >>> preflight output for reset control:" >&2
	echo "${PF_OUT}" | tail -4 | sed 's/^/        /' >&2
fi

# --- Check 2: remove keepDebugSymbols ---
sed -i 's/keepDebugSymbols/XX-VIOLATED-XX/g' "${GRADLE}"
commit_violation "${GRADLE}"
run_pf; expect_fail "check2" "keepDebugSymbols removed" "keepDebugSymbols"
revert

# --- Check 3: remove the DMA cookie (all occurrences) ---
sed -i 's/0x47454d53/0xDEADBEEF/g' "${GM_CPP}"
commit_violation "${GM_CPP}"
run_pf; expect_fail "check3" "memory cookie removed" "magic cookie"
revert

# --- Check 4: damage the override dance (remove equal_range — dance-unique) ---
sed -i 's/equal_range/XX-VIOLATED-XX/g' "${AFS}"
commit_violation "${AFS}"
run_pf; expect_fail "check4" "override dance damaged" "multimap"
revert

# --- Check 5: add #if RTS_GENERALS gating in a committed source change ---
printf '// GeneralsX @build adversarial-test 09/07/2026 probe\n#if RTS_GENERALS\nint unused_violation_marker = 0;\n#endif\n' > Core/_adversarial_probe.cpp
commit_violation Core/_adversarial_probe.cpp
run_pf; expect_fail "check5" "new RTS_GENERALS gating" "RTS_GENERALS"
revert
rm -f Core/_adversarial_probe.cpp

# --- Check 6: commit a new source file WITHOUT the GeneralsX @ annotation ---
printf '// no annotation here — should trip check 6\nint no_annotation_violation = 1;\n' > Core/_no_annotation_probe.cpp
commit_violation Core/_no_annotation_probe.cpp
run_pf; expect_fail "check6" "source file without annotation" "annotation"
revert
rm -f Core/_no_annotation_probe.cpp

# --- Check 7: BUILD_DIR no longer ends in android-vulkan ---
sed -i 's#BUILD_DIR="${REPO_ROOT}/build/android-vulkan"#BUILD_DIR="${REPO_ROOT}/build/android-game"#' "${PKG}"
commit_violation "${PKG}"
run_pf; expect_fail "check7" "BUILD_DIR wrong" "android-vulkan"
revert

echo ""
echo "=== results ==="
printf '%s\n' "${RESULTS[@]}"
echo ""
echo "PASS (caught): ${PASS}   WEAK (not caught): ${WEAK}"
[[ ${WEAK} -eq 0 ]] && { echo "ALL GUARDRAILS PROVEN ADVERSARIALLY."; exit 0; } || { echo "WEAK CHECK(S) FOUND — fix them."; exit 1; }
