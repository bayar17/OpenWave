cmake_minimum_required(VERSION 3.21)
project(OceanOnly LANGUAGES CXX)

# Build Ocean Launcher alone against the libraries already installed in the
# SDK sysroot (and shipped in rootfs).
#
# The installed OpenWave SDK config exports no CMake targets for Controls,
# Theme and Application, although their libraries, headers and QML plugins
# are installed (docs/APPD-ONLY-STAGE2H-BUILD.md). This wrapper declares
# IMPORTED targets for exactly those three installed shared libraries and
# fails closed if any of them is missing. It invents no API and changes
# neither the SDK nor Ocean's own CMakeLists.txt. It is not a substitute for
# restoring the WaterRouge SDK sources.

set(_openwave_usr "${CMAKE_SYSROOT}/usr")
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Quick)
foreach(_pair "Controls:controls" "Theme:theme" "Application:application")
    string(REPLACE ":" ";" _pair "${_pair}")
    list(GET _pair 0 _name)
    list(GET _pair 1 _library)
    set(_path "${_openwave_usr}/lib64/libopenwave-${_library}.so.0.1.0")
    if(NOT EXISTS "${_path}")
        message(FATAL_ERROR "Installed OpenWave ${_name} library is missing: ${_path}")
    endif()
    if(NOT TARGET OpenWave::${_name})
        add_library(OpenWave::${_name} SHARED IMPORTED)
        set_target_properties(OpenWave::${_name} PROPERTIES
            IMPORTED_LOCATION "${_path}"
            IMPORTED_SONAME "libopenwave-${_library}.so.0"
            INTERFACE_INCLUDE_DIRECTORIES "${_openwave_usr}/include;${_openwave_usr}/include/OpenWave"
            INTERFACE_LINK_LIBRARIES "Qt6::Core;Qt6::Gui;Qt6::Quick")
    endif()
    set(OpenWaveSDK_${_name}_FOUND TRUE)
endforeach()

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../OceanLauncher/OceanLauncher" OceanLauncher)
