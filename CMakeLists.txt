cmake_minimum_required(VERSION 3.25)
project(ConkerLauncher VERSION 1.0.0 LANGUAGES C CXX)

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
    resources/resources.qrc
)

target_link_libraries(ConkerLauncher PRIVATE Qt6::Widgets Qt6::Network LibArchive::LibArchive tomlplusplus::tomlplusplus
    libminiupnpc-static)

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
