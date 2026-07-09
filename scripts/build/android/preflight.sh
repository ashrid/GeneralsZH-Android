#!/usr/bin/env bash
# preflight.sh — Android safe-build-loop guardrails. Runs BEFORE every build.
# Fails loud, cites the doc anchor, exits non-zero on the first violation.
# GeneralsX @build android-port 09/07/2026
# See: docs/WORKDIR/android-safe-build-loop.md §Guardrails
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "${REPO_ROOT}"

DOC="docs/WORKDIR/android-safe-build-loop.md §Guardrails"

fail() {
	echo "PREFLIGHT FAIL: $1" >&2
	echo "  doc anchor: ${DOC}" >&2
	exit 1
}

echo "=== preflight: 7 guardrail checks ==="

# GeneralsX @build android-port 09/07/2026 Reset the DXVK submodule before the clean-tree
# check. cmake/dx8.cmake applies Patches/dxvk-android.patch to references/fbraz3-dxvk at
# configure time (idempotently), dirtying the submodule after every build — which would
# spuriously fail commit-gating below. The patch is reproducible (in Patches/, re-applied
# by cmake), so discarding the applied state is safe; the next configure re-applies it.
# No-op if the submodule is not yet initialized.
# GeneralsX @bugfix Claude 10/07/2026 F9: warn loudly before discarding DXVK submodule
# local changes. cmake re-applies Patches/dxvk-android.patch idempotently, but a developer
# may have other in-flight DXVK edits that this reset silently destroys — surface it.
if [[ -n "$(git -C references/fbraz3-dxvk status --porcelain 2>/dev/null)" ]]; then
	echo "  NOTE: discarding DXVK submodule local changes (cmake re-applies Patches/dxvk-android.patch on next configure)." >&2
fi
git -C references/fbraz3-dxvk checkout -- . 2>/dev/null || true

# 1. Clean tree + HEAD hash (enforces commit-gating: one build per hash).
#    Tracked modifications only — untracked files (scratch notes, temp) are tolerated so
#    they don't block builds. The engine's CMake uses explicit source lists, not globs,
#    so an untracked file cannot sneak into the compile. Use `git status` to inspect all.
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
	fail "tracked tree not clean — commit-gating requires committed source. Run 'git status' (untracked files are OK)."
fi
HEAD_HASH="$(git rev-parse --short HEAD)"
echo "[1/7] clean tree OK — building HEAD ${HEAD_HASH}"

# 2. DXVK strip protection: keepDebugSymbols for libdxvk_d3d8/d3d9 (stripping => SIGSEGV)
GRADLE="android/app/build.gradle"
[[ -f "${GRADLE}" ]] || fail "missing ${GRADLE}"
grep -q "keepDebugSymbols" "${GRADLE}" \
	|| fail "keepDebugSymbols missing from ${GRADLE} — stripping libdxvk_*.so breaks Vulkan dispatch-table resolution (android.md §4.0)."
grep -q "libdxvk_d3d8" "${GRADLE}" && grep -q "libdxvk_d3d9" "${GRADLE}" \
	|| fail "DXVK .so keepDebugSymbols entries incomplete in ${GRADLE}."
echo "[2/7] DXVK keepDebugSymbols OK"

# 3. Memory-pool DMA magic cookie 0x47454d53 (do-not-revert)
GM_H="Core/GameEngine/Include/Common/GameMemory.h"
GM_CPP="Core/GameEngine/Source/Common/System/GameMemory.cpp"
[[ -f "${GM_H}" ]]   || fail "missing ${GM_H}"
[[ -f "${GM_CPP}" ]] || fail "missing ${GM_CPP}"
grep -q "0x47454d53" "${GM_CPP}" \
	|| fail "DMA magic cookie 0x47454d53 missing in ${GM_CPP} — do not revert (CLAUDE.md §2)."
echo "[3/7] memory-pool cookie OK"

