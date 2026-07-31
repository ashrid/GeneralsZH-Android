# GeneralsX @feature Claude 10/07/2026 Task 8 (D1): adversarial test harness (doctest).
# 7 adversarial ArchiveFileSystem multimap-dance tests in archive_override_test.cpp
# (overwrite ordering, getArchiveFile, empty archive, 3-archive sensitivity, 1000-archive
# stress with overwrite, malformed entries).
find_package(doctest CONFIG REQUIRED)

# Sanity test — verifies doctest links + ctest discovers/runs the test (no engine deps).
add_executable(sanity_test sanity_test.cpp)
target_link_libraries(sanity_test PRIVATE doctest::doctest)
add_test(NAME sanity_test COMMAND sanity_test)

# Adversarial ArchiveFileSystem multimap-dance tests (Task 8 D1). Links the engine via the
# corei_gameengine_private INTERFACE source-propagation + corei_gameengine_public includes.
# PreRTS.h lives in Include/Precompiled/ (that dir is commented out in corei_gameengine_private,
# so the propagated engine sources can't find it — add it explicitly here).
add_executable(archive_override_test archive_override_test.cpp)
target_include_directories(archive_override_test PRIVATE
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Include/Precompiled
    ${CMAKE_SOURCE_DIR}/Generals/Code/GameEngine/Include/Precompiled
    ${CMAKE_SOURCE_DIR}/Core/GameEngine/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/Compression
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WW3D2
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWAudio
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWDebug
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWLib
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWMath
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWSaveLoad
    ${CMAKE_SOURCE_DIR}/Dependencies/Utility
    ${CMAKE_SOURCE_DIR}/Generals/Code/CompatLib/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/CompatLib/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2
    ${CMAKE_SOURCE_DIR}/resources/gitinfo
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/include/dxvk
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/usr/include
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/usr/include/dxvk
    ${CMAKE_BINARY_DIR}/_deps/gamespy-src/include
    ${CMAKE_BINARY_DIR}/_deps/gamespy-src/include/gamespy
    ${CMAKE_BINARY_DIR}/_deps/lzhl-src/CompLibHeader
    ${CMAKE_BINARY_DIR}/_deps/sdl3-build/include-revision
    ${CMAKE_BINARY_DIR}/_deps/sdl3-src/include
)
target_link_libraries(archive_override_test PRIVATE doctest::doctest z_gameengine z_gameenginedevice zi_always core_profile_legacy d3dx8 corei_gameengine_public)
target_compile_features(archive_override_test PRIVATE cxx_std_20)
target_compile_definitions(archive_override_test PRIVATE
    RTS_ZEROHOUR RTS_RELEASE _UNIX
    SAGE_USE_SDL SAGE_USE_OPENAL SAGE_USE_GLM SAGE_UPDATE_CHECK
    IG_DEBUG_STACKTRACE
    DISABLE_MEMORYPOOL_CHECKPOINTING DISABLE_MEMORYPOOL_STACKTRACE
    HAVE_STRLCAT HAVE_STRLCPY HAVE_WCSLCAT HAVE_WCSLCPY
    CURL_STATICLIB NDEBUG
)
add_test(NAME archive_override_test COMMAND archive_override_test)

