cmake_minimum_required(VERSION 3.25)
project(ConkerLauncher VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

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
