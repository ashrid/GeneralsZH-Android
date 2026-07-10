# GeneralsX @feature Claude 10/07/2026 Task 8 (D1): adversarial test harness (doctest).
# Foundation: doctest + ctest infrastructure. The 7 adversarial ArchiveFileSystem
# multimap-dance tests (engine init + synthetic .big + assertions) are added next.
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
