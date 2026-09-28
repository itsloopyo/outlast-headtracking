cmake_minimum_required(VERSION 3.20)

# Everything under test is a pure function of its arguments - the rotator arithmetic,
# the head-pose composition, the config boundary checks, the field-of-view decision and
# the camera-cache predicates - so nothing here loads the game, hooks anything or needs
# a running process. That is what makes the axis signs and the unit conversions
# checkable at all: in game they are one playtest each.
add_executable(OutlastHeadTrackingTests
    test_main.cpp
    ue3_math_tests.cpp
    head_pose_tests.cpp
    config_tests.cpp
    frame_tests.cpp
    frame_zoom_tests.cpp
    aim_projection_tests.cpp
    camera_cache_tests.cpp
    window_placement_tests.cpp
    light_composition_tests.cpp
    build_profile_tests.cpp
    log_budget_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/ue3_camera_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/steam_offsets.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/legacy_config/legacy_config.cpp
    ${CMAKE_SOURCE_DIR}/src/path_utils.cpp
)

target_include_directories(OutlastHeadTrackingTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# For the core headers the mod's own headers include, and for the config table config.cpp
# builds.
target_link_libraries(OutlastHeadTrackingTests PRIVATE cameraunlock)

if(MSVC)
    target_compile_options(OutlastHeadTrackingTests PRIVATE /W4 /WX /permissive-)
endif()

add_test(NAME OutlastHeadTrackingTests COMMAND OutlastHeadTrackingTests)

# The config differential test. The oracle is the newest published build's reader and hotkey
# registration with the core sources they compiled, copied verbatim into
# config_differential/oracle. It is two libraries because Hotkeys::Start has to run on
# oracle_fake's recording poller; each renames the two namespaces its own way, so neither
# collides with the other or with the current core.
set(OLHT_DIFF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/config_differential)
set(OLHT_ORACLE_DIR ${OLHT_DIFF_DIR}/oracle)
add_library(outlast_config_oracle STATIC
    ${OLHT_ORACLE_DIR}/src/config.cpp
    ${OLHT_ORACLE_DIR}/core/src/config/ini_reader.cpp
    ${OLHT_ORACLE_DIR}/core/src/logging/file_log.cpp
    ${OLHT_DIFF_DIR}/oracle_config_adapter.cpp
)
target_include_directories(outlast_config_oracle PRIVATE
    ${OLHT_ORACLE_DIR}/src
    ${OLHT_ORACLE_DIR}/core/include
    ${OLHT_DIFF_DIR})
target_compile_definitions(outlast_config_oracle PRIVATE
    cameraunlock=outlast_oracle_core
    OutlastHeadTracking=outlast_oracle)

add_library(outlast_hotkeys_oracle STATIC
    ${OLHT_ORACLE_DIR}/src/hotkeys.cpp
    ${OLHT_ORACLE_DIR}/core/src/logging/file_log.cpp
    ${OLHT_DIFF_DIR}/oracle_hotkeys_adapter.cpp
)
target_include_directories(outlast_hotkeys_oracle PRIVATE
    ${OLHT_DIFF_DIR}/oracle_fake
    ${OLHT_ORACLE_DIR}/src
    ${OLHT_ORACLE_DIR}/core/include
    ${OLHT_DIFF_DIR})
target_compile_definitions(outlast_hotkeys_oracle PRIVATE
    cameraunlock=outlast_oracle_hk_core
    OutlastHeadTracking=outlast_oracle_hk)

add_executable(config_differential_tests
    ${OLHT_DIFF_DIR}/differential_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/legacy_config/legacy_config.cpp
    ${CMAKE_SOURCE_DIR}/src/path_utils.cpp
)
target_include_directories(config_differential_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${OLHT_DIFF_DIR})
target_compile_definitions(config_differential_tests PRIVATE
    OUTLAST_DIFFERENTIAL_DATA="${OLHT_DIFF_DIR}/data"
    OUTLAST_COMMITTED_CONFIG="${CMAKE_SOURCE_DIR}/config/CameraUnlock.ini")
target_link_libraries(config_differential_tests PRIVATE
    cameraunlock outlast_config_oracle outlast_hotkeys_oracle)
if(MSVC)
    target_compile_options(config_differential_tests PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME config_differential_tests COMMAND config_differential_tests)

# The differential test is only as good as its claim about what it compiled.
add_test(NAME config_provenance COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File ${OLHT_DIFF_DIR}/check-provenance.ps1)

# Core's canonical config lint over the committed file and every file the differential test
# migrated, which it writes beside itself under migrated.
add_test(NAME config_lint COMMAND node
    ${OLHT_DIFF_DIR}/lint-migrated.mjs
    $<TARGET_FILE_DIR:config_differential_tests>/migrated)
set_tests_properties(config_lint PROPERTIES DEPENDS config_differential_tests)
set_tests_properties(config_differential_tests config_provenance config_lint PROPERTIES LABELS differential)

# The committed config against the table, a first start, the toggles' saves and the legacy
# file's ANSI path. `--render-config <path>` makes it write the committed file instead
# (pixi run render-config).
add_executable(config_tests
    canonical_config_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/legacy_config/legacy_config.cpp
    ${CMAKE_SOURCE_DIR}/src/path_utils.cpp
)
target_include_directories(config_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(config_tests PRIVATE cameraunlock)
target_compile_definitions(config_tests PRIVATE
    OUTLAST_COMMITTED_CONFIG="${CMAKE_SOURCE_DIR}/config/CameraUnlock.ini")
if(MSVC)
    target_compile_options(config_tests PRIVATE /W4 /WX /permissive-)
endif()
add_test(NAME config_tests COMMAND config_tests)
