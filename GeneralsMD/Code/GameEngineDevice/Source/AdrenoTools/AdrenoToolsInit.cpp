/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

// AdrenoToolsInit.cpp
//
// GeneralsX @feature Claude 02/08/2026 App-bundled Mesa Turnip Vulkan driver.
//
// The stock Qualcomm Adreno Vulkan driver has a confirmed memory-allocator fault
// in DxvkMemoryAllocator::createAllocation (during D3D9ConstantBuffer::AllocSlice
// for fixed-function VS) that crashes DXVK — reliably under mod load, and at
// ~150s even vanilla. Mesa Turnip handles the same path correctly (proven via a
// system-wide Magisk swap). This module loads Turnip per-app, rootless, so other
// apps keep the stock driver.
//
// Flow (called from SDL3Main BEFORE SDL_Vulkan_LoadLibrary / DXVK init):
//   1. Locate the bundled Turnip driver (APK assets/turnip/libvulkan_freedreno.so)
//      and extract it to the app's internal files dir (dlopen cannot read sdcard).
//   2. Obtain ApplicationInfo.nativeLibraryDir (where the hook libs must live).
//   3. adrenotools_open_libvulkan() -> handle to the Turnip-backed libvulkan.
//   4. Load libvulkan_bridge.so (dlopen-able shim exporting vkGetInstanceProcAddr
//      that forwards to the AdrenoTools handle) and install the loader pointer.
//   5. Set GX_VULKAN_LIBRARY (DXVK loader seam) and SDL_HINT_VULKAN_LIBRARY (SDL3)
//      to the bridge's absolute path so both DXVK and SDL3 route through Turnip.

#if defined(__ANDROID__)

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include <SDL3/SDL.h>

// AdrenoTools public API (vendored under this dir).
#include "adrenotools/include/adrenotools/driver.h"

#define ADRENOTOOLS_LOG(...) \
	__android_log_print(ANDROID_LOG_INFO, "GeneralsX", __VA_ARGS__)

namespace {

// Copy an APK asset (by name) to an absolute output path. Returns true on success.
bool extractAssetToFile(AAssetManager *mgr, const char *assetName, const char *outPath)
{
	AAsset *asset = AAssetManager_open(mgr, assetName, AASSET_MODE_STREAMING);
	if (asset == nullptr)
		return false;

	const off_t size = AAsset_getLength(asset);
	FILE *out = fopen(outPath, "wb");
	if (out == nullptr) {
		AAsset_close(asset);
		return false;
	}

	char buf[65536];
	off_t remaining = size;
	while (remaining > 0) {
		const int n = AAsset_read(asset, buf, (size_t)(remaining > (off_t)sizeof(buf) ? sizeof(buf) : remaining));
		if (n <= 0)
			break;
		fwrite(buf, 1, (size_t)n, out);
		remaining -= n;
	}

	fclose(out);
	AAsset_close(asset);
	return remaining == 0;
}

// Resolve ApplicationInfo.nativeLibraryDir via the activity's Context. This is
// where libmain_hook.so / libhook_impl.so / libvulkan_bridge.so must live.
bool getNativeLibraryDir(std::string &outDir)
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity = (jobject)SDL_GetAndroidActivity();
	if (env == nullptr || activity == nullptr)
		return false;

	jclass activityClass = env->GetObjectClass(activity);
	jmethodID getAppInfo = env->GetMethodID(activityClass, "getApplicationInfo",
		"()Landroid/content/pm/ApplicationInfo;");
	if (getAppInfo == nullptr) {
		env->DeleteLocalRef(activityClass);
		return false;
	}

	jobject appInfo = env->CallObjectMethod(activity, getAppInfo);
	jclass appInfoClass = env->GetObjectClass(appInfo);
	jfieldID nativeLibDirField = env->GetFieldID(appInfoClass, "nativeLibraryDir", "Ljava/lang/String;");
	if (nativeLibDirField == nullptr) {
		env->DeleteLocalRef(appInfoClass);
		env->DeleteLocalRef(appInfo);
		env->DeleteLocalRef(activityClass);
		return false;
	}

	jstring dirJStr = (jstring)env->GetObjectField(appInfo, nativeLibDirField);
	const char *dirCStr = env->GetStringUTFChars(dirJStr, nullptr);
	outDir = dirCStr;
	env->ReleaseStringUTFChars(dirJStr, dirCStr);

	env->DeleteLocalRef(dirJStr);
	env->DeleteLocalRef(appInfoClass);
	env->DeleteLocalRef(appInfo);
	env->DeleteLocalRef(activityClass);
	return !outDir.empty();
}

// Resolve the app's internal files dir (getFilesDir) — writable, non-sdcard, so
// dlopen can load the Turnip driver from it.
bool getInternalFilesDir(std::string &outDir)
{
	JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
	jobject activity = (jobject)SDL_GetAndroidActivity();
	if (env == nullptr || activity == nullptr)
		return false;

	jclass activityClass = env->GetObjectClass(activity);
	jmethodID getFilesDir = env->GetMethodID(activityClass, "getFilesDir", "()Ljava/io/File;");
	if (getFilesDir == nullptr) {
		env->DeleteLocalRef(activityClass);
		return false;
	}

	jobject filesDir = env->CallObjectMethod(activity, getFilesDir);
	jclass fileClass = env->GetObjectClass(filesDir);
	jmethodID getPath = env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
	jstring pathJStr = (jstring)env->CallObjectMethod(filesDir, getPath);
	const char *pathCStr = env->GetStringUTFChars(pathJStr, nullptr);
	outDir = pathCStr;
	env->ReleaseStringUTFChars(pathJStr, pathCStr);

	env->DeleteLocalRef(pathJStr);
	env->DeleteLocalRef(fileClass);
	env->DeleteLocalRef(filesDir);
	env->DeleteLocalRef(activityClass);
	return !outDir.empty();
}

