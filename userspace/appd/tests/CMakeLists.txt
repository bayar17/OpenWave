cmake_minimum_required(VERSION 3.21)
project(AppdRuntimeTests LANGUAGES C CXX)
find_package(Qt6 REQUIRED COMPONENTS Core Network Sql)
find_package(OpenWaveSDK REQUIRED COMPONENTS Applications)
add_executable(appd-qa runtime.cpp)
target_link_libraries(appd-qa PRIVATE OpenWave::Applications Qt6::Core Qt6::Network Qt6::Sql)
set_target_properties(appd-qa PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "/usr/lib64;/usr/lib")
add_executable(appd-qa-init init.c)
add_executable(appd-folder-tests folders.c ../user-folders.c)
target_include_directories(appd-folder-tests PRIVATE
    "${CMAKE_SYSROOT}/usr/include/glib-2.0" "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
target_link_libraries(appd-folder-tests PRIVATE glib-2.0)
