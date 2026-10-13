cmake_minimum_required(VERSION 3.21)
project(AppdRuntimeTests LANGUAGES C CXX)
find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(OpenWaveSDK REQUIRED COMPONENTS Applications)
add_executable(appd-qa runtime.cpp)
target_link_libraries(appd-qa PRIVATE OpenWave::Applications Qt6::Core Qt6::Network)
set_target_properties(appd-qa PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/../lib64")
add_executable(appd-qa-init init.c)
