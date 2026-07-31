// GeneralsX @feature Claude 31/07/2026 RED phase: Android loose-mod fallback discovery test.
// Guards the proven Android bug in StdLocalFileSystem.cpp:fixFilenameFromWindowsPath, where the
// #if defined(__ANDROID__) early-return (added to dodge the directory_iterator heap corruption)
// skips the s_assetFallbackPath / s_assetFallbackPaths checks entirely. loadMods() registers a
// mod dir via setAssetFallbackPaths(...), so a loose Data/INI/Locomotor.ini shipped in the mod
// can never override the retail archive entry on Android (it resolves on Linux/macOS, which is
// why the bug is device-only).
//
// How this exercises the Android path on a Linux host build: the test target ALSO compiles the
// real Core/GameEngineDevice/Source/StdDevice/Common/StdLocalFileSystem.cpp as one of its
// sources, with -D__ANDROID__ added for that translation unit only (plus the CppMacros.h
// force-include z_gameenginedevice normally gets via PCH). Because that object supplies every
// StdLocalFileSystem::* symbol ahead of the z_gameenginedevice STATIC archive, the archive's own
// (Linux) StdLocalFileSystem.o is never pulled — so doesFileExist/openFile here run the genuine
// __ANDROID__ code path. No directory_iterator is ever invoked.
//
// Three independent Given/When/Then cases:
//   mod root only     -> doesFileExist succeeds (the regression)
//   file absent       -> doesFileExist fails (no false positives)
//   primary + mod     -> openFile reads the PRIMARY root content (precedence preserved)
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <Utility/CppMacros.h>

#include "PreRTS.h"

#include <doctest/doctest.h>

#include "Common/AsciiString.h"
#include "Common/ArchiveFile.h"
#include "Common/ArchiveFileSystem.h"
#include "Common/FileSystem.h"
#include "Common/GameMemory.h"
#include "StdDevice/Common/StdLocalFileSystem.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

// Stubs for engine symbols referenced by z_gameengine / z_gameenginedevice but defined in the
// app Main layer. The test only exercises StdLocalFileSystem; these are no-ops. Mirrors
// tests/archive_override_test.cpp so the test exe links the same engine archives.
HWND ApplicationHWnd = nullptr;
const Char *g_csfFile = "";
const Char *g_strFile = "";
void OSDisplaySetBusyState(Bool, Bool) {}
struct SDL_Window;  // forward declare (the test doesn't use SDL3 directly)
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

// RAII: restore cwd and tear down temp dirs even when a failing REQUIRE throws, so a RED run
// never leaves the host cwd changed or temp files lying around.
struct AndroidFallbackWorkspace
{
	std::filesystem::path prevCwd;
	std::filesystem::path cleanCwd;
	std::filesystem::path primaryRoot;
	std::filesystem::path modRoot;

	~AndroidFallbackWorkspace()
	{
		std::error_code ec;
		if (!prevCwd.empty()) std::filesystem::current_path(prevCwd, ec);
		if (!cleanCwd.empty()) std::filesystem::remove_all(cleanCwd, ec);
		if (!primaryRoot.empty()) std::filesystem::remove_all(primaryRoot, ec);
		if (!modRoot.empty()) std::filesystem::remove_all(modRoot, ec);
	}
};

static std::filesystem::path uniqueTempDir(const char *tag)
{
	std::string name = std::string("generalsx_android_fallback_") + tag + "_" + std::to_string(::getpid());
	std::filesystem::path p = std::filesystem::temp_directory_path() / name;
	std::filesystem::remove_all(p);
	std::filesystem::create_directories(p);
	return p;
}

static void writeFile(const std::filesystem::path &p, const std::string &content)
{
	std::filesystem::create_directories(p.parent_path());
	std::ofstream f(p);
	f << content;
}

TEST_CASE("Android loose mod fallback: relative Data/INI/Locomotor.ini found via registered mod root")
{
	// Given: cwd has no Data/ subtree; a mod root registered via setAssetFallbackPaths holds the
	// loose file (this is what loadMods() wires on device).
	ensureMemInit();
	AndroidFallbackWorkspace ws;
	ws.prevCwd = std::filesystem::current_path();
	ws.cleanCwd = uniqueTempDir("cwd");
	ws.modRoot = uniqueTempDir("mod");
	writeFile(ws.modRoot / "Data" / "INI" / "Locomotor.ini", "loose-mod-override");
	std::filesystem::current_path(ws.cleanCwd);

	StdLocalFileSystem fsdev;
	std::vector<AsciiString> modPaths;
	modPaths.push_back(AsciiString(ws.modRoot.string().c_str()));
	fsdev.setAssetFallbackPaths(modPaths);

	// When: Android-style lookup (this exe links the __ANDROID__ build of the unit) for a file
	// that only exists under the registered mod root.
	// Then: it is discovered via the fallback path rather than being reported missing.
	REQUIRE(fsdev.doesFileExist("Data/INI/Locomotor.ini"));
}

