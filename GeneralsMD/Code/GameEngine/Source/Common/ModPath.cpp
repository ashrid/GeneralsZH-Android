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

// ModPath.cpp
// Implements GeneralsX_NormalizeModPath (see Common/ModPath.h for the contract).

#include "PreRTS.h"

#include "Common/ModPath.h"

// A path is absolute if it begins with a separator (POSIX '/' or DOS '\\') or
// uses a Windows drive-colon prefix (e.g. "C:"). Mirrors how the engine treats
// rooted paths so absolute Android Intent paths pass through untouched.
static Bool ModPathIsAbsolute(const char *path)
{
	if (path == nullptr)
		return FALSE;
	if (path[0] == '/' || path[0] == '\\')
		return TRUE;
	if (path[0] != '\0' && path[1] == ':')
		return TRUE;
	return FALSE;
}

// GeneralsX @bugfix Claude 31/07/2026 Android persisted mod-path normalization.
// Resolves a relative ModPicker path ("Mods/<name>") against gameDataPath so the
// caller hands parseMod an absolute path; parseMod's relative behavior targets
// sandboxed config storage and would otherwise leave m_modDir empty. Absolute
// and empty inputs are returned unchanged (see ModPath.h for the full contract).
AsciiString GeneralsX_NormalizeModPath(const AsciiString &rawPath, const AsciiString &gameDataPath)
{
	if (rawPath.isEmpty())
		return rawPath;

	if (ModPathIsAbsolute(rawPath.str()))
		return rawPath;

	if (gameDataPath.isEmpty())
		return rawPath;

	// Trim trailing separators off the base so a base like "/game-data/" joins as
	// "/game-data/Mods/Foo" (exactly one separator) rather than "/game-data//Mods/Foo".
	const char *base = gameDataPath.str();
	Int baseLen = gameDataPath.getLength();
	while (baseLen > 0 && (base[baseLen - 1] == '/' || base[baseLen - 1] == '\\'))
		--baseLen;

	AsciiString result(base, baseLen);
	result.concat('/');
	result.concat(rawPath);
	return result;
}
