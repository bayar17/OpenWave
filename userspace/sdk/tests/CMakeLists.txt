cmake_minimum_required(VERSION 3.21)
project(OpenWaveSDKRuntimeCheck LANGUAGES CXX)
find_package(OpenWaveSDK REQUIRED COMPONENTS Prompts FileChooser Applications Wallpaper Time Connectivity SystemControls ApplicationData)
add_executable(openwave-sdk-probe runtime-check.cpp)
target_link_libraries(openwave-sdk-probe PRIVATE OpenWave::Prompts OpenWave::FileChooser
    OpenWave::Applications OpenWave::Wallpaper OpenWave::Time OpenWave::Connectivity
    OpenWave::SystemControls OpenWave::ApplicationData)
set_target_properties(openwave-sdk-probe PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib64;$ORIGIN/../lib")