TEST_CASE("Android loose mod fallback: absent file is not falsely reported present")
{
	// Given: cwd and the registered mod root both lack the requested file.
	ensureMemInit();
	AndroidFallbackWorkspace ws;
	ws.prevCwd = std::filesystem::current_path();
	ws.cleanCwd = uniqueTempDir("cwd_neg");
	ws.modRoot = uniqueTempDir("mod_neg");
	std::filesystem::current_path(ws.cleanCwd);

	StdLocalFileSystem fsdev;
	std::vector<AsciiString> modPaths;
	modPaths.push_back(AsciiString(ws.modRoot.string().c_str()));
	fsdev.setAssetFallbackPaths(modPaths);

	// When: Android-style lookup for a file present nowhere.
	// Then: it is not found (the fallback fix must not introduce false positives).
	REQUIRE_FALSE(fsdev.doesFileExist("Data/INI/DoesNotExist.ini"));
}

TEST_CASE("Android loose mod fallback: primary asset root wins over mod root (precedence)")
{
	// Given: both the primary asset root (setAssetRootPath -> s_assetFallbackPath) and a mod root
	// (setAssetFallbackPaths -> s_assetFallbackPaths) contain the same relative loose file, with
	// distinguishable content.
	ensureMemInit();
	AndroidFallbackWorkspace ws;
	ws.prevCwd = std::filesystem::current_path();
	ws.cleanCwd = uniqueTempDir("cwd_prec");
	ws.primaryRoot = uniqueTempDir("primary");
	ws.modRoot = uniqueTempDir("mod_prec");
	writeFile(ws.primaryRoot / "Data" / "INI" / "Locomotor.ini", "PRIMARY");
	writeFile(ws.modRoot / "Data" / "INI" / "Locomotor.ini", "MOD");
	std::filesystem::current_path(ws.cleanCwd);

	StdLocalFileSystem fsdev;
	fsdev.setAssetRootPath(AsciiString(ws.primaryRoot.string().c_str()));
	std::vector<AsciiString> modPaths;
	modPaths.push_back(AsciiString(ws.modRoot.string().c_str()));
	fsdev.setAssetFallbackPaths(modPaths);

	// When: Android-style openFile reads the resolved loose file.
	// Then: the primary asset root content is returned, proving s_assetFallbackPath is checked
	// before s_assetFallbackPaths (precedence preserved by the fix).
	File *f = fsdev.openFile("Data/INI/Locomotor.ini", File::READ);
	REQUIRE(f != nullptr);
	char buf[32] = {0};
	Int n = f->read(buf, static_cast<Int>(sizeof(buf) - 1));
	if (n < 0) n = 0;
	if (n > static_cast<Int>(sizeof(buf) - 1)) n = static_cast<Int>(sizeof(buf) - 1);
	buf[n] = '\0';
	f->close();
	REQUIRE(std::string(buf) == "PRIMARY");
}

// GeneralsX @feature Claude 31/07/2026 Regression guard for the public FileSystem::openFile
// seam: a loose file resolved via a registered mod fallback root must win over an archive-
// provided file at the same logical path. FileSystem::openFile (Common/System/FileSystem.cpp)
// consults TheLocalFileSystem first and falls back to TheArchiveFileSystem only when local
// returns null, so a loadMods()-registered loose mod file (setAssetFallbackPaths) outranks both
// selected-mod .big files and retail archives. This is already the engine's behavior; this case
// locks it so a future refactor cannot silently flip the local-vs-archive boundary.

// Minimal ArchiveFile substitute. Its openFile records whether FileSystem::openFile ever reached
// the archive (it must not, because the loose mod file wins). Registered in the archive directory
// tree so ArchiveFileSystem::doesFileExist resolves the path — i.e. the archive genuinely offers
// Data/INI/Locomotor.ini (standing in for a mod .big that would serve MOD_BIG). Mirrors the stub
// shape in tests/archive_override_test.cpp.
class TestArchiveFile : public ArchiveFile
{
	AsciiString m_name;
public:
	mutable bool openFileCalled = false;
	explicit TestArchiveFile(const char *name) : m_name(name) {}
	Bool      getFileInfo(const AsciiString &, FileInfo *) const override { return FALSE; }
	File *    openFile(const Char *, Int) override { openFileCalled = true; return nullptr; }
	void      closeAllFiles() override {}
	AsciiString getName() override { return m_name; }
	AsciiString getPath() override { return m_name; }
	void      setSearchPriority(Int) override {}
	void      close() override {}
	// Populate the archive's flat file map so the inherited getFileListInDirectory emits this
	// path; loadIntoDirectoryTree then routes it into the matching subdirectory.
	void      addFileDirect(const AsciiString &path, const ArchivedFileInfo &info)
	{
		m_rootDirectory.m_files[path] = info;
	}
};

