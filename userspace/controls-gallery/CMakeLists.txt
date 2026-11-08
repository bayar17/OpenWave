cmake_minimum_required(VERSION 3.21)
project(OpenWaveControlsGallery LANGUAGES CXX)
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick QuickControls2)
find_package(OpenWaveSDK REQUIRED COMPONENTS Application Controls Fonts Theme)
qt_standard_project_setup(REQUIRES 6.10)
qt_add_executable(water-rouge-gallery main.cpp)
qt_add_qml_module(water-rouge-gallery
    URI OpenWave.ControlsGallery VERSION 1.0
    IMPORT_PATH "${OpenWaveSDK_QML_IMPORT_PATH}"
    QML_FILES Main.qml)
target_link_libraries(water-rouge-gallery PRIVATE Qt6::Quick Qt6::QuickControls2
    OpenWave::Application OpenWave::Controls OpenWave::Fonts OpenWave::Theme)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/OpenWaveBundle.cmake")
openwave_install_bundle(water-rouge-gallery com.openwave.waterrouge-gallery SYSTEM)
