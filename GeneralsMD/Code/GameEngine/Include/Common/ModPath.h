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

// ModPath.h
// Normalizes a mod path selected by the Android ModPicker into an absolute
// GameData-rooted path before parseMod resolves it. Shared Common helper so the
// contract is testable independently of SDL3Main / ModPicker wiring.

#pragma once

#include "Common/AsciiString.h"

// GeneralsX @bugfix Claude 31/07/2026 Android persisted mod-path normalization.
// ModPicker writes "Mods/<name>" (relative to GameData); SDL3 injects it as -mod,
// but parseMod treats relative paths against the sandboxed config storage instead
// of GameData, leaving m_modDir empty. This helper resolves a relative path against
// gameDataPath so the caller can pass an absolute path into parseMod.
//
// Contract:
//   - empty rawPath        -> returned empty (unchanged)
//   - absolute rawPath     -> returned unchanged (POSIX '/', DOS '\\', or drive-colon)
//   - empty gameDataPath   -> rawPath preserved
//   - otherwise            -> "<base>/<raw>" joined with exactly one separator
AsciiString GeneralsX_NormalizeModPath(const AsciiString &rawPath, const AsciiString &gameDataPath);
