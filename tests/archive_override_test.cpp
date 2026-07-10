// GeneralsX @feature Claude 10/07/2026 Task 8 (D1): adversarial ArchiveFileSystem tests.
// Locks the non-negotiable multimap override-precedence dance (ArchiveFileSystem.cpp:158-183):
//   overwrite=TRUE  -> newly-loaded archive wins (inserted first in the multimap range);
//   overwrite=FALSE -> first-loaded archive wins (appended at the end).
// getArchiveFile returns the first ArchiveFile* in the range (the winner).
// Uses stub ArchiveFile/ArchiveFileSystem subclasses — no real .big files, no full engine
// init beyond initMemoryManager (for AsciiString's pool allocator on the Linux host).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <Utility/CppMacros.h>

#include "PreRTS.h"

#include <doctest/doctest.h>

#include "Common/ArchiveFileSystem.h"
#include "Common/ArchiveFile.h"
#include "Common/GameMemory.h"

// Stubs for engine symbols referenced by z_gameengine but defined in the app Main layer.
// The test only exercises ArchiveFileSystem; these are no-ops.
HWND ApplicationHWnd = nullptr;
const Char *g_csfFile = "";
const Char *g_strFile = "";
void OSDisplaySetBusyState(Bool, Bool) {}
struct SDL_Window;  // forward declare (the test doesn't use SDL3 directly)
SDL_Window *TheSDL3Window = nullptr;
int __argc = 0;
char **__argv = nullptr;

// Stub ArchiveFile: no-op the pure virtuals; inherit addFile + getFileListInDirectory.
class TestArchiveFile : public ArchiveFile
{
	AsciiString m_name;
public:
	explicit TestArchiveFile(const char *name) : m_name(name) {}
	Bool      getFileInfo(const AsciiString &, FileInfo *) const override { return FALSE; }
	File *    openFile(const Char *, Int) override { return nullptr; }
	void      closeAllFiles() override {}
	AsciiString getName() override { return m_name; }
	AsciiString getPath() override { return m_name; }
	void      setSearchPriority(Int) override {}
	void      close() override {}
};

// Stub ArchiveFileSystem: no-op the pure virtuals; inherit loadIntoDirectoryTree + getArchiveFile.
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
	Bool         loadBigFilesFromDirectory(AsciiString, AsciiString, Bool) override { return FALSE; }
	// loadIntoDirectoryTree is protected in the base; expose it for the test.
	void         loadIntoDirectoryTreePublic(ArchiveFile *af, Bool ow) { loadIntoDirectoryTree(af, ow); }
};

static bool g_memInited = false;
static void ensureMemInit()
{
	if (!g_memInited)
	{
		initMemoryManager();
		g_memInited = true;
	}
}

TEST_CASE("overwrite=TRUE: newly-loaded archive wins (the override-precedence dance)")
{
	fprintf(stderr, "STEP: ensureMemInit\n");
	ensureMemInit();
	fprintf(stderr, "STEP: create afs\n");
	TestArchiveFileSystem afs;
	fprintf(stderr, "STEP: create archives\n");
	TestArchiveFile archiveA("A.big");
	TestArchiveFile archiveB("B.big");
	ArchivedFileInfo info;
	info.m_archiveFilename = "test";
	fprintf(stderr, "STEP: test nextToken isolation\n");
	{
		AsciiString ts = "Art\\foo.tga";
		AsciiString tt;
		ts.nextToken(&tt, "\\/");
		fprintf(stderr, "STEP: nextToken result: '%s'\n", tt.str());
	}
	fprintf(stderr, "STEP: test map<AsciiString,int> isolation\n");
	{
		std::map<AsciiString, int> tm;
		tm["Art"] = 1;
		auto it = tm.find("Art");
		fprintf(stderr, "STEP: map find: %d\n", it != tm.end() ? it->second : -1);
	}
	fprintf(stderr, "STEP: addFile A\n");
	archiveA.addFile("Art\\foo.tga", &info);
	fprintf(stderr, "STEP: addFile B\n");
	archiveB.addFile("Art\\foo.tga", &info);
	fprintf(stderr, "STEP: loadIntoDirectoryTree A\n");
	afs.loadIntoDirectoryTreePublic(&archiveA, FALSE);
	fprintf(stderr, "STEP: loadIntoDirectoryTree B\n");
	afs.loadIntoDirectoryTreePublic(&archiveB, TRUE);
	fprintf(stderr, "STEP: getArchiveFile\n");
	REQUIRE(afs.getArchiveFile("Art\\foo.tga") == &archiveB);
	fprintf(stderr, "STEP: done\n");
}

TEST_CASE("overwrite=FALSE: first-loaded archive wins")
{
	ensureMemInit();
	TestArchiveFileSystem afs;
	TestArchiveFile archiveA("A.big");
	TestArchiveFile archiveB("B.big");
	ArchivedFileInfo info;
	info.m_archiveFilename = "test";
	archiveA.addFile("Art\\foo.tga", &info);
	archiveB.addFile("Art\\foo.tga", &info);
	afs.loadIntoDirectoryTreePublic(&archiveA, FALSE);
	afs.loadIntoDirectoryTreePublic(&archiveB, FALSE);  // no overwrite -> A stays first
	REQUIRE(afs.getArchiveFile("Art\\foo.tga") == &archiveA);
}
