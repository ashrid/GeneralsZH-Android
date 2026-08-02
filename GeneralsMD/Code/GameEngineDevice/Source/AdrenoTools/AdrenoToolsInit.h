/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// AdrenoToolsInit.h
//
// GeneralsX @feature Claude 02/08/2026 App-bundled Mesa Turnip Vulkan driver.
// Declares the single entry point the engine calls before Vulkan initialization.

#pragma once

#if defined(__ANDROID__)
// Load the app-bundled Mesa Turnip Vulkan driver via libadrenotools and route
// SDL3 + DXVK through the vkGetInstanceProcAddr bridge. Returns true if Turnip
// is active; false keeps the system Vulkan (stock Qualcomm HAL).
bool AdrenoToolsInitVulkanDriver();
#endif
