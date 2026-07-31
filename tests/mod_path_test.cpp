// GeneralsX @feature Claude 31/07/2026 RED phase: Android persisted relative mod-path
// normalization tests. Locks the contract of GeneralsX_NormalizeModPath(rawPath,
// gameDataPath) BEFORE any production implementation exists.
//
// Device evidence: ModPicker writes "Mods/<name>" (a path RELATIVE to GameData) and SDL
// injects it as -mod; parseMod then treats it as relative to the sandboxed config path
// instead of GameData, leaving TheGlobalData->m_modDir empty. An absolute Intent path
// (e.g. /sdcard/Android/data/.../Mods/MyMod) succeeds. The normalizer must turn a
// relative ModPicker path into an absolute GameData-rooted path while passing absolute
// paths through untouched. The definition lands later (GREEN phase); until then this
// target MUST fail to link (undefined reference to GeneralsX_NormalizeModPath).
//
// Four independent Given/When/Then cases pin the contract:
//   relative path   -> joined under gameDataPath (Mods/Foo + /game-data -> /game-data/Mods/Foo)
//   absolute path   -> returned unchanged (Android Intent path wins as-is)
//   empty raw path  -> stays empty (do not synthesize a base-only path)
//   trailing slash  -> base "/game-data/" must not collapse into "//" when joining
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <Utility/CppMacros.h>

#include "PreRTS.h"

#include <doctest/doctest.h>

#include "Common/AsciiString.h"
#include "Common/GameMemory.h"

// Stubs for engine symbols referenced by z_gameengine / z_gameenginedevice but defined
// in the app Main layer. The test only exercises GeneralsX_NormalizeModPath; these are
// no-ops. Mirrors tests/mod_session_test.cpp so the test exe links the same engine archives.
HWND ApplicationHWnd = nullptr;
const Char *g_csfFile = "";
const Char *g_strFile = "";
void OSDisplaySetBusyState(Bool, Bool) {}
struct SDL_Window;  // forward declare (the test doesn't use SDL3 directly)
SDL_Window *TheSDL3Window = nullptr;
int __argc = 0;
char **__argv = nullptr;

// Declaration ONLY. The definition lives in the engine (added in the GREEN phase).
// Linking this target against z_gameengine MUST fail until that definition exists.
AsciiString GeneralsX_NormalizeModPath(const AsciiString &rawPath, const AsciiString &gameDataPath);

static bool g_memInited = false;
static void ensureMemInit()
{
	if (!g_memInited)
	{
		initMemoryManager();
		g_memInited = true;
	}
}

TEST_CASE("mod path: relative Mods/Foo is joined under gameDataPath")
{
	// Given: ModPicker wrote a path relative to GameData, and GameData is rooted at /game-data.
	ensureMemInit();
	AsciiString rawPath("Mods/Foo");
	AsciiString gameDataPath("/game-data");
	// When: the normalizer resolves the relative path against GameData.
	// Then: it becomes an absolute GameData-rooted path.
	REQUIRE(GeneralsX_NormalizeModPath(rawPath, gameDataPath) == "/game-data/Mods/Foo");
}

TEST_CASE("mod path: absolute Android Intent path is returned unchanged")
{
	// Given: an absolute Android path arrived via Intent extra; GameData is a different root.
	ensureMemInit();
	AsciiString rawPath("/sdcard/Android/data/me.generalsx.zh/files/GameData/Mods/MyMod");
	AsciiString gameDataPath("/sdcard/Android/data/me.generalsx.zh/files/GameData");
	// When: the normalizer sees the path is already absolute.
	// Then: it is returned as-is, NOT re-joined under gameDataPath.
	REQUIRE(GeneralsX_NormalizeModPath(rawPath, gameDataPath) == rawPath);
}

TEST_CASE("mod path: empty raw path stays empty")
{
	// Given: no mod path was supplied (default-constructed AsciiString is empty).
	ensureMemInit();
	AsciiString rawPath;            // empty
	AsciiString gameDataPath("/game-data");
	// When: the normalizer receives an empty raw path.
	// Then: it must not synthesize a base-only path; the result stays empty.
	REQUIRE(GeneralsX_NormalizeModPath(rawPath, gameDataPath).isEmpty());
}

TEST_CASE("mod path: trailing base slash does not create a double slash")
{
	// Given: gameDataPath already ends with a separator.
	ensureMemInit();
	AsciiString rawPath("Mods/Foo");
	AsciiString gameDataPath("/game-data/");  // note trailing '/'
	// When: the normalizer joins the relative path onto the base.
	// Then: exactly one separator appears between base and relative path.
	REQUIRE(GeneralsX_NormalizeModPath(rawPath, gameDataPath) == "/game-data/Mods/Foo");
}
