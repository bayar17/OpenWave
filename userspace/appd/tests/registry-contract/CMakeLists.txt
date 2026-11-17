cmake_minimum_required(VERSION 3.21)
project(OpenWaveRegistryContract LANGUAGES C CXX)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_executable(registry-contract contract.cpp ../../registry.c ../../identity.c)
target_compile_options(registry-contract PRIVATE -ffunction-sections -fdata-sections)
target_link_options(registry-contract PRIVATE -Wl,--gc-sections)
target_include_directories(registry-contract PRIVATE ../.. "${ROOT}/userspace/security" "${ROOT}/toolchain/include"
    "${CMAKE_SYSROOT}/usr/include/glib-2.0" "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
target_link_libraries(registry-contract PRIVATE Qt6::Core Qt6::Network gio-2.0 gobject-2.0 glib-2.0)
find_library(SELINUX_LIBRARY NAMES selinux REQUIRED)
add_executable(registry-manifests manifests.cpp ../../registry.c ../../identity.c ../../manifest.cpp)
target_compile_options(registry-manifests PRIVATE -ffunction-sections -fdata-sections)
target_link_options(registry-manifests PRIVATE -Wl,--gc-sections)
target_include_directories(registry-manifests PRIVATE ../.. "${ROOT}/toolchain/include"
    "${CMAKE_SYSROOT}/usr/include/glib-2.0" "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
target_link_libraries(registry-manifests PRIVATE Qt6::Core gio-2.0 gobject-2.0 glib-2.0 "${SELINUX_LIBRARY}")
set(CMAKE_AUTOMOC ON)
add_executable(ocean-startup-contract ocean.cpp
    "${ROOT}/OceanLauncher/OceanLauncher/AppController.cpp"
    "${ROOT}/OceanLauncher/OceanLauncher/AppController.h"
    "${CMAKE_SYSROOT}/usr/include/OpenWave/ApplicationsClient.h")
target_link_libraries(ocean-startup-contract PRIVATE Qt6::Core Qt6::Network)
