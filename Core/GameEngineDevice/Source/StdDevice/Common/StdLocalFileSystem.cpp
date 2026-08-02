/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

///////// StdLocalFileSystem.cpp /////////////////////////
// Stephan Vedder, April 2025
////////////////////////////////////////////////////////////

#include "Common/AsciiString.h"
#include "Common/GameMemory.h"
#include "Common/PerfTimer.h"
#include "StdDevice/Common/StdLocalFileSystem.h"
#include "StdDevice/Common/StdLocalFile.h"

#include <algorithm>
#include <filesystem>

#if defined(__ANDROID__)
// GeneralsX @bugfix Claude 31/07/2026 Android loose-file fallback uses POSIX fixed-buffer APIs
// only (snprintf/stat) — std::filesystem::path::operator/ and parent_path() allocate
// (basic_string::append) and corrupt the Scudo heap on device.
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <limits.h>
#include <strings.h>
#include <sys/stat.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#endif

#ifndef _WIN32
// GeneralsX @bugfix felipebraz 23/03/2026 Asset root fallback path for loose file lookups.
// On Linux/macOS the game binary's cwd and the data directory (asset root, CNC_GENERALS_ZH_PATH) are separate.
// The StdBIGFileSystem sets this after resolving the primary asset directory so that relative paths like
// "Data\Scripts\SkirmishScripts.scb" can be found in the asset root when the cwd lookup fails.
static std::filesystem::path s_assetFallbackPath;
// GeneralsX @feature Claude 10/07/2026 Task 13 (D8a): additional fallback paths for mod
// loose files. Checked after s_assetFallbackPath (primary asset root) and before archives.
static std::vector<std::filesystem::path> s_assetFallbackPaths;
#endif

#if defined(__ANDROID__)
// GeneralsX @bugfix Claude 31/07/2026 Allocation-free loose-file fallback probe. std::filesystem
// path joins/exists allocate (basic_string::append) and corrupt the Scudo heap on device, so
// probe with snprintf + POSIX stat() and copy the match into outBuf — the caller then constructs
// exactly one std::filesystem::path (pre-fix allocation profile). File::WRITE passes when the
// parent directory exists.
static bool androidLooseFileHit(const char *root, const char *rel, Int access,
								char *outBuf, size_t outSize)
{
	if (root == nullptr || root[0] == '\0' || rel == nullptr || rel[0] == '\0')
		return false;

	const size_t rootLen = std::strlen(root);
	const char *sep = (root[rootLen - 1] == '/') ? "" : "/";
	char probe[PATH_MAX];
	const int len = std::snprintf(probe, sizeof(probe), "%s%s%s", root, sep, rel);
	if (len < 0 || static_cast<size_t>(len) >= sizeof(probe))
		return false;

	struct stat st;
	if (::stat(probe, &st) == 0) {
		std::strncpy(outBuf, probe, outSize - 1);
		outBuf[outSize - 1] = '\0';
		return true;
	}

	if (access & File::WRITE) {
		char *slash = std::strrchr(probe, '/');
		if (slash != nullptr && slash != probe) {
			*slash = '\0';
			const bool parentExists = (::stat(probe, &st) == 0);
			*slash = '/';
			if (parentExists) {
				std::strncpy(outBuf, probe, outSize - 1);
				outBuf[outSize - 1] = '\0';
				return true;
			}
		}
	}
	return false;
}

