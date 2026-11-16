cmake_minimum_required(VERSION 3.25)
project(ConkerLauncher VERSION 1.4.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# The build shown in the launcher (Settings -> About and credits). The release tag CI builds from (its checkout has no
# tags), else git describe here (v1.4.2, or v1.4.2-3-gabc1234-dirty between releases), else the project's version;
# -DCONKER_VERSION=... sets it. The game kit is the KIT_TAG the release workflow builds the game from.
set(CONKER_VERSION "" CACHE STRING "Launcher version to show (empty: from the release tag or git)")
set(CONKER_BUILD_VERSION "${CONKER_VERSION}")
if(NOT CONKER_BUILD_VERSION AND "$ENV{GITHUB_REF_TYPE}" STREQUAL "tag")
    set(CONKER_BUILD_VERSION "$ENV{GITHUB_REF_NAME}")
endif()
if(NOT CONKER_BUILD_VERSION)
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v*" --dirty
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE CONKER_BUILD_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(NOT CONKER_BUILD_VERSION)
            execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" OUTPUT_VARIABLE _sha
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(_sha)
                set(CONKER_BUILD_VERSION "v${PROJECT_VERSION}-g${_sha}")
            endif()
        endif()
    endif()
endif()
if(NOT CONKER_BUILD_VERSION)
    set(CONKER_BUILD_VERSION "v${PROJECT_VERSION}")
endif()
string(REGEX REPLACE "^v" "" CONKER_BUILD_VERSION "${CONKER_BUILD_VERSION}")
set(CONKER_KIT_TAG "")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/.github/workflows/build.yml" _kit REGEX "^[ ]*KIT_TAG:")
if(_kit)
    string(REGEX REPLACE "^[ ]*KIT_TAG:[ ]*['\"]?([^'\" ]+).*" "\\1" CONKER_KIT_TAG "${_kit}")
endif()
# configure again when a commit or tag moves, so a local build's version follows the checkout
foreach(_f .git/HEAD .git/index .git/packed-refs .github/workflows/build.yml)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${_f}")
    endif()
endforeach()
message(STATUS "Launcher version ${CONKER_BUILD_VERSION}, game kit ${CONKER_KIT_TAG}")
configure_file(src/version.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h" @ONLY)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
find_package(LibArchive REQUIRED)  # unpacks the decensor release (.7z)

# the same TOML library and version the game reads settings.toml with
include(FetchContent)
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(tomlplusplus)

# UPnP: the host's router opens the system link port (BSD-3-Clause), built in so no system package is needed
set(UPNPC_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(UPNPC_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(miniupnpc
    GIT_REPOSITORY https://github.com/miniupnp/miniupnp.git
    GIT_TAG miniupnpc_2_3_3
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR miniupnpc)
FetchContent_MakeAvailable(miniupnpc)

# SDL3 for controller navigation (src/gamepad.cpp): static, gamepads only (no audio, renderer or GPU), the release the
# game's next kit pins
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
set(SDL_RENDER OFF CACHE BOOL "" FORCE)
set(SDL_GPU OFF CACHE BOOL "" FORCE)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
# on Linux pads need no video (evdev, udev or inotify), and SDL's X11 and Wayland parts would want their development
# packages on the build machine (the CI runner has no Xcursor's): left out there
if(UNIX AND NOT APPLE)
    set(SDL_VIDEO OFF CACHE BOOL "" FORCE)
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)  # SDL's own switch for a build without X11 or Wayland
endif()
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(SDL3)

qt_add_executable(ConkerLauncher WIN32
    src/main.cpp
    src/paths.cpp
    src/config.cpp
    src/keys.cpp
    src/keybutton.cpp
    src/mods.cpp
    src/decensor.cpp
    src/install.cpp
    src/online.cpp
    src/setuppage.cpp
    src/mainwindow.cpp
    src/gamepad.cpp
    resources/resources.qrc
)

target_include_directories(ConkerLauncher PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(ConkerLauncher PRIVATE Qt6::Widgets Qt6::Network LibArchive::LibArchive tomlplusplus::tomlplusplus
    libminiupnpc-static SDL3::SDL3-static)

if(WIN32)
    target_compile_definitions(ConkerLauncher PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_compile_options(ConkerLauncher PRIVATE /W4 /utf-8)
else()
    target_compile_options(ConkerLauncher PRIVATE -Wall -Wextra)
endif()

# a console tool for testing the decensor download and unpacking (not part of the launcher)
add_executable(decensor_tool EXCLUDE_FROM_ALL tests/decensor_tool.cpp src/decensor.cpp)
target_link_libraries(decensor_tool PRIVATE Qt6::Network LibArchive::LibArchive)

# a console tool for testing join codes, the router probe and STUN (not part of the launcher)
add_executable(online_tool EXCLUDE_FROM_ALL tests/online_tool.cpp src/online.cpp resources/resources.qrc)
target_link_libraries(online_tool PRIVATE Qt6::Network libminiupnpc-static)
