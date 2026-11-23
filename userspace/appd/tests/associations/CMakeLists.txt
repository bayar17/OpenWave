cmake_minimum_required(VERSION 3.21)
project(AssociationTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
add_executable(association-test test.cpp ../../defaults.c ../../file-types.cpp ../../file-delivery.c ../../desktop.c ../../mimeapps.c ../../desktop-registry.c)
target_compile_definitions(association-test PRIVATE
    APPD_DEFAULTS_DIRECTORY="${CMAKE_CURRENT_BINARY_DIR}/defaults"
    APPD_SYSTEM_APPLICATIONS="${CMAKE_CURRENT_BINARY_DIR}/fixture/system"
    APPD_USER_APPLICATIONS="${CMAKE_CURRENT_BINARY_DIR}/fixture/user"
    APPD_DATA_DIRS="${CMAKE_CURRENT_BINARY_DIR}/fixture/usr/local/share:${CMAKE_CURRENT_BINARY_DIR}/fixture/usr/share"
    APPD_ANDROID_ROOT="${CMAKE_CURRENT_BINARY_DIR}/fixture/android"
    APPD_FIXTURE_ROOT="${CMAKE_CURRENT_BINARY_DIR}/fixture")
target_link_libraries(association-test PRIVATE Qt6::Core PkgConfig::GIO)
enable_testing()
add_test(NAME associations COMMAND association-test)
