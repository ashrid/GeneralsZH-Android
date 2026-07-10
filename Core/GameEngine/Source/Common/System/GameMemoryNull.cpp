/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 TheSuperHackers
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "PreRTS.h"

// GeneralsX @bugfix BenderAI 24/02/2026 Phase 5 - malloc.h not on macOS
#ifdef __APPLE__
#include <stdlib.h>
#else
#include <malloc.h>
#endif

#include "Common/GameMemoryNull.h"
#include "Common/ArchiveFileSystem.h"

// GeneralsX @feature Claude 10/07/2026 Task 7 (D2): always-on RSS/fd telemetry for Android.
// Logs process RSS (getrusage ru_maxrss — bionic reports KB, same as POSIX/glibc),
// open fd count (/proc/self/fd), fd ceiling (getrlimit RLIMIT_NOFILE), and the DMA net
// budget/peak — every ~30s via a background thread. Serves the memory-footprint
// investigation (the iOS ~3GB question) and is a prerequisite for D6 (memory budget + LRU).
#if defined(__ANDROID__)
#include <sys/resource.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>
#include <atomic>
#include <android/log.h>
#define MEMORY_TELEMETRY_ENABLED 1
#define CNC_MEMORY_BUDGET_BYTES (512LL * 1024 * 1024) // 512 MB default budget
static std::atomic<long> theCurrentBudgetBytes(0);
// GeneralsX @bugfix Claude 10/07/2026 Task 11 Oracle review: exhausted flag prevents
// eviction storm — archives use global new/delete (not DMA), so evicting them does
// not decrement theCurrentBudgetBytes. Without this guard, every DMA allocation over
// budget would re-trigger eviction forever. Hysteresis: re-arm at 80% of threshold.
static std::atomic<bool> theEvictionExhausted(false);
static std::atomic<long> thePeakBudgetBytes(0);

static void sampleProcessTelemetry()
{
	struct rusage ru;
	getrusage(RUSAGE_SELF, &ru);
	long rss = ru.ru_maxrss;  // bionic: KB (same as POSIX/glibc — the plan's "bytes" note was wrong)

	long fdCount = 0;
	DIR *d = opendir("/proc/self/fd");
	if (d != nullptr) { struct dirent *e; while ((e = readdir(d)) != nullptr) if (e->d_name[0] != '.') ++fdCount; closedir(d); }

	struct rlimit rl;
	getrlimit(RLIMIT_NOFILE, &rl);

	long budget = theCurrentBudgetBytes.load();
	long peak = thePeakBudgetBytes.load();
	__android_log_print(ANDROID_LOG_INFO, "GeneralsX",
		"TELEMETRY: RSS=%ld KB (%.1f MB)  fds=%ld/%ld  DMA budget=%ld B (%.1f MB)  peak=%ld B (%.1f MB)",
		rss, rss / 1024.0, fdCount, (long)rl.rlim_cur, budget, budget / 1048576.0, peak, peak / 1048576.0);
}

static void *telemetrySamplerThread(void *)
{
	for (;;) { sleep(30); sampleProcessTelemetry(); }
	return nullptr;
}
#endif

static Bool theMainInitFlag = false;

// ----------------------------------------------------------------------------
// PUBLIC DATA
// ----------------------------------------------------------------------------

MemoryPoolFactory *TheMemoryPoolFactory = nullptr;
DynamicMemoryAllocator *TheDynamicMemoryAllocator = nullptr;

//-----------------------------------------------------------------------------
// METHODS for DynamicMemoryAllocator
//-----------------------------------------------------------------------------

/**
	allocate a chunk-o-bytes from this DMA and return it, but don't bother zeroing
	out the block. if unable to allocate, throw ERROR_OUT_OF_MEMORY. this
	function will never return null.

  added code to make sure we're on a DWord boundary, throw exception if not
*/
void *DynamicMemoryAllocator::allocateBytesDoNotZeroImplementation(Int numBytes)
{
	void *p = malloc(numBytes);
	if (p == nullptr)
		throw ERROR_OUT_OF_MEMORY;
#if defined(__ANDROID__)
	// GeneralsX @feature Claude 10/07/2026 Task 7: track net DMA budget + peak.
	long cur = (theCurrentBudgetBytes += (long)numBytes);
	long prevPeak = thePeakBudgetBytes.load();
	while (cur > prevPeak && !thePeakBudgetBytes.compare_exchange_weak(prevPeak, cur)) {}
	// GeneralsX @feature Claude 10/07/2026 Task 11 (D6): evict first mod archive if budget exceeded.
	// Skipped once exhausted (nothing left to evict); re-arms via hysteresis at 80% threshold.
	if (cur > CNC_MEMORY_BUDGET_BYTES && !theEvictionExhausted.load() && TheArchiveFileSystem != nullptr)
	{
		Bool evicted = TheArchiveFileSystem->evictColdestModArchive();
		if (!evicted)
			theEvictionExhausted.store(true);
	}
	else if (cur < (CNC_MEMORY_BUDGET_BYTES * 4 / 5) && theEvictionExhausted.load())
	{
		theEvictionExhausted.store(false);
	}
#endif
	return p;
}