// GeneralsX @bugfix Claude 02/08/2026 Android loose-file DIRECTORY listing. Single-file lookups
// (doesFileExist/openFile/getFileInfo) already probe the asset root + mod roots via
// androidLooseFileHit, but getFileListInDirectory ran a raw cwd-relative directory_iterator, so
// loose mod subdirectory INIs (Data/INI/ParticleSystem/*.ini, Data/INI/FXList/*.ini) were
// invisible to INI::loadFileDirectory -> loadDirectory -> getFileListInDirectory. A mod's new
// particle/FX definitions therefore never registered and its VFX silently vanished (release
// builds store a null template in parseParticleSystemTemplate). Fixed buffers + POSIX
// opendir/readdir/stat only, matching the allocation-free fallback contract above. Mirrors the
// single-file precedence: asset root first, then each registered mod root.
static void androidLooseDirListing(const char *root, const char *relDir, const char *searchExt,
								   FilenameList &filenameList, Bool searchSubdirectories)
{
	if (root == nullptr || root[0] == '\0' || relDir == nullptr || relDir[0] == '\0')
		return;

	char probe[PATH_MAX];
	const int len = std::snprintf(probe, sizeof(probe), "%s/%s", root, relDir);
	if (len < 0 || static_cast<size_t>(len) >= sizeof(probe))
		return;

	DIR *dir = ::opendir(probe);
	if (dir == nullptr)
		return;

	struct dirent *ent;
	while ((ent = ::readdir(dir)) != nullptr)
	{
		if (std::strcmp(ent->d_name, ".") == 0 || std::strcmp(ent->d_name, "..") == 0)
			continue;

		char full[PATH_MAX];
		const int flen = std::snprintf(full, sizeof(full), "%s/%s", probe, ent->d_name);
		if (flen < 0 || static_cast<size_t>(flen) >= sizeof(full))
			continue;

		struct stat st;
		if (::stat(full, &st) != 0)
			continue;

		if (S_ISDIR(st.st_mode))
		{
			if (searchSubdirectories)
			{
				char subRel[PATH_MAX];
				const int slen = std::snprintf(subRel, sizeof(subRel), "%s/%s", relDir, ent->d_name);
				if (slen < 0 || static_cast<size_t>(slen) >= sizeof(subRel))
					continue;
				androidLooseDirListing(root, subRel, searchExt, filenameList, searchSubdirectories);
			}
			continue;
		}

		if (searchExt != nullptr && searchExt[0] != '\0')
		{
			const char *dot = std::strrchr(ent->d_name, '.');
			if (dot == nullptr || strcasecmp(dot, searchExt) != 0)
				continue;
		}

		// Emit the logical path (relative to the probed root) so INI::loadDirectory strips the
		// directory prefix the same way it does for archive-sourced entries.
		char logical[PATH_MAX];
		const int llen = std::snprintf(logical, sizeof(logical), "%s/%s", relDir, ent->d_name);
		if (llen < 0 || static_cast<size_t>(llen) >= sizeof(logical))
			continue;
		filenameList.insert(AsciiString(logical));
	}

	::closedir(dir);
}
#endif

StdLocalFileSystem::StdLocalFileSystem() : LocalFileSystem()
{
}

StdLocalFileSystem::~StdLocalFileSystem() {
}

