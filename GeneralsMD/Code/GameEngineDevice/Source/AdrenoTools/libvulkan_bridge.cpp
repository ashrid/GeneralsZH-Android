/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// libvulkan_bridge.cpp
//
// GeneralsX @feature Claude 02/08/2026 Vulkan loader bridge for the app-bundled
// Mesa Turnip driver. SDL3's Android_Vulkan_LoadLibrary does SDL_LoadObject(path)
// and reads vkGetInstanceProcAddr from the loaded library; DXVK's
// loadVulkanLibrary() does the same via dlopen("libvulkan.so"). Neither can be
// pointed at the handle returned by adrenotools_open_libvulkan() because that is
// an in-memory dlopen handle, not a filesystem path. This bridge is a real .so on
// disk that exports vkGetInstanceProcAddr and forwards to the AdrenoTools-loaded
// Turnip loader, so both SDL3 and DXVK end up routing through Turnip.
//
// Usage:
//   1. Call adrenotools_open_libvulkan() and save the returned handle.
//   2. dlopen("libvulkan_bridge.so"), call libvulkan_bridge_set_loader(handle).
//   3. Set SDL_HINT_VULKAN_LIBRARY to the bridge's absolute path before
//      SDL_Vulkan_LoadLibrary; DXVK picks up the same pointer via its env seam.

#include <dlfcn.h>

extern "C" {

static void *s_loaderHandle = nullptr;

// Called by the engine after adrenotools_open_libvulkan() succeeds. The handle
// is a dlopen handle to the Turnip-backed libvulkan; vkGetInstanceProcAddr is
// resolved from it lazily so this bridge works for both SDL3 and DXVK.
void libvulkan_bridge_set_loader(void *loaderHandle)
{
	s_loaderHandle = loaderHandle;
}

typedef void *(*PFN_vkGetInstanceProcAddrCompat)(void *, const char *);

void *vkGetInstanceProcAddr(void *instance, const char *pName)
{
	if (s_loaderHandle == nullptr)
		return nullptr;
	PFN_vkGetInstanceProcAddrCompat realGetInstanceProcAddr =
		(PFN_vkGetInstanceProcAddrCompat)dlsym(s_loaderHandle, "vkGetInstanceProcAddr");
	if (realGetInstanceProcAddr == nullptr)
		return nullptr;
	return realGetInstanceProcAddr(instance, pName);
}

// SDL3 reads vkEnumerateInstanceExtensionProperties directly from the loader
// library via vkGetInstanceProcAddr(VK_NULL_HANDLE, ...), so the single
// vkGetInstanceProcAddr export above is sufficient. Keep a minimal symbol for
// linkers that expect it.
void *vkEnumerateInstanceExtensionProperties(const char *pLayerName, void *pPropertyCount, void *pProperties)
{
	if (s_loaderHandle == nullptr)
		return nullptr;
	typedef void *(*PFN_vkEnumerateInstanceExtensionPropertiesCompat)(const char *, void *, void *);
	PFN_vkEnumerateInstanceExtensionPropertiesCompat realEnum = (PFN_vkEnumerateInstanceExtensionPropertiesCompat)
		dlsym(s_loaderHandle, "vkEnumerateInstanceExtensionProperties");
	if (realEnum == nullptr)
		return nullptr;
	return realEnum(pLayerName, pPropertyCount, pProperties);
}

} // extern "C"
