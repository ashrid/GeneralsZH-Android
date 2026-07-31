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

#pragma once

// GeneralsX @feature Claude 31/07/2026 Android-only archive-entry skip predicate for the
// approved mod-file hierarchy. During primary (retail) discovery it (a) excludes the scan
// root's top-level Mods/ subtree so inactive mods are never mounted, and (b) preserves the
// retail Data/INI/INIZH.big duplicate CRC-skip. The selected-mod sweep passes retailScan=FALSE
// and skips nothing, so a selected mod's own INIZH.big loads.
//
// Pure + dependency-free: deliberately no std::filesystem. On Android arm64, std::filesystem
// path joins/exists allocate (basic_string::append) and corrupt the Scudo heap — the same
// hazard androidLooseFileHit avoids with fixed buffers + POSIX stat(). This helper follows
// that precedent: fixed char[CAP] buffers + strncmp/strcmp only.
//
// The implementation is `inline` so it can live in a header consumed by both the production
// StdBIGFileSystem TU (Android build, -D__ANDROID__) and the test TU
// (tests/archive_filter_test.cpp, compiled -D__ANDROID__) without a multiple-definition link
// conflict. Off-Android the function is a no-op (the only call site is itself gated on
// __ANDROID__), so the host-built engine never instantiates it.
//
//   archivePath - a *.big path returned by the directory scan
//   scanRoot    - the `dir` passed to loadBigFilesFromDirectory (the GameData root for
//                 primary discovery, or m_modDir for the selected-mod sweep). May be "" or "."
//   retailScan  - TRUE = primary/retail discovery; FALSE = selected-mod sweep
// Returns TRUE iff the entry must be skipped.

#include <cstring>

inline bool GeneralsX_ShouldSkipArchiveEntry(const char* archivePath,
											 const char* scanRoot,
											 bool retailScan)
{
	if (archivePath == nullptr || archivePath[0] == '\0')
		return false;

#if defined(__ANDROID__)
	if (!retailScan)
		return false; // selected-mod sweep: the single selected mod loads in full

	static const unsigned int CAP = 4096;
	char path[CAP];
	char root[CAP];

	// Normalize: lowercase, '\' -> '/', collapse repeat/leading slashes, strip "./" segment
	// dots, strip trailing slash. Fixed-buffer, no allocation.
	const auto normalize = [](char* dst, const char* src) {
		if (src == nullptr) { dst[0] = '\0'; return; }
		unsigned int w = 0;
		bool prevSlash = true; // also strips a leading slash
		for (unsigned int r = 0; src[r] != '\0' && w + 1 < CAP; ++r) {
			char c = src[r];
			if (c == '\\') c = '/';
			if (c == '/') {
				if (prevSlash) continue;       // collapse repeats / drop leading
				prevSlash = true;
				dst[w++] = '/';
			} else {
				// Drop a single '.' segment ("./"): a '.' at a segment boundary followed by
				// '/' or end. Keeps real dots inside names like "mod.big" (prevSlash is false there).
				if (prevSlash && c == '.' &&
					(src[r + 1] == '/' || src[r + 1] == '\\' || src[r + 1] == '\0')) {
					continue; // the following slash (if any) is collapsed by prevSlash staying true
				}
				prevSlash = false;
				dst[w++] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
			}
		}
		if (w > 0 && dst[w - 1] == '/') --w; // strip trailing slash
		dst[w] = '\0';
	};

	normalize(path, archivePath);
	normalize(root, scanRoot != nullptr ? scanRoot : "");

	const unsigned int rootLen = static_cast<unsigned int>(std::strlen(root));
	const char* tail = path;
	if (rootLen > 0 && std::strncmp(path, root, rootLen) == 0 && path[rootLen] == '/') {
		tail = path + rootLen + 1; // strip the matched scan-root prefix
	}
	// If the root did not prefix-match (e.g. a relative return like "Mods/A.big" with an
	// absolute or empty root), tail stays the whole path — its first component is still tested.

	// Exclude only the scan root's TOP-LEVEL Mods/ subtree (component test, not substring),
	// so a coincidental "Mods" elsewhere (e.g. UserData/Mods/) is unaffected.
	if (std::strncmp(tail, "mods/", 5) == 0)
		return true;

	// Retail-only Data/INI/INIZH.big duplicate skip (CRC guard), preserved for retail discovery.
	static const char kInizh[] = "data/ini/inizh.big";
	const unsigned int plen = static_cast<unsigned int>(std::strlen(path));
	const unsigned int tlen = static_cast<unsigned int>(sizeof(kInizh) - 1);
	if (plen >= tlen && std::strcmp(path + plen - tlen, kInizh) == 0)
		return true;

	return false;
#else
	(void)scanRoot;
	return false; // non-Android: predicate is unused (the only call site is #if __ANDROID__)
#endif
}