//DECLARE_PERF_TIMER(StdLocalFileSystem_openFile)
static std::filesystem::path fixFilenameFromWindowsPath(const Char *filename, Int access)
{
	std::string fixedFilename(filename);

#ifndef _WIN32
	// Replace backslashes with forward slashes on unix
	std::replace(fixedFilename.begin(), fixedFilename.end(), '\\', '/');
#endif

	// Convert the filename to a std::filesystem::path and pass that
	std::filesystem::path path(std::move(fixedFilename));

#if defined(__ANDROID__)
	// GeneralsX @bugfix Claude 10/07/2026 On Android, skip the case-insensitive std::filesystem
	// resolution below. It causes heap corruption (Scudo "corrupted chunk header" during the
	// std::filesystem::operator/ + directory_iterator path) — confirmed via heap probes proving
	// the heap is clean before this function, and the crash vanishing when this early-return is
	// added (engine then completes full init + creates D3D device + enters execute()). Android is
	// case-sensitive, and .big archive lookups go through ArchiveFileSystem (which has its own
	// case handling), so the loose-file case-insensitive traversal is not needed here.
	// GeneralsX @bugfix Claude 31/07/2026 The early-return also skipped the fallback roots, so
	// loadMods()-registered loose mod files could not override retail archives on device. Probe
	// s_assetFallbackPath (primary) then s_assetFallbackPaths (mod roots) via androidLooseFileHit
	// — fixed buffers + stat(), no std::filesystem joins/exists (heap corruptor). Precedence is
	// preserved; a hit yields exactly one std::filesystem::path, otherwise the original path is
	// returned as before.
	char relBuf[PATH_MAX];
	size_t ri = 0;
	for (; filename[ri] != '\0' && ri < sizeof(relBuf) - 1; ++ri)
		relBuf[ri] = (filename[ri] == '\\') ? '/' : filename[ri];
	relBuf[ri] = '\0';

	if (relBuf[0] != '\0' && relBuf[0] != '/') {
		char match[PATH_MAX];
		bool hit = androidLooseFileHit(s_assetFallbackPath.c_str(), relBuf, access,
									   match, sizeof(match));
		for (const auto &fb : s_assetFallbackPaths) {
			if (hit) break;
			hit = androidLooseFileHit(fb.c_str(), relBuf, access, match, sizeof(match));
		}
		if (hit) {
			return std::filesystem::path(match);
		}
	}
	return path;
#endif

#ifndef _WIN32
	// check if the file exists to see if fixup is required
	// if it's not found try to match disregarding case sensitivity
	// For cases where a write is happening, we should check if the parent path exists, if so, let it through, since the file may not exist yet.
	std::error_code ec;
	if (!std::filesystem::exists(path, ec) &&
		((!(access & File::WRITE)) || ((access & File::WRITE) && !std::filesystem::exists(path.parent_path(), ec))))
	{
		// GeneralsX @bugfix felipebraz 23/03/2026 Before attempting expensive case-insensitive cwd traversal,
		// check if the relative path resolves directly from the asset root (e.g. CNC_GENERALS_ZH_PATH).
		// On Windows cwd == install dir so this is never needed; on Linux/macOS they are separate.
		if (!s_assetFallbackPath.empty() && path.is_relative()) {
			std::filesystem::path assetRootPath = s_assetFallbackPath / path;
			std::error_code ecAsset;
			const bool writeAndParentExists = (access & File::WRITE) && std::filesystem::exists(assetRootPath.parent_path(), ecAsset);
			if (std::filesystem::exists(assetRootPath, ecAsset) || writeAndParentExists) {
				return assetRootPath;
			}

			#ifdef __linux__
			// GeneralsX @bugfix BenderAI 11/05/2026 Linux: resolve case-insensitive paths from asset root.
			std::filesystem::path assetRootFixed = s_assetFallbackPath;
			std::filesystem::path assetRootCurrent = s_assetFallbackPath;
			bool assetRootFound = true;
			for (const auto& p : path)
			{
				std::filesystem::path pathFixedPart;
				std::error_code ecAssetCase;
				if (std::filesystem::exists(assetRootCurrent / p, ecAssetCase))
				{
					pathFixedPart = p;
				}
				else
				{
					for (auto& entry : std::filesystem::directory_iterator(assetRootCurrent, ecAssetCase))
					{
						if (strcasecmp(entry.path().filename().string().c_str(), p.string().c_str()) == 0)
						{
							pathFixedPart = entry.path().filename();
							break;
						}
					}
				}

				if (pathFixedPart.empty())
				{
					assetRootFound = false;
					break;
				}

				assetRootFixed /= pathFixedPart;
				assetRootCurrent /= pathFixedPart;
			}

			if (assetRootFound)
			{
				std::error_code ecAssetFixed;
				const bool writeAndParentExistsFixed = (access & File::WRITE)
					&& std::filesystem::exists(assetRootFixed.parent_path(), ecAssetFixed);
				if (std::filesystem::exists(assetRootFixed, ecAssetFixed) || writeAndParentExistsFixed)
				{
					return assetRootFixed;
				}
			}
			#endif
		}

		// GeneralsX @feature Claude 10/07/2026 Task 13 (D8a): check mod-directory fallback paths.
		if (path.is_relative()) {
			for (const auto& fb : s_assetFallbackPaths) {
				std::filesystem::path modPath = fb / path;
				std::error_code ecMod;
				const bool modWriteParent = (access & File::WRITE) && std::filesystem::exists(modPath.parent_path(), ecMod);
				if (std::filesystem::exists(modPath, ecMod) || modWriteParent) {
					return modPath;
				}
			}
		}
		// Traverse path to try and match case-insensitively
		std::filesystem::path parent = path.parent_path();

		std::filesystem::path pathFixed;
		std::filesystem::path pathCurrent;
		// GeneralsX @build felipebraz 20/06/2025 const auto& required because libc++ std::filesystem::path iterator yields temporaries (non-const lvalue reference would fail on Apple clang)
		for (const auto& p : path)
		{
			std::filesystem::path pathFixedPart;
			if (pathCurrent.empty())
			{
				// Load the first part of the path
				pathFixed /= p;
				pathCurrent /= p;
				continue;
			}

			if (std::filesystem::exists(pathCurrent / p, ec))
			{
				pathFixedPart = p;
			}
			else if (std::filesystem::exists(pathFixed / p, ec))
			{
				pathFixedPart = p;
			}
			else
			{
				// Check if the subpath exists using case-insensitive comparison
				for (auto& entry : std::filesystem::directory_iterator(pathFixed, ec))
				{
					if (strcasecmp(entry.path().filename().string().c_str(), p.string().c_str()) == 0)
					{
						pathFixedPart = entry.path().filename();
						break;
					}
				}
			}

			if (pathFixedPart.empty())
			{
				// Required to allow creation of new files
				if (!(access & File::WRITE))
				{
					DEBUG_LOG(("StdLocalFileSystem::fixFilenameFromWindowsPath - Error finding file %s", filename.string().c_str()));
					DEBUG_LOG(("StdLocalFileSystem::fixFilenameFromWindowsPath - Got so far %s", pathCurrent.string().c_str()));

					return std::filesystem::path();
				}

				// Use the last known good path
				pathFixed = p;
			}

			// Copy of the current path to mirror the current depth
			pathFixed /= pathFixedPart;
			pathCurrent /= p;
		}
		path = pathFixed;
	}
#endif

	return path;
}

