// GeneralsX @feature Claude 31/07/2026 Integration test locking the retailScan wiring through
// the REAL StdBIGFileSystem::loadBigFilesFromDirectory. This target compiles
// Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFileSystem.cpp with -D__ANDROID__ (the
// same per-TU technique android_fallback_test uses for StdLocalFileSystem.cpp), so the Android
// filter branch is live on the Linux host. Verifies the two contracts the call sites depend on:
//   retailScan=TRUE  -> primary discovery skips top-level Mods/*.big and Data/INI/INIZH.big,
//                       loads the other retail archives;
//   retailScan=FALSE -> the selected-mod sweep opens every archive (a mod's own INIZH.big loads).
// Drives the real code with a stub LocalFileSystem (controlled file list) and a recording
// openArchiveFile override (no real .big I/O). A third case invokes the REAL
// ArchiveFileSystem::loadMods() with a recording subclass to lock the call-site contract
// (loadMods passes retailScan=FALSE). The two primary-discovery call sites remain explicit
// /*retailScan=*/TRUE literals in StdBIGFileSystem.cpp.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <Utility/CppMacros.h>

#include "PreRTS.h"

#include <doctest/doctest.h>

#include "Common/AsciiString.h"
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/FileSystem.h"
#include "Common/GameMemory.h"
#include "Common/GlobalData.h"
#include "Common/LocalFileSystem.h"
#include "StdDevice/Common/StdBIGFileSystem.h"

#include <set>
#include <string>
#include <vector>

// Stubs for engine symbols referenced by z_gameengine/z_gameenginedevice but defined in the
// app Main layer. Mirrors tests/archive_override_test.cpp so the test exe links.
HWND ApplicationHWnd = nullptr;
const Char *g_csfFile = "";
const Char *g_strFile = "";
void OSDisplaySetBusyState(Bool, Bool) {}
struct SDL_Window;
SDL_Window *TheSDL3Window = nullptr;
int __argc = 0;
char **__argv = nullptr;

static bool g_memInited = false;
static void ensureMemInit()
{
	if (!g_memInited)
	{
		initMemoryManager();
		g_memInited = true;
	}
}

// Stub LocalFileSystem: getFileListInDirectory emits a controlled, fixed list; the rest no-op.
// loadBigFilesFromDirectory only calls getFileListInDirectory, so only that path matters here.
class StubLocalFileSystem : public LocalFileSystem
{
public:
	std::vector<std::string> files;
	File *openFile(const Char *, Int, size_t) override { return nullptr; }
	Bool doesFileExist(const Char *) const override { return FALSE; }
	void getFileListInDirectory(const AsciiString &, const AsciiString &, const AsciiString &,
	                            FilenameList &filenameList, Bool) const override
	{
		for (const auto &f : files)
			filenameList.insert(AsciiString(f.c_str()));
	}
	Bool getFileInfo(const AsciiString &, FileInfo *) const override { return FALSE; }
	Bool createDirectory(AsciiString) override { return FALSE; }
	AsciiString normalizePath(const AsciiString &) const override { return AsciiString::TheEmptyString; }
	void init() override {}
	void reset() override {}
	void update() override {}
};

// StdBIGFileSystem subclass that records which archive paths reached openArchiveFile and returns
// nullptr (so loadIntoDirectoryTree is skipped — the test only cares whether the filter let each
// path through, not about archive contents).
class RecordingStdBIGFileSystem : public StdBIGFileSystem
{
public:
	std::set<std::string> opened;
	ArchiveFile *openArchiveFile(const Char *filename) override
	{
		opened.emplace(filename);
		return nullptr;
	}
};

// RAII: restore TheLocalFileSystem even when a failing REQUIRE throws.
struct LocalFsGuard
{
	LocalFileSystem *prev;
	LocalFsGuard() : prev(TheLocalFileSystem) {}
	~LocalFsGuard() { TheLocalFileSystem = prev; }
};

// RAII: restore TheWritableGlobalData so the GlobalData-backed case does not leak into siblings.
// (TheGlobalData is a read-only accessor over TheWritableGlobalData, so the writable pointer is
// what must be saved/restored.)
struct GlobalDataGuard
{
	GlobalData *prev;
	GlobalDataGuard() : prev(TheWritableGlobalData) {}
	~GlobalDataGuard() { TheWritableGlobalData = prev; }
};