# GeneralsX @feature Claude 31/07/2026 RED phase: session-based Mods status predicate tests
# (mod_session_test.cpp). Four-case truth table for GeneralsX_SessionModLoadedFrom(modDir, modBIG):
# the predicate is declared in the test but defined later in MainMenu.cpp (GREEN phase). Until
# then this target intentionally fails to link with an undefined reference to that symbol —
# proof the test exists and guards the contract before any implementation. Same engine
# link/include setup as archive_override_test.
add_executable(mod_session_test mod_session_test.cpp)
target_include_directories(mod_session_test PRIVATE
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Include/Precompiled
    ${CMAKE_SOURCE_DIR}/Generals/Code/GameEngine/Include/Precompiled
    ${CMAKE_SOURCE_DIR}/Core/GameEngine/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/Compression
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WW3D2
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWAudio
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWDebug
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWLib
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWMath
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWSaveLoad
    ${CMAKE_SOURCE_DIR}/Dependencies/Utility
    ${CMAKE_SOURCE_DIR}/Generals/Code/CompatLib/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/CompatLib/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2
    ${CMAKE_SOURCE_DIR}/resources/gitinfo
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/include/dxvk
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/usr/include
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/usr/include/dxvk
    ${CMAKE_BINARY_DIR}/_deps/gamespy-src/include
    ${CMAKE_BINARY_DIR}/_deps/gamespy-src/include/gamespy
    ${CMAKE_BINARY_DIR}/_deps/lzhl-src/CompLibHeader
    ${CMAKE_BINARY_DIR}/_deps/sdl3-build/include-revision
    ${CMAKE_BINARY_DIR}/_deps/sdl3-src/include
)
target_link_libraries(mod_session_test PRIVATE doctest::doctest z_gameengine z_gameenginedevice zi_always core_profile_legacy d3dx8 corei_gameengine_public)
target_compile_features(mod_session_test PRIVATE cxx_std_20)
target_compile_definitions(mod_session_test PRIVATE
    RTS_ZEROHOUR RTS_RELEASE _UNIX
    SAGE_USE_SDL SAGE_USE_OPENAL SAGE_USE_GLM SAGE_UPDATE_CHECK
    IG_DEBUG_STACKTRACE
    DISABLE_MEMORYPOOL_CHECKPOINTING DISABLE_MEMORYPOOL_STACKTRACE
    HAVE_STRLCAT HAVE_STRLCPY HAVE_WCSLCAT HAVE_WCSLCPY
    CURL_STATICLIB NDEBUG
)
add_test(NAME mod_session_test COMMAND mod_session_test)

# GeneralsX @feature Claude 31/07/2026 RED phase: Android persisted relative mod-path
# normalization tests (mod_path_test.cpp). Four-case contract for
# GeneralsX_NormalizeModPath(rawPath, gameDataPath): the function is declared in the test
# but defined later in the engine (GREEN phase). Until then this target intentionally
# fails to link with an undefined reference to that symbol — proof the test exists and
# guards the contract before any implementation. Same engine link/include setup as
# mod_session_test / archive_override_test.
add_executable(mod_path_test mod_path_test.cpp)
target_include_directories(mod_path_test PRIVATE
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Include/Precompiled
    ${CMAKE_SOURCE_DIR}/Generals/Code/GameEngine/Include/Precompiled
    ${CMAKE_SOURCE_DIR}/Core/GameEngine/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/Compression
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WW3D2
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWAudio
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWDebug
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWLib
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWMath
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWSaveLoad
    ${CMAKE_SOURCE_DIR}/Dependencies/Utility
    ${CMAKE_SOURCE_DIR}/Generals/Code/CompatLib/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/CompatLib/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2
    ${CMAKE_SOURCE_DIR}/resources/gitinfo
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/include/dxvk
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/usr/include
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/usr/include/dxvk
    ${CMAKE_BINARY_DIR}/_deps/gamespy-src/include
    ${CMAKE_BINARY_DIR}/_deps/gamespy-src/include/gamespy
    ${CMAKE_BINARY_DIR}/_deps/lzhl-src/CompLibHeader
    ${CMAKE_BINARY_DIR}/_deps/sdl3-build/include-revision
    ${CMAKE_BINARY_DIR}/_deps/sdl3-src/include
)
target_link_libraries(mod_path_test PRIVATE doctest::doctest z_gameengine z_gameenginedevice zi_always core_profile_legacy d3dx8 corei_gameengine_public)
target_compile_features(mod_path_test PRIVATE cxx_std_20)
target_compile_definitions(mod_path_test PRIVATE
    RTS_ZEROHOUR RTS_RELEASE _UNIX
    SAGE_USE_SDL SAGE_USE_OPENAL SAGE_USE_GLM SAGE_UPDATE_CHECK
    IG_DEBUG_STACKTRACE
    DISABLE_MEMORYPOOL_CHECKPOINTING DISABLE_MEMORYPOOL_STACKTRACE
    HAVE_STRLCAT HAVE_STRLCPY HAVE_WCSLCAT HAVE_WCSLCPY
    CURL_STATICLIB NDEBUG
)
add_test(NAME mod_path_test COMMAND mod_path_test)

