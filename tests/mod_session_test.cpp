// GeneralsX @feature Claude 31/07/2026 RED phase: session-based Mods status predicate tests.
// Locks the contract of GeneralsX_SessionModLoadedFrom(modDir, modBIG) BEFORE any production
// implementation exists. The predicate reports whether a mod was selected for THIS launch via
// the parsed -mod / -modBIG command-line (TheGlobalData->m_modDir / m_modBIG), as opposed to
// the persistent GameData/mod.txt default. The definition lands later in MainMenu.cpp; until
// then this target MUST fail to link (undefined reference to GeneralsX_SessionModLoadedFrom).
//
// Four cases pin the truth table:
//   both empty  -> FALSE (no session mod)
//   dir only    -> TRUE
//   BIG only    -> TRUE
//   both        -> TRUE
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <Utility/CppMacros.h>

#include "PreRTS.h"

#include <doctest/doctest.h>

#include "Common/AsciiString.h"
#include "Common/GameMemory.h"

// Stubs for engine symbols referenced by z_gameengine / z_gameenginedevice but defined in the
// app Main layer. The test only exercises GeneralsX_SessionModLoadedFrom; these are no-ops.
// Mirrors tests/archive_override_test.cpp so the test exe links the same engine archives.
HWND ApplicationHWnd = nullptr;
const Char *g_csfFile = "";
const Char *g_strFile = "";
void OSDisplaySetBusyState(Bool, Bool) {}
struct SDL_Window;  // forward declare (the test doesn't use SDL3 directly)
SDL_Window *TheSDL3Window = nullptr;
int __argc = 0;
char **__argv = nullptr;

// Declaration ONLY. The definition lives in the engine (MainMenu.cpp, added in the GREEN
// phase). Linking this target against z_gameengine MUST fail until that definition exists.
Bool GeneralsX_SessionModLoadedFrom(const AsciiString &modDir, const AsciiString &modBIG);

static bool g_memInited = false;
static void ensureMemInit()
{
	if (!g_memInited)
	{
		initMemoryManager();
		g_memInited = true;
	}
}

TEST_CASE("session mod: both modDir and modBIG empty -> FALSE")
{
	// Given: no mod was selected for this launch.
	ensureMemInit();
	AsciiString modDir;   // default-constructed -> empty
	AsciiString modBIG;   // default-constructed -> empty
	// When: the predicate inspects the parsed session arguments.
	// Then: no session mod is active.
	REQUIRE(GeneralsX_SessionModLoadedFrom(modDir, modBIG) == FALSE);
}

TEST_CASE("session mod: modDir only -> TRUE")
{
	// Given: a -mod directory was parsed for this session, no -modBIG archive.
	ensureMemInit();
	AsciiString modDir;
	modDir = "/sdcard/Android/data/me.generalsx.zh/files/GameData/Mods/MyMod";
	AsciiString modBIG;   // empty
	// When / Then: a directory-only session mod counts as loaded.
	REQUIRE(GeneralsX_SessionModLoadedFrom(modDir, modBIG) == TRUE);
}

TEST_CASE("session mod: modBIG only -> TRUE")
{
	// Given: a -modBIG archive was parsed for this session, no -mod directory.
	ensureMemInit();
	AsciiString modDir;   // empty
	AsciiString modBIG;
	modBIG = "/sdcard/Android/data/me.generalsx.zh/files/GameData/Mods/MyMod/MyMod.big";
	// When / Then: a BIG-only session mod counts as loaded.
	REQUIRE(GeneralsX_SessionModLoadedFrom(modDir, modBIG) == TRUE);
}

TEST_CASE("session mod: both modDir and modBIG -> TRUE")
{
	// Given: both -mod directory and -modBIG archive were parsed for this session.
	ensureMemInit();
	AsciiString modDir;
	modDir = "/sdcard/Android/data/me.generalsx.zh/files/GameData/Mods/MyMod";
	AsciiString modBIG;
	modBIG = "/sdcard/Android/data/me.generalsx.zh/files/GameData/Mods/MyMod/MyMod.big";
	// When / Then: both-present counts as a loaded session mod.
	REQUIRE(GeneralsX_SessionModLoadedFrom(modDir, modBIG) == TRUE);
}
