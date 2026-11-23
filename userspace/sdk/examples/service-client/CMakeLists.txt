cmake_minimum_required(VERSION 3.21)
project(OpenWaveSDKExample LANGUAGES CXX)
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick)
find_package(OpenWaveSDK REQUIRED COMPONENTS Prompts FileChooser Applications Wallpaper Time Connectivity SystemControls ApplicationData)
qt_standard_project_setup(REQUIRES 6.10)
qt_add_executable(openwave-sdk-example main.cpp)
target_link_libraries(openwave-sdk-example PRIVATE Qt6::Quick
    OpenWave::Prompts OpenWave::FileChooser OpenWave::Applications
    OpenWave::Wallpaper OpenWave::Time OpenWave::Connectivity
    OpenWave::SystemControls OpenWave::ApplicationData)
set_target_properties(openwave-sdk-example PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/../lib64")
