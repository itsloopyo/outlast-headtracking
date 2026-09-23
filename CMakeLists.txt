cmake_minimum_required(VERSION 3.20)
project(OutlastHeadTracking VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# OutlastLauncher.exe picks Binaries\Win64\OLGame.exe on a 64-bit Windows and
# Binaries\Win32\OLGame.exe otherwise, and this shim is loaded into that process. A
# Win32 build produces a DLL the 64-bit game silently refuses to load, which reads in the
# log as "the mod never started" - there is no log at all.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "OutlastHeadTracking must be built as x64. Configure with -A x64.")
endif()

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    # Static CRT so the shipped dinput8.dll runs without a VC++ redistributable. The game
    # is from 2013 and ships its own MSVC 10 runtime; ours must not depend on a newer one
    # being present.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# Vendored rather than fetched: `pixi run package` has to succeed on a clean checkout
# with no network and no game installed, which a FetchContent at configure time does not.
set(MINHOOK_SOURCES
    extern/minhook/src/buffer.c
    extern/minhook/src/hook.c
    extern/minhook/src/trampoline.c
    extern/minhook/src/hde/hde32.c
    extern/minhook/src/hde/hde64.c
)

set(MOD_SOURCES
    src/dllmain.cpp
    src/dinput8_proxy.cpp
    src/build_registry.cpp
    src/camera_hook.cpp
    src/camera_trace.cpp
    src/camera_probe.cpp
    src/config.cpp
    src/data_watchpoint.cpp
    src/fov_override.cpp
    src/game_module.cpp
    src/game_state.cpp
    src/hotkeys.cpp
    src/lean_trace.cpp
    src/light_hook.cpp
    src/light_probe.cpp
    src/path_utils.cpp
    src/crosshair_hook.cpp
    src/pe_headers.cpp
    src/scene_view.cpp
    src/steam_offsets.cpp
    src/tracking_runtime.cpp
    src/ue3_camera_cache.cpp
    src/window_center.cpp
)

add_library(OutlastHeadTracking SHARED
    ${MINHOOK_SOURCES}
    ${MOD_SOURCES}
)

# Shipped AS dinput8.dll. The game statically imports DINPUT8.dll and the exe's own
# directory is searched before the system one, so dropping this beside the exe is the
# entire loading mechanism. src/dinput8.def exports the one function the game binds.
set_target_properties(OutlastHeadTracking PROPERTIES
    OUTPUT_NAME "dinput8"
    PREFIX ""
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_SOURCE_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_SOURCE_DIR}/bin/Debug"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/bin/Release"
)
# /LTCG in Release because cameraunlock is compiled with /GL. Without it the linker
# discovers that mid-link, prints a warning and restarts itself - which is the only noise
# left in a build that is otherwise silent under /W4 /WX.
target_link_options(OutlastHeadTracking PRIVATE
    "/DEF:${CMAKE_SOURCE_DIR}/src/dinput8.def"
    "$<$<CONFIG:Release>:/LTCG>")

target_compile_definitions(OutlastHeadTracking PRIVATE
    HEADTRACKING_VERSION="${PROJECT_VERSION}")

target_include_directories(OutlastHeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern/minhook/include
)

target_link_libraries(OutlastHeadTracking PRIVATE
    cameraunlock
    ws2_32
)

if(MSVC)
    target_compile_options(OutlastHeadTracking PRIVATE /W4 /WX /permissive- /wd4324)
    set_source_files_properties(${MINHOOK_SOURCES} PROPERTIES COMPILE_FLAGS "/W0")
endif()

option(OLHT_BUILD_TESTS "Build the mod's own unit tests" OFF)
if(OLHT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
