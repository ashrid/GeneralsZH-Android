# GeneralsX @feature Claude 02/08/2026 App-bundled Mesa Turnip Vulkan driver for Android.
#
# Builds:
#   - libadrenotools.so      — rootless per-app custom Vulkan driver loader
#   - libmain_hook.so        — linker-namespace hook lib (must live in nativeLibraryDir)
#   - libhook_impl.so        — hook implementation dependency of libmain_hook
#   - libvulkan_bridge.so    — dlopen-able shim exporting vkGetInstanceProcAddr that
#                              forwards to the AdrenoTools-loaded Turnip loader, so SDL3
#                              (SDL_HINT_VULKAN_LIBRARY) and DXVK both route through Turnip
#
# Android-only. On every other platform this directory contributes nothing.
if(ANDROID AND (ANDROID_ABI STREQUAL "arm64-v8a"))
    # Vendored libadrenotools (upstream CMake). It asserts arm64-v8a itself.
    set(BUILD_SHARED_LIBS ON)
    add_subdirectory(adrenotools)

    # dlopen-able bridge .so. SDL3's Android_Vulkan_LoadLibrary does
    # SDL_LoadObject(path) and reads vkGetInstanceProcAddr from the loaded lib;
    # the AdrenoTools handle is not a filesystem path, so this real .so on disk
    # forwards vkGetInstanceProcAddr to it. Also satisfies DXVK's loader seam.
    add_library(vulkan_bridge SHARED libvulkan_bridge.cpp)
    target_compile_features(vulkan_bridge PRIVATE cxx_std_20)
    target_link_libraries(vulkan_bridge PRIVATE dl)

    # Static object linked into z_gameenginedevice: the init module the engine
    # calls from SDL3Main before Vulkan initialization.
    add_library(adrenotools_init STATIC AdrenoToolsInit.cpp)
    target_include_directories(adrenotools_init PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_features(adrenotools_init PRIVATE cxx_std_20)
    target_link_libraries(adrenotools_init PRIVATE adrenotools sdl3lib android log)

    # The hook libraries and bridge must land in nativeLibraryDir for the
    # AdrenoTools hook (hookLibDir == ApplicationInfo.nativeLibraryDir) and for
    # SDL/DXVK dlopen to find the bridge. They are staged into jniLibs by
    # scripts/build/android/package-android-zh.sh, so set their output names.
    set_target_properties(main_hook PROPERTIES OUTPUT_NAME "main_hook")
    set_target_properties(hook_impl PROPERTIES OUTPUT_NAME "hook_impl")
    set_target_properties(vulkan_bridge PROPERTIES OUTPUT_NAME "vulkan_bridge")

    # Export the hook + bridge artifacts so the packager can find them.
    set(ADRENOTOOLS_HOOK_LIBS
        $<TARGET_FILE:main_hook>
        $<TARGET_FILE:hook_impl>
        $<TARGET_FILE:vulkan_bridge>
        CACHE INTERNAL "AdrenoTools hook + bridge libraries for APK packaging"
    )
endif()
