cmake_minimum_required(VERSION 3.21)
project(HelloBundle VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick)
find_package(OpenWaveSDK REQUIRED)
qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(hello main.cpp)
qt_add_qml_module(hello URI Hello VERSION 1.0 QML_FILES Main.qml)
target_link_libraries(hello PRIVATE Qt6::Quick)

include("${OpenWaveSDK_DIR}/OpenWaveBundle.cmake")
openwave_install_bundle(hello org.example.hello)
install(FILES share/icons/icon.svg DESTINATION /Applications/org.example.hello/share/icons)
