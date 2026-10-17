cmake_minimum_required(VERSION 3.21)
project(OpenWaveWebTests LANGUAGES CXX)
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick Test Network)
find_package(OpenWaveSDK REQUIRED COMPONENTS Web)
qt_standard_project_setup(REQUIRES 6.10)
qt_add_executable(webview-tests tst_webview.cpp)
target_link_libraries(webview-tests PRIVATE Qt6::Quick Qt6::Test Qt6::Network OpenWave::Web)
set_target_properties(webview-tests PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/../lib64;/usr/lib64;/usr/lib")
