cmake_minimum_required(VERSION 3.21)

# This directory can be built on its own with the OpenWave cross-toolchain.
# The full userspace build includes it as a subdirectory. It intentionally
# has no Qt or OpenWaveSDK dependencies.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(OpenWaveWaydroidMount VERSION 0.1.0 LANGUAGES C)
endif()

add_executable(waydroid-mount "${CMAKE_CURRENT_LIST_DIR}/../waydroid_mount.c")
target_compile_features(waydroid-mount PRIVATE c_std_11)
target_compile_options(waydroid-mount PRIVATE -Wall -Wextra -Werror)
install(TARGETS waydroid-mount RUNTIME DESTINATION libexec/openwave-waydroid)