# GeneralsX @feature Claude 31/07/2026 RED phase: Android loose-mod fallback discovery test
# (android_fallback_test.cpp). Guards the Android-only bug where fixFilenameFromWindowsPath
# returns under #if defined(__ANDROID__) before consulting s_assetFallbackPath /
# s_assetFallbackPaths, so loadMods()-registered loose mod files (Data/INI/Locomotor.ini)
# cannot override retail archive entries on device.
#
# To exercise the real __ANDROID__ code path on the Linux host build, the target ALSO compiles
# the genuine Core/GameEngineDevice/Source/StdDevice/Common/StdLocalFileSystem.cpp as a source,
# with -D__ANDROID__ (and the CppMacros.h force-include the unit normally gets via the
# z_gameenginedevice PCH) applied to THAT translation unit only via set_source_files_properties.
# The android object supplies every StdLocalFileSystem::* symbol ahead of the z_gameenginedevice
# STATIC archive, so the archive's own (Linux) StdLocalFileSystem.o is never pulled in — no
# duplicate symbols, and doesFileExist/openFile run the genuine Android path. No
# directory_iterator is ever invoked (the Android early-exit from the case-insensitive
# traversal is preserved).
add_executable(android_fallback_test
    android_fallback_test.cpp
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/StdDevice/Common/StdLocalFileSystem.cpp
)
# Per-TU: force the __ANDROID__ code path + the CppMacros.h first-include the unit relies on.
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/StdDevice/Common/StdLocalFileSystem.cpp
    PROPERTIES COMPILE_OPTIONS "-include;Utility/CppMacros.h;-D__ANDROID__"
)
target_include_directories(android_fallback_test PRIVATE
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Include/Precompiled
    ${CMAKE_SOURCE_DIR}/Generals/Code/GameEngine/Include/Precompiled
    ${CMAKE_SOURCE_DIR}/Core/GameEngine/Include
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/Compression
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WW3D2
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWAudio
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWDebug
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWLib
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWMath
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWSaveLoad
    ${CMAKE_SOURCE_DIR}/Dependencies/Utility
    ${CMAKE_SOURCE_DIR}/Generals/Code/CompatLib/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/CompatLib/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2
    ${CMAKE_SOURCE_DIR}/resources/gitinfo
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/include/dxvk
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/usr/include
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/usr/include/dxvk
    ${CMAKE_BINARY_DIR}/_deps/gamespy-src/include
    ${CMAKE_BINARY_DIR}/_deps/gamespy-src/include/gamespy
    ${CMAKE_BINARY_DIR}/_deps/lzhl-src/CompLibHeader
    ${CMAKE_BINARY_DIR}/_deps/sdl3-build/include-revision
    ${CMAKE_BINARY_DIR}/_deps/sdl3-src/include
)
target_link_libraries(android_fallback_test PRIVATE doctest::doctest z_gameengine z_gameenginedevice zi_always core_profile_legacy d3dx8 corei_gameengine_public)
target_compile_features(android_fallback_test PRIVATE cxx_std_20)
target_compile_definitions(android_fallback_test PRIVATE
    RTS_ZEROHOUR RTS_RELEASE _UNIX
    SAGE_USE_SDL SAGE_USE_OPENAL SAGE_USE_GLM SAGE_UPDATE_CHECK
    IG_DEBUG_STACKTRACE
    DISABLE_MEMORYPOOL_CHECKPOINTING DISABLE_MEMORYPOOL_STACKTRACE
    HAVE_STRLCAT HAVE_STRLCPY HAVE_WCSLCAT HAVE_WCSLCPY
    CURL_STATICLIB NDEBUG
)
add_test(NAME android_fallback_test COMMAND android_fallback_test)

