cmake_minimum_required(VERSION 3.21)
project(OpenWaveAppdOnly LANGUAGES C CXX)

# Build the production daemon against the real exported SDK components, without
# configuring GUI applications that currently depend on missing WaterRouge APIs.
# Do not fabricate OpenWave::Application, Controls or Theme targets.
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Network)
find_package(OpenWaveSDK REQUIRED COMPONENTS Xml Identity Applications)

find_library(OPENWAVE_SELINUX_LIBRARY NAMES selinux REQUIRED)
add_library(openwave-selinux STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../security/selinux/OpenWaveSELinux.c")
target_compile_options(openwave-selinux PRIVATE -Wall -Wextra -Werror)
target_include_directories(openwave-selinux PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../security/selinux")
target_link_libraries(openwave-selinux PUBLIC "${OPENWAVE_SELINUX_LIBRARY}")

# Reuse appd's authoritative build and install rules. Nothing here defines
# new policies, ships substitute UI modules, or launches Waydroid.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../appd"
                 "${CMAKE_CURRENT_BINARY_DIR}/appd")
