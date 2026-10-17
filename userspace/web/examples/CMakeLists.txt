cmake_minimum_required(VERSION 3.21)
project(OpenWaveWebExample LANGUAGES CXX)
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick)
find_package(OpenWaveSDK REQUIRED COMPONENTS Web)
qt_standard_project_setup(REQUIRES 6.10)
qt_add_executable(openwave-web-example main.cpp)
qt_add_qml_module(openwave-web-example
    URI WebExample VERSION 1.0
    IMPORT_PATH ${OpenWaveSDK_QML_IMPORT_PATH}
    QML_FILES Main.qml)
target_link_libraries(openwave-web-example PRIVATE Qt6::Quick OpenWave::Web)
set_target_properties(openwave-web-example PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/../lib64;/usr/lib64;/usr/lib")