class TestArchiveFileSystem : public ArchiveFileSystem
{
public:
	ArchiveFile *openArchiveFile(const Char *) override { return nullptr; }
	void         closeArchiveFile(const Char *) override {}
	void         closeAllArchiveFiles() override {}
	void         postProcessLoad() override {}
	void         init() override {}
	void         reset() override {}
	void         update() override {}
	void         closeAllFiles() override {}
	Bool         loadBigFilesFromDirectory(AsciiString, AsciiString, Bool, Bool) override { return FALSE; }
	void         loadIntoDirectoryTreePublic(ArchiveFile *af, Bool ow) { loadIntoDirectoryTree(af, ow); }
};

// RAII: restore the global FileSystem subsystem pointers even when a failing REQUIRE throws, so
// this case never leaks its stubs into the sibling cases above.
struct FileSystemGlobalsGuard
{
	LocalFileSystem *prevLocal;
	ArchiveFileSystem *prevArchive;
	FileSystemGlobalsGuard() : prevLocal(TheLocalFileSystem), prevArchive(TheArchiveFileSystem) {}
	~FileSystemGlobalsGuard()
	{
		TheLocalFileSystem = prevLocal;
		TheArchiveFileSystem = prevArchive;
	}
};

TEST_CASE("FileSystem::openFile: loose mod fallback root wins over archive-provided file")
{
	// Given: cwd has no Data/ subtree; a mod fallback root (registered via setAssetFallbackPaths,
	// exactly what loadMods() wires on device) holds the loose file with LOOSE_MOD content; and a
	// minimal archive substitute ALSO offers the same logical path (a mod .big / retail archive
	// that would serve MOD_BIG).
	ensureMemInit();
	AndroidFallbackWorkspace ws;
	ws.prevCwd = std::filesystem::current_path();
	ws.cleanCwd = uniqueTempDir("cwd_prio");
	ws.modRoot = uniqueTempDir("mod_prio");
	writeFile(ws.modRoot / "Data" / "INI" / "Locomotor.ini", "LOOSE_MOD");
	std::filesystem::current_path(ws.cleanCwd);

	StdLocalFileSystem fsdev;
	std::vector<AsciiString> modPaths;
	modPaths.push_back(AsciiString(ws.modRoot.string().c_str()));
	fsdev.setAssetFallbackPaths(modPaths);

	TestArchiveFile archive("MOD.big");
	ArchivedFileInfo info;
	info.m_archiveFilename = "MOD.big";
	info.m_filename = "data/ini/locomotor.ini";
	archive.addFileDirect("data/ini/locomotor.ini", info);

	TestArchiveFileSystem afs;
	afs.loadIntoDirectoryTreePublic(&archive, FALSE);

	// Point the public resolver at both subsystems. Restored on scope exit by the guard.
	FileSystemGlobalsGuard gbg;
	TheLocalFileSystem = &fsdev;
	TheArchiveFileSystem = &afs;

	// Precondition: the archive genuinely offers the path (doesFileExist resolves it). This makes
	// the precedence assertion meaningful rather than trivially true of an absent archive.
	REQUIRE(TheArchiveFileSystem->doesFileExist("Data/INI/Locomotor.ini"));

	// When: the public seam is exercised.
	FileSystem fs;
	File *f = fs.openFile("Data/INI/Locomotor.ini", File::READ);

	// Then: the loose mod file wins — LOOSE_MOD content is returned and the archive's openFile is
	// never consulted, because FileSystem::openFile short-circuits on a non-null local result.
	REQUIRE(f != nullptr);
	char buf[32] = {0};
	Int n = f->read(buf, static_cast<Int>(sizeof(buf) - 1));
	if (n < 0) n = 0;
	if (n > static_cast<Int>(sizeof(buf) - 1)) n = static_cast<Int>(sizeof(buf) - 1);
	buf[n] = '\0';
	f->close();
	REQUIRE(std::string(buf) == "LOOSE_MOD");
	REQUIRE_FALSE(archive.openFileCalled);
}
