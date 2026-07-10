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
#include <vector>
#include <cstdio>

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
	// Bypass addFile (which builds a directory tree via
	// std::map<AsciiString, DetailedArchivedDirectoryInfo> — the 256-byte DMA pool
	// hangs on the Linux host). Populate m_files at the root directly (protected member).
	void      addFileDirect(const AsciiString &filename, const ArchivedFileInfo &info)
	{
		m_rootDirectory.m_files[filename] = info;
	}
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
	ensureMemInit();
	TestArchiveFileSystem afs;
	TestArchiveFile archiveA("A.big");
	TestArchiveFile archiveB("B.big");
	ArchivedFileInfo info;
	info.m_archiveFilename = "test";
	info.m_filename = "foo.tga";
	archiveA.addFileDirect("foo.tga", info);
	archiveB.addFileDirect("foo.tga", info);
	afs.loadIntoDirectoryTreePublic(&archiveA, FALSE);
	afs.loadIntoDirectoryTreePublic(&archiveB, TRUE);
	REQUIRE(afs.getArchiveFile("foo.tga") == &archiveB);
}

TEST_CASE("overwrite=FALSE: first-loaded archive wins")
{
	ensureMemInit();
	TestArchiveFileSystem afs;
	TestArchiveFile archiveA("A.big");
	TestArchiveFile archiveB("B.big");
	ArchivedFileInfo info;
	info.m_archiveFilename = "test";
	info.m_filename = "foo.tga";
	archiveA.addFileDirect("foo.tga", info);
	archiveB.addFileDirect("foo.tga", info);
	afs.loadIntoDirectoryTreePublic(&archiveA, FALSE);
	afs.loadIntoDirectoryTreePublic(&archiveB, FALSE);
	REQUIRE(afs.getArchiveFile("foo.tga") == &archiveA);
}

TEST_CASE("getArchiveFile: returns correct archive, nullptr for nonexistent")
{
	ensureMemInit();
	TestArchiveFileSystem afs;
	TestArchiveFile archiveA("A.big");
	ArchivedFileInfo info;
	info.m_archiveFilename = "test";
	info.m_filename = "foo.tga";
	archiveA.addFileDirect("foo.tga", info);
	afs.loadIntoDirectoryTreePublic(&archiveA, FALSE);
	REQUIRE(afs.getArchiveFile("foo.tga") == &archiveA);
	REQUIRE(afs.getArchiveFile("nonexistent.tga") == nullptr);
}

TEST_CASE("empty archive: loadIntoDirectoryTree with no files does not crash")
{
	ensureMemInit();
	TestArchiveFileSystem afs;
	TestArchiveFile emptyArchive("empty.big");
	afs.loadIntoDirectoryTreePublic(&emptyArchive, FALSE);
	REQUIRE(afs.getArchiveFile("anything.tga") == nullptr);
}

TEST_CASE("order-sensitivity: 3 overlapping archives, overwrite=FALSE keeps first, then overwrite flips")
{
	ensureMemInit();
	TestArchiveFileSystem afs;
	TestArchiveFile archiveA("A.big");
	TestArchiveFile archiveB("B.big");
	TestArchiveFile archiveC("C.big");
	ArchivedFileInfo info;
	info.m_archiveFilename = "test";
	info.m_filename = "foo.tga";
	archiveA.addFileDirect("foo.tga", info);
	archiveB.addFileDirect("foo.tga", info);
	archiveC.addFileDirect("foo.tga", info);
	afs.loadIntoDirectoryTreePublic(&archiveA, FALSE);
	afs.loadIntoDirectoryTreePublic(&archiveB, FALSE);
	afs.loadIntoDirectoryTreePublic(&archiveC, FALSE);
	REQUIRE(afs.getArchiveFile("foo.tga") == &archiveA);
	afs.loadIntoDirectoryTreePublic(&archiveC, TRUE);
	REQUIRE(afs.getArchiveFile("foo.tga") == &archiveC);
}

TEST_CASE("stress: 1000 archives with same file, no crash, first wins")
{
	ensureMemInit();
	TestArchiveFileSystem afs;
	ArchivedFileInfo info;
	info.m_archiveFilename = "test";
	info.m_filename = "foo.tga";
	std::vector<TestArchiveFile> archives;
	archives.reserve(1000);
	for (int i = 0; i < 1000; ++i)
	{
		char name[16];
		snprintf(name, sizeof(name), "%d.big", i);
		archives.emplace_back(name);
		archives.back().addFileDirect("foo.tga", info);
		afs.loadIntoDirectoryTreePublic(&archives.back(), FALSE);
	}
	REQUIRE(afs.getArchiveFile("foo.tga") == &archives[0]);
}