// Configure the bridge .so: dlopen it (from nativeLibraryDir), then hand it the
// AdrenoTools loader handle so its vkGetInstanceProcAddr forwards to Turnip.
// Returns the bridge's absolute path (for SDL_HINT_VULKAN_LIBRARY / GX_VULKAN_LIBRARY).
std::string installBridge(void *libVulkanHandle, const std::string &nativeLibDir)
{
	std::string bridgePath = nativeLibDir + "/libvulkan_bridge.so";

	void *bridge = dlopen(bridgePath.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (bridge == nullptr) {
		ADRENOTOOLS_LOG("AdrenoTools: dlopen bridge failed: %s", dlerror());
		return "";
	}

	typedef void (*SetLoaderFn)(void *);
	SetLoaderFn setLoader = (SetLoaderFn)dlsym(bridge, "libvulkan_bridge_set_loader");
	if (setLoader == nullptr) {
		ADRENOTOOLS_LOG("AdrenoTools: bridge set_loader symbol missing: %s", dlerror());
		dlclose(bridge);
		return "";
	}

	setLoader(libVulkanHandle);
	ADRENOTOOLS_LOG("AdrenoTools: bridge installed at %s", bridgePath.c_str());
	return bridgePath;
}

} // namespace

// Public entry point — called from SDL3Main before Vulkan initialization.
// Returns true if Turnip was loaded and routed through the bridge; false if the
// stock path should be kept (vanilla/desktop behavior or init failure).
bool AdrenoToolsInitVulkanDriver()
{
	// Only meaningful on Adreno Android devices; skip cleanly elsewhere.
	std::string nativeLibDir;
	std::string filesDir;
	if (!getNativeLibraryDir(nativeLibDir) || !getInternalFilesDir(filesDir)) {
		ADRENOTOOLS_LOG("AdrenoTools: could not resolve app dirs, keeping system Vulkan");
		return false;
	}
	ADRENOTOOLS_LOG("AdrenoTools: nativeLibraryDir=%s filesDir=%s", nativeLibDir.c_str(), filesDir.c_str());

	// The Turnip driver ships in APK assets; extract to the internal files dir.
	AAssetManager *mgr = nullptr;
	{
		JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
		jobject activity = (jobject)SDL_GetAndroidActivity();
		if (env != nullptr && activity != nullptr) {
			jclass cls = env->GetObjectClass(activity);
			jmethodID mid = env->GetMethodID(cls, "getAssets", "()Landroid/content/res/AssetManager;");
			if (mid != nullptr) {
				jobject javaMgr = env->CallObjectMethod(activity, mid);
				if (javaMgr != nullptr) {
					mgr = AAssetManager_fromJava(env, javaMgr);
					env->DeleteLocalRef(javaMgr);
				}
			}
			env->DeleteLocalRef(cls);
		}
	}
	if (mgr == nullptr) {
		ADRENOTOOLS_LOG("AdrenoTools: no AAssetManager, keeping system Vulkan");
		return false;
	}

	// Extract libvulkan_freedreno.so (Turnip) to <filesDir>/turnip/.
	const std::string turnipDir = filesDir + "/turnip/";
	mkdir(turnipDir.c_str(), 0700);
	const std::string driverPath = turnipDir + "libvulkan_freedreno.so";
	const bool driverExists = (access(driverPath.c_str(), F_OK) == 0);
	if (!driverExists) {
		if (!extractAssetToFile(mgr, "turnip/libvulkan_freedreno.so", driverPath.c_str())) {
			ADRENOTOOLS_LOG("AdrenoTools: could not extract Turnip driver from assets, keeping system Vulkan");
			return false;
		}
		chmod(driverPath.c_str(), 0700);
	}
	ADRENOTOOLS_LOG("AdrenoTools: Turnip driver at %s", driverPath.c_str());

	// Load Turnip via AdrenoTools. hookLibDir must be nativeLibraryDir; the custom
	// driver dir must be internal (not sdcard). tmpLibDir only matters on API < 29.
	void *libVulkan = adrenotools_open_libvulkan(
		RTLD_NOW | RTLD_LOCAL,
		ADRENOTOOLS_DRIVER_CUSTOM,
		nullptr,                 // tmpLibDir (memfd used on API 29+)
		nativeLibDir.c_str(),    // hookLibDir
		turnipDir.c_str(),       // customDriverDir (trailing slash)
		"libvulkan_freedreno.so",// customDriverName
		nullptr,                 // fileRedirectDir
		nullptr);                // userMappingHandle

	if (libVulkan == nullptr) {
		ADRENOTOOLS_LOG("AdrenoTools: adrenotools_open_libvulkan failed: %s", dlerror());
		return false;
	}
	ADRENOTOOLS_LOG("AdrenoTools: Turnip libvulkan loaded via AdrenoTools");

	// Route SDL3 + DXVK through the bridge.
	const std::string bridgePath = installBridge(libVulkan, nativeLibDir);
	if (bridgePath.empty())
		return false;

	setenv("GX_VULKAN_LIBRARY", bridgePath.c_str(), 1);
	SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, bridgePath.c_str());
	ADRENOTOOLS_LOG("AdrenoTools: GX_VULKAN_LIBRARY + SDL_HINT_VULKAN_LIBRARY -> %s", bridgePath.c_str());
	return true;
}

#endif // __ANDROID__