File * StdLocalFileSystem::openFile(const Char *filename, Int access, size_t bufferSize)
{
	//USE_PERF_TIMER(StdLocalFileSystem_openFile)

	// sanity check
	if (strlen(filename) <= 0) {
		return nullptr;
	}

	std::filesystem::path path = fixFilenameFromWindowsPath(filename, access);

	if (path.empty()) {
		return nullptr;
	}

	if (access & File::WRITE) {
		// if opening the file for writing, we need to make sure the directory is there
		// before we try to create the file.
		std::filesystem::path dir = path.parent_path();
		std::error_code ec;
		if (!std::filesystem::exists(dir, ec) || ec) {
			if(!std::filesystem::create_directories(dir, ec) || ec) {
				DEBUG_LOG(("StdLocalFileSystem::openFile - Error creating directory %s", dir.string().c_str()));
				return nullptr;
			}
		}
	}

	StdLocalFile *file = newInstance( StdLocalFile );

	if (file->open(path.string().c_str(), access, bufferSize) == FALSE) {
		deleteInstance(file);
		file = nullptr;
	} else {
		file->deleteOnClose();
	}

// this will also need to play nice with the STREAMING type that I added, if we ever enable this

// srj sez: this speeds up INI loading, but makes BIG files unusable.
// don't enable it without further tweaking.
//
// unless you like running really slowly.
//	if (!(access&File::WRITE)) {
//		// Return a ramfile.
//		RAMFile *ramFile = newInstance( RAMFile );
//		if (ramFile->open(file)) {
//			file->close(); // is deleteonclose, so should delete.
//			ramFile->deleteOnClose();
//			return ramFile;
//		}	else {
//			ramFile->close();
//			deleteInstance(ramFile);
//		}
//	}

	return file;
}

void StdLocalFileSystem::update()
{
}

void StdLocalFileSystem::init()
{
}

