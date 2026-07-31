// GeneralsX @feature Claude 31/07/2026 Truth table for the Android archive skip predicate
// (Common/GeneralsXArchiveFilter.h). Locks the approved mod-file hierarchy:
//   - primary (retail) discovery excludes the scan root's top-level Mods/ subtree (inactive mods
//     are never mounted), and preserves the retail Data/INI/INIZH.big duplicate CRC-skip;
//   - the selected-mod sweep (retailScan=FALSE) skips nothing, so a selected mod's own INIZH.big
//     loads and its overrides win;
//   - a direct -mod file.big path (which loadMods opens via openArchiveFile, never reaching
//     loadBigFilesFromDirectory) is never wrongly skipped by the filter (case "direct bypass").
//
// This TU is compiled with -D__ANDROID__ (see tests/CMakeLists.txt) so the inline helper's
// Android branch is live and the assertions are meaningful rather than hitting the non-Android
// no-op. The helper is pure + dependency-free, so this target needs only doctest + the header.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "Common/GeneralsXArchiveFilter.h"

namespace {
inline bool shouldSkip(const char* path, const char* root, bool retailScan) {
	return GeneralsX_ShouldSkipArchiveEntry(path, root, retailScan);
}
} // namespace

TEST_CASE("primary discovery excludes the scan root's top-level Mods/ subtree (retailScan=TRUE)")
{
	CHECK(shouldSkip("GameData/Mods/B/foo.big", "GameData", true));           // T1 inactive Mods/B
	CHECK(shouldSkip("Mods/A.big", "GameData", true));                         // T2 relative return, no root prefix
	CHECK(shouldSkip("/abs/GameData/Mods/A/foo.big", "/abs/GameData", true)); // T3 absolute root + path
	CHECK(shouldSkip("GameData/Mods/A.big", "GameData/", true));              // T12 trailing slash on root
	CHECK(shouldSkip("gamedata\\mods\\a\\foo.big", "gamedata", true));         // T11 backslash + case
}

TEST_CASE("selected-mod sweep skips nothing (retailScan=FALSE), incl. selected INIZH.big")
{
	CHECK_FALSE(shouldSkip("/abs/GameData/Mods/A/foo.big", "/abs/GameData", false));              // T4
	CHECK_FALSE(shouldSkip("/abs/GameData/Mods/A/Data/INI/INIZH.big", "/abs/GameData", false));   // T5
}

TEST_CASE("retail INIZH.big CRC guard preserved; retail root archives still load (retailScan=TRUE)")
{
	CHECK(shouldSkip("/abs/GameData/Data/INI/INIZH.big", "/abs/GameData", true));   // T6 retail dup skipped
	CHECK_FALSE(shouldSkip("/abs/GameData/INI.big", "/abs/GameData", true));        // T7 retail root loads
	CHECK_FALSE(shouldSkip("/abs/GameData/AlwaysOn.big", "/abs/GameData", true));   // T8 root-level always-on
}

TEST_CASE("direct -mod file.big path bypasses the primary Mods/ filter")
{
	// loadMods() opens m_modBIG via openArchiveFile (ArchiveFileSystem.cpp:327) and never reaches
	// loadBigFilesFromDirectory (:343) — the only site that consults this filter. So a direct .big
	// is filtered by neither the Mods/ exclusion nor the INIZH skip. Assert the predicate agrees:
	// a direct path that is neither under Mods/ nor a retail INIZH.big is never skipped.
	CHECK_FALSE(shouldSkip("/abs/direct.big", "/abs/GameData", true));             // T9 direct .big, LOAD
	CHECK_FALSE(shouldSkip("/sdcard/ZH/MyMod.big", "/abs/GameData", true));        // direct path outside root
}

TEST_CASE("only the scan root's Mods/ is excluded, not arbitrary Mods dirs elsewhere")
{
	CHECK_FALSE(shouldSkip("UserData/Mods/x.big", "/abs/GameData", true));         // T10 unrelated Mods dir
}

TEST_CASE("defensive: empty/null path is a no-op (LOAD)")
{
	CHECK_FALSE(shouldSkip("", "GameData", true));       // T13 empty path
	CHECK_FALSE(shouldSkip(nullptr, "GameData", true));  // null path
}