# 4. Multimap erase-and-reinsert dance intact (override precedence)
AFS="Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp"
[[ -f "${AFS}" ]] || fail "missing ${AFS}"
# The dance = equal_range (get overwrite range) + erase (drop old) + insert (new first, then old).
grep -Eq "equal_range\(" "${AFS}" && grep -Eq "\.erase\(" "${AFS}" && grep -Eq "\.insert\(" "${AFS}" \
	|| fail "multimap erase-and-reinsert override dance missing in ${AFS} — do not simplify (android.md §4.2-4.3)."
echo "[4/7] multimap dance OK"

# 5. No new base-INI gating added in HEAD's diff. Catches ALL preprocessor forms:
#    #if / #ifdef / #ifndef, plus the negated #if ! form. Regex anchors to the line
#    start (after + and optional indent) so it matches real directives, NOT the text
#    appearing inside strings/comments (which caused a false positive on the probe).
#    GeneralsX @bugfix Claude 10/07/2026 F1: the old regex only matched bare '#if',
#    missing #ifdef/#ifndef (already used at ControlBar.cpp:3562,3565) — a new such
#    gate could reintroduce the FLESHY_SNIPER-class bug (android.md §4.1).
#    Limitation (F15): HEAD~1 compares against the first parent only; a merge commit's
#    other parent is not inspected. Initial commit skips (F16).
if git rev-parse --verify HEAD~1 >/dev/null 2>&1; then
	if git diff HEAD~1 HEAD | grep -E '^\+[[:space:]]*#if(def|ndef)?[[:space:]]+(![[:space:]]*)?(defined[[:space:]]*\([[:space:]]*)?RTS_GENERALS([^[:alnum:]_]|$)' >/dev/null; then
		fail "new RTS_GENERALS gate (#if/#ifdef/#ifndef/#if !) added in HEAD — re-gates base-INI definitions (android.md §4.1). Move the definition out of the gate."
	fi
	echo "[5/7] no new base-INI gating OK"
else
	echo "[5/7] WARNING: no parent commit — base-INI gating check skipped (initial commit, F16)"
fi

# 6. GeneralsX @ annotation in every changed source file of HEAD's diff
if git rev-parse --verify HEAD~1 >/dev/null 2>&1; then
	MISSING=0
	while IFS= read -r f; do
		[[ -z "${f}" ]] && continue
		case "${f}" in
			*.cpp|*.h|*.c|*.hpp)
				# GeneralsX @bugfix Claude 10/07/2026 F3: inspect the diff's ADDED lines,
				# not the whole file. The old check grepped HEAD:${f}, so a file with any
				# prior annotation passed even if the new change added none.
				added="$(git diff HEAD~1 HEAD -- "${f}" | grep -E '^\+[^+]')"
				if [[ -n "${added}" ]] && ! echo "${added}" | grep -q "GeneralsX @"; then
					echo "  missing annotation in new lines: ${f}" >&2
					MISSING=1
				fi
				;;
		esac
	done < <(git diff HEAD~1 HEAD --name-only --diff-filter=d)
	[[ "${MISSING}" -eq 0 ]] || fail "one or more changed source files lack a 'GeneralsX @' annotation."
	echo "[6/7] annotation convention OK"
else
	echo "[6/7] no parent commit — annotation check skipped (first commit)"
fi

# 7. Packager BUILD_DIR ends in android-vulkan (protects the setup fix)
PKG="scripts/build/android/package-android-zh.sh"
[[ -f "${PKG}" ]] || fail "missing ${PKG}"
grep -E '^BUILD_DIR=.*android-vulkan' "${PKG}" >/dev/null \
	|| fail "BUILD_DIR in ${PKG} must end in android-vulkan (build-loop setup fix)."
echo "[7/7] packager BUILD_DIR OK"

# 7b. (F10) Gradle SDL3 java srcDir must point at android-vulkan. Protects the F2 fix —
# both Gradle and the packager must agree on the build dir, else the .cxx fallback masks a
# mismatch until a fresh checkout fails Java compile.
grep -E 'build/android-vulkan/[^"]*sdl3-src' "${GRADLE}" >/dev/null \
	|| fail "Gradle SDL3 java srcDir in ${GRADLE} must point at build/android-vulkan (F2 fix)."
echo "  gradle SDL3 srcDir OK"

echo "=== preflight PASS — building ${HEAD_HASH} ==="