void StdLocalFileSystem::reset()
{
}

//DECLARE_PERF_TIMER(StdLocalFileSystem_doesFileExist)
Bool StdLocalFileSystem::doesFileExist(const Char *filename) const
{
	std::filesystem::path path = fixFilenameFromWindowsPath(filename, 0);
	if(path.empty()) {
		return FALSE;
	}

	std::error_code ec;
	return std::filesystem::exists(path, ec);
}

void StdLocalFileSystem::getFileListInDirectory(const AsciiString& currentDirectory, const AsciiString& originalDirectory, const AsciiString& searchName, FilenameList & filenameList, Bool searchSubdirectories) const
{

	AsciiString asciisearch;
	asciisearch = originalDirectory;
	asciisearch.concat(currentDirectory);
	auto searchExt = std::filesystem::path(searchName.str()).extension();
	if (asciisearch.isEmpty()) {
		asciisearch = ".";
	}

	std::string fixedDirectory(asciisearch.str());

#ifndef _WIN32
	// Replace backslashes with forward slashes on unix
	std::replace(fixedDirectory.begin(), fixedDirectory.end(), '\\', '/');
#endif

#if defined(__ANDROID__)
	// GeneralsX @bugfix Claude 02/08/2026 Probe the loose-file fallback roots (primary asset
	// root then registered mod roots) for directory listings, mirroring the single-file
	// precedence in fixFilenameFromWindowsPath. Without this, INI::loadFileDirectory ->
	// loadDirectory -> getFileListInDirectory could not see loose mod subdirectory INIs
	// (Data/INI/ParticleSystem/*.ini, Data/INI/FXList/*.ini) and mod VFX definitions never
	// registered. The cwd-relative directory_iterator below is then still attempted (it is a
	// no-op when the directory is not in cwd).
	{
		std::string searchExtStr = searchExt.string();
		if (!s_assetFallbackPath.empty())
		{
			androidLooseDirListing(s_assetFallbackPath.c_str(), fixedDirectory.c_str(),
								   searchExtStr.c_str(), filenameList, searchSubdirectories);
		}
		for (const auto &fb : s_assetFallbackPaths)
		{
			androidLooseDirListing(fb.c_str(), fixedDirectory.c_str(),
								   searchExtStr.c_str(), filenameList, searchSubdirectories);
		}
	}
#endif

	Bool done = FALSE;
	std::error_code ec;

	auto iter = std::filesystem::directory_iterator(fixedDirectory.c_str(), ec);
	// The default iterator constructor creates an end iterator
	done = iter == std::filesystem::directory_iterator();

	if (ec) {
		DEBUG_LOG(("StdLocalFileSystem::getFileListInDirectory - Error opening directory %s", fixedDirectory.c_str()));
		return;
	}

	while (!done)	{
		std::string filenameStr = iter->path().filename().string();
		if (!iter->is_directory() && iter->path().extension() == searchExt &&
			(strcmp(filenameStr.c_str(), ".") != 0 && strcmp(filenameStr.c_str(), "..") != 0)) {
			// if we haven't already, add this filename to the list.
			// a stl set should only allow one copy of each filename
			AsciiString newFilename = iter->path().string().c_str();
			if (filenameList.find(newFilename) == filenameList.end()) {
				filenameList.insert(newFilename);
			}
		}

		iter++;
		done = iter == std::filesystem::directory_iterator();
	}

	if (searchSubdirectories) {
		auto iter = std::filesystem::directory_iterator(fixedDirectory, ec);

		if (ec) {
			DEBUG_LOG(("StdLocalFileSystem::getFileListInDirectory - Error opening subdirectory %s", fixedDirectory.c_str()));
			return;
		}

		// The default iterator constructor creates an end iterator
		done = iter == std::filesystem::directory_iterator();

		while (!done) {
			std::string filenameStr = iter->path().filename().string();
			if(iter->is_directory() &&
				(strcmp(filenameStr.c_str(), ".") != 0 && strcmp(filenameStr.c_str(), "..") != 0)) {
				AsciiString tempsearchstr(filenameStr.c_str());

				// recursively add files in subdirectories if required.
				getFileListInDirectory(tempsearchstr, originalDirectory, searchName, filenameList, searchSubdirectories);
			}

			iter++;
			done = iter == std::filesystem::directory_iterator();
		}
	}
}