/**
	allocate a chunk-o-bytes from this DMA and return it, and zero out the contents first.
	if unable to allocate, throw ERROR_OUT_OF_MEMORY.
	this function will never return null.
*/
void *DynamicMemoryAllocator::allocateBytesImplementation(Int numBytes)
{
	void* p = allocateBytesDoNotZeroImplementation(numBytes);	// throws on failure
	memset(p, 0, numBytes);
	return p;
}

/**
	free a chunk-o-bytes allocated by this dma. it's ok to pass null.
*/
void DynamicMemoryAllocator::freeBytes(void* pBlockPtr)
{
#if defined(__ANDROID__)
	// GeneralsX @feature Claude 10/07/2026 Task 7: decrement budget. libc free() takes no
	// size, so use malloc_usable_size (bionic) to recover the block's allocated size.
	if (pBlockPtr != nullptr)
		theCurrentBudgetBytes -= (long)malloc_usable_size(pBlockPtr);
#endif
	free(pBlockPtr);
}

Int DynamicMemoryAllocator::getActualAllocationSize(Int numBytes)
{
	return numBytes;
}

#ifdef MEMORYPOOL_DEBUG
void DynamicMemoryAllocator::debugIgnoreLeaksForThisBlock(void* pBlockPtr)
{
}
#endif

//-----------------------------------------------------------------------------
// METHODS for MemoryPoolFactory
//-----------------------------------------------------------------------------

void MemoryPoolFactory::memoryPoolUsageReport( const char* filename, FILE *appendToFileInstead )
{
}

#ifdef MEMORYPOOL_DEBUG
void MemoryPoolFactory::debugMemoryReport(Int flags, Int startCheckpoint, Int endCheckpoint, FILE *fp )
{
}
void MemoryPoolFactory::debugSetInitFillerIndex(Int index)
{
}
#endif

//-----------------------------------------------------------------------------
// GLOBAL FUNCTIONS
//-----------------------------------------------------------------------------

/**
	Initialize the memory manager, and create TheMemoryPoolFactory and TheDynamicMemoryAllocator.
*/
void initMemoryManager()
{
	if (TheMemoryPoolFactory == nullptr && TheDynamicMemoryAllocator == nullptr)
	{
		TheMemoryPoolFactory = new (malloc(sizeof(MemoryPoolFactory))) MemoryPoolFactory;
		TheDynamicMemoryAllocator = new (malloc(sizeof(DynamicMemoryAllocator))) DynamicMemoryAllocator;

		DEBUG_INIT(DEBUG_FLAGS_DEFAULT);
		DEBUG_LOG(("*** Initialized the Null Memory Manager"));
#if defined(__ANDROID__)
		// GeneralsX @feature Claude 10/07/2026 Task 7: start the telemetry sampler
		// (detached; logs RSS/fd/budget every ~30s) + an initial sample.
		pthread_t _telemetryThread;
		pthread_create(&_telemetryThread, nullptr, telemetrySamplerThread, nullptr);
		pthread_detach(_telemetryThread);
		sampleProcessTelemetry();
#endif
	}
	else
	{
			DEBUG_CRASH(("Null Memory Manager is already initialized"));
	}

	theMainInitFlag = true;
}

//-----------------------------------------------------------------------------
Bool isMemoryManagerOfficiallyInited()
{
	return theMainInitFlag;
}

//-----------------------------------------------------------------------------
/**
	shutdown the memory manager and discard all memory. Note: if preMainInitMemoryManager()
	was called prior to initMemoryManager(), this call will do nothing.
*/
void shutdownMemoryManager()
{
	if (TheDynamicMemoryAllocator != nullptr)
	{
		TheDynamicMemoryAllocator->~DynamicMemoryAllocator();
		free((void *)TheDynamicMemoryAllocator);
		TheDynamicMemoryAllocator = nullptr;
	}

	if (TheMemoryPoolFactory != nullptr)
	{
		TheMemoryPoolFactory->~MemoryPoolFactory();
		free((void *)TheMemoryPoolFactory);
		TheMemoryPoolFactory = nullptr;
	}

	theMainInitFlag = false;

	DEBUG_SHUTDOWN();
}


#ifndef DISABLE_GAMEMEMORY_NEW_OPERATORS

extern void * __cdecl operator new(size_t size)
{
	void *p = malloc(size);
	if (p == nullptr)
		throw ERROR_OUT_OF_MEMORY;
	memset(p, 0, size);
	return p;
}

extern void __cdecl operator delete(void *p)
{
	free(p);
}

extern void * __cdecl operator new[](size_t size)
{
	void *p = malloc(size);
	if (p == nullptr)
		throw ERROR_OUT_OF_MEMORY;
	memset(p, 0, size);
	return p;
}

extern void __cdecl operator delete[](void *p)
{
	free(p);
}

// additional overloads to account for VC/MFC funky versions
extern void* __cdecl operator new(size_t size, const char *, int)
{
	void *p = malloc(size);
	if (p == nullptr)
		throw ERROR_OUT_OF_MEMORY;
	memset(p, 0, size);
	return p;
}

extern void __cdecl operator delete(void *p, const char *, int)
{
	free(p);
}

extern void* __cdecl operator new[](size_t size, const char *, int)
{
	void *p = malloc(size);
	if (p == nullptr)
		throw ERROR_OUT_OF_MEMORY;
	memset(p, 0, size);
	return p;
}

extern void __cdecl operator delete[](void *p, const char *, int)
{
	free(p);
}

#endif