// Recording base-class ArchiveFileSystem: captures the retailScan value loadMods() passes to
// loadBigFilesFromDirectory. This is the call-site lock — it invokes the REAL ArchiveFileSystem::
// loadMods() (Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp:323), so a future edit
// that drops or inverts the explicit retailScan=FALSE there now fails this test.
class RecordingArchiveFileSystem : public ArchiveFileSystem
{
public:
	Bool lastRetailScan = TRUE;
	int loadBigCallCount = 0;
	ArchiveFile *openArchiveFile(const Char *) override { return nullptr; }
	void closeArchiveFile(const Char *) override {}
	void closeAllArchiveFiles() override {}
	void postProcessLoad() override {}
	void init() override {}
	void reset() override {}
	void update() override {}
	void closeAllFiles() override {}
	Bool loadBigFilesFromDirectory(AsciiString, AsciiString, Bool, Bool retailScan) override
	{
		lastRetailScan = retailScan;
		++loadBigCallCount;
		return FALSE;
	}
};

// A representative primary-discovery file set: two retail archives that must load, plus the
// duplicate Data/INI/INIZH.big and a top-level Mods/ entry that retailScan=TRUE must skip.
static void seedRetailSet(StubLocalFileSystem &ls)
{
	ls.files = {"Data/INI.big", "Data/INI/INIZH.big", "Mods/Xeno/x.big", "Data/Textures.big"};
}

TEST_CASE("retailScan=TRUE: primary discovery skips Mods/ and Data/INI/INIZH.big, loads retail")
{
	ensureMemInit();
	StubLocalFileSystem ls;
	seedRetailSet(ls);
	LocalFsGuard g;
	TheLocalFileSystem = &ls;

	RecordingStdBIGFileSystem fs;
	fs.loadBigFilesFromDirectory(".", "*.big", FALSE, /*retailScan=*/TRUE);

	REQUIRE(fs.opened.count("Data/INI.big") == 1);
	REQUIRE(fs.opened.count("Data/Textures.big") == 1);
	REQUIRE(fs.opened.count("Data/INI/INIZH.big") == 0);
	REQUIRE(fs.opened.count("Mods/Xeno/x.big") == 0);
}

TEST_CASE("retailScan=FALSE: selected-mod sweep loads every archive (mod INIZH.big loads)")
{
	ensureMemInit();
	StubLocalFileSystem ls;
	seedRetailSet(ls);
	LocalFsGuard g;
	TheLocalFileSystem = &ls;

	RecordingStdBIGFileSystem fs;
	fs.loadBigFilesFromDirectory(".", "*.big", FALSE, /*retailScan=*/FALSE);

	// The mod sweep skips nothing — a mod's own INIZH.big and a Mods/ archive both load.
	REQUIRE(fs.opened.count("Data/INI.big") == 1);
	REQUIRE(fs.opened.count("Data/Textures.big") == 1);
	REQUIRE(fs.opened.count("Data/INI/INIZH.big") == 1);
	REQUIRE(fs.opened.count("Mods/Xeno/x.big") == 1);
}

TEST_CASE("loadMods() wiring: selected-mod sweep passes retailScan=FALSE to loadBigFilesFromDirectory")
{
	// Invokes the REAL ArchiveFileSystem::loadMods() with a recording subclass + a real GlobalData
	// (m_modDir set, m_modBIG empty so the openArchiveFile branch is skipped). Asserts loadMods
	// calls loadBigFilesFromDirectory exactly once with retailScan=FALSE — locking the call-site
	// contract that fix B made explicit (ArchiveFileSystem.cpp:345).
	ensureMemInit();
	GlobalData gd;
	GlobalDataGuard gdg;
	TheWritableGlobalData = &gd;
	gd.m_modDir = "Mods/Xeno";
	gd.m_modBIG.clear();

	StubLocalFileSystem ls;
	LocalFsGuard lsg;
	TheLocalFileSystem = &ls;

	RecordingArchiveFileSystem afs;
	afs.loadMods();

	REQUIRE(afs.loadBigCallCount == 1);
	REQUIRE_FALSE(afs.lastRetailScan);
}
