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
    ${CMAKE_SOURCE_DIR}/src/ue3_camera_cache.cpp
    ${CMAKE_SOURCE_DIR}/src/steam_offsets.cpp
)

target_include_directories(OutlastHeadTrackingTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# For the core headers the mod's own headers include (position_settings.h,
# smoothing_utils.h, angle_utils.h). Nothing here calls into the library itself.
target_link_libraries(OutlastHeadTrackingTests PRIVATE cameraunlock)

if(MSVC)
    target_compile_options(OutlastHeadTrackingTests PRIVATE /W4 /WX /permissive-)
endif()

add_test(NAME OutlastHeadTrackingTests COMMAND OutlastHeadTrackingTests)