# GeneralsX @feature Claude 31/07/2026 Truth table for the Android archive skip predicate
# (GeneralsXArchiveFilter.h). The inline helper is pure + dependency-free, so this target only
# needs doctest + the engine Common include path. The TU is compiled -D__ANDROID__ so the
# helper's Android branch is live (off-Android it is a no-op, which would make every assertion
# pass for the wrong reason). See archive_filter_test.cpp for the locked hierarchy contract.
add_executable(archive_filter_test archive_filter_test.cpp)
target_include_directories(archive_filter_test PRIVATE ${CMAKE_SOURCE_DIR}/Core/GameEngine/Include)
target_link_libraries(archive_filter_test PRIVATE doctest::doctest)
target_compile_features(archive_filter_test PRIVATE cxx_std_20)
target_compile_definitions(archive_filter_test PRIVATE __ANDROID__)
add_test(NAME archive_filter_test COMMAND archive_filter_test)

# GeneralsX @feature Claude 31/07/2026 Integration test for the retailScan wiring. Compiles the
# REAL StdBIGFileSystem.cpp with -D__ANDROID__ for this target only (same per-TU technique as
# android_fallback_test), so loadBigFilesFromDirectory runs the genuine Android filter branch on
# the Linux host. The android object supplies every StdBIGFileSystem::* symbol ahead of the
# z_gameenginedevice STATIC archive, so the archive's own (Linux) StdBIGFileSystem.o is never
# pulled — the filter is exercised for real. archive_wiring_test.cpp drives it with a stub
# LocalFileSystem + a recording openArchiveFile override (no real .big I/O).
add_executable(archive_wiring_test
    archive_wiring_test.cpp
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFileSystem.cpp
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Source/StdDevice/Common/StdBIGFileSystem.cpp
    PROPERTIES COMPILE_OPTIONS "-include;Utility/CppMacros.h;-D__ANDROID__"
)
target_include_directories(archive_wiring_test PRIVATE
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Include/Precompiled
    ${CMAKE_SOURCE_DIR}/Generals/Code/GameEngine/Include/Precompiled
    ${CMAKE_SOURCE_DIR}/Core/GameEngine/Include
    ${CMAKE_SOURCE_DIR}/Core/GameEngineDevice/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Include
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/Compression
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WW3D2
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWAudio
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWDebug
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWLib
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWMath
    ${CMAKE_SOURCE_DIR}/Core/Libraries/Source/WWVegas/WWSaveLoad
    ${CMAKE_SOURCE_DIR}/Dependencies/Utility
    ${CMAKE_SOURCE_DIR}/Generals/Code/CompatLib/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/CompatLib/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/GameEngine/Include
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas
    ${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2
    ${CMAKE_SOURCE_DIR}/resources/gitinfo
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/include/dxvk
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/usr/include
    ${CMAKE_BINARY_DIR}/_deps/dxvk-src/usr/include/dxvk
    ${CMAKE_BINARY_DIR}/_deps/gamespy-src/include
    ${CMAKE_BINARY_DIR}/_deps/gamespy-src/include/gamespy
    ${CMAKE_BINARY_DIR}/_deps/lzhl-src/CompLibHeader
    ${CMAKE_BINARY_DIR}/_deps/sdl3-build/include-revision
    ${CMAKE_BINARY_DIR}/_deps/sdl3-src/include
)
target_link_libraries(archive_wiring_test PRIVATE doctest::doctest z_gameengine z_gameenginedevice zi_always core_profile_legacy d3dx8 corei_gameengine_public)
target_compile_features(archive_wiring_test PRIVATE cxx_std_20)
target_compile_definitions(archive_wiring_test PRIVATE
    RTS_ZEROHOUR RTS_RELEASE _UNIX
    SAGE_USE_SDL SAGE_USE_OPENAL SAGE_USE_GLM SAGE_UPDATE_CHECK
    IG_DEBUG_STACKTRACE
    DISABLE_MEMORYPOOL_CHECKPOINTING DISABLE_MEMORYPOOL_STACKTRACE
    HAVE_STRLCAT HAVE_STRLCPY HAVE_WCSLCAT HAVE_WCSLCPY
    CURL_STATICLIB NDEBUG
)
add_test(NAME archive_wiring_test COMMAND archive_wiring_test)
