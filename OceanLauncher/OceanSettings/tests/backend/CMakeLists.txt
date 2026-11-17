cmake_minimum_required(VERSION 3.21)
project(SettingsBackendTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(settings-backend-tests tst_settingsbackend.cpp
    ../../SettingsBackend.cpp ../../SettingsBackend.h)
target_link_libraries(settings-backend-tests PRIVATE Qt6::Core)
enable_testing()
add_test(NAME settings-backend COMMAND settings-backend-tests)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
add_executable(connectivity-backend-tests tst_connectivitybackend.cpp
    ../../ConnectivityBackend.cpp ../../ConnectivityBackend.h)
target_link_libraries(connectivity-backend-tests PRIVATE Qt6::Core PkgConfig::GIO)
find_program(GIO_TEST_PYTHON NAMES python3 PATHS /usr/bin NO_DEFAULT_PATH REQUIRED)
add_test(NAME connectivity-backend COMMAND "${GIO_TEST_PYTHON}"
    "${CMAKE_CURRENT_SOURCE_DIR}/mock_connectivity.py" $<TARGET_FILE:connectivity-backend-tests>)