Bool StdLocalFileSystem::getFileInfo(const AsciiString& filename, FileInfo *fileInfo) const
{
	std::filesystem::path path = fixFilenameFromWindowsPath(filename.str(), 0);

	if(path.empty()) {
		return FALSE;
	}

	std::error_code ec;
	auto file_size = std::filesystem::file_size(path, ec);
	if (ec)
	{
		return FALSE;
	}

	auto write_time = std::filesystem::last_write_time(path, ec);
	if (ec)
	{
		return FALSE;
	}

	// TODO: fix this to be win compatible (time since 1601)
	auto time = write_time.time_since_epoch().count();
	fileInfo->timestampHigh = time >> 32;
	fileInfo->timestampLow = time & UINT32_MAX;
	fileInfo->sizeHigh      = file_size >> 32;
	fileInfo->sizeLow  = file_size & UINT32_MAX;

	return TRUE;
}

Bool StdLocalFileSystem::createDirectory(AsciiString directory)
{
	bool result = FALSE;

	std::string fixedDirectory(directory.str());

#ifndef _WIN32
	// Replace backslashes with forward slashes on unix
	std::replace(fixedDirectory.begin(), fixedDirectory.end(), '\\', '/');
#endif

	if ((!fixedDirectory.empty()) && (fixedDirectory.length() < _MAX_DIR)) {
		// Convert to host path
		std::filesystem::path path(std::move(fixedDirectory));

		std::error_code ec;
		result = std::filesystem::create_directory(path, ec);
		if (ec) {
			result = FALSE;
		}
	}
	return result;
}

AsciiString StdLocalFileSystem::normalizePath(const AsciiString& filePath) const
{
	std::string nonNormalized(filePath.str());
#ifndef _WIN32
	// Replace backslashes with forward slashes on non-Windows platforms
	// GeneralsX @bugfix BenderAI 13/02/2026 Fixed typo: unNormalized → nonNormalized
	std::replace(nonNormalized.begin(), nonNormalized.end(), '\\', '/');
#endif
	std::filesystem::path pathNonNormalized(nonNormalized);
	return AsciiString(pathNonNormalized.lexically_normal().string().c_str());
}

#ifndef _WIN32
// GeneralsX @bugfix felipebraz 23/03/2026 Receive the asset root path from StdBIGFileSystem after it resolves
// CNC_GENERALS_ZH_PATH. Used as a fallback in fixFilenameFromWindowsPath so that loose data files
// (e.g. Data\Scripts\SkirmishScripts.scb) can be found even when cwd != asset root directory.
void StdLocalFileSystem::setAssetRootPath(const AsciiString& path)
{
	std::string p(path.str());
	std::replace(p.begin(), p.end(), '\\', '/');
	s_assetFallbackPath = std::filesystem::path(std::move(p));
	DEBUG_LOG(("StdLocalFileSystem::setAssetRootPath - asset fallback path set to '%s'", s_assetFallbackPath.string().c_str()));
}

void StdLocalFileSystem::setAssetFallbackPaths(const std::vector<AsciiString>& paths)
{
	s_assetFallbackPaths.clear();
	for (const auto& p : paths)
	{
		std::string s(p.str());
		std::replace(s.begin(), s.end(), '\\', '/');
		// GeneralsX @security Claude 10/07/2026 Task 13 Oracle review: reject parent-traversal segments.
		if (s.find("/..") != std::string::npos || s.find("../") != std::string::npos || s == "..")
			continue;
		s_assetFallbackPaths.emplace_back(std::move(s));
	}
}

void StdLocalFileSystem::clearAssetFallbackPaths()
{
	s_assetFallbackPaths.clear();
}
#endif
