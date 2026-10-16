cmake_minimum_required(VERSION 3.21)
project(OpenWaveFontTests LANGUAGES CXX)
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick Test QuickTest)
find_package(OpenWaveSDK REQUIRED COMPONENTS Fonts)
qt_standard_project_setup(REQUIRES 6.10)
add_executable(openwave-font-tests tst_fonts.cpp)
target_link_libraries(openwave-font-tests PRIVATE OpenWave::Fonts Qt6::Quick Qt6::Test)
target_compile_definitions(openwave-font-tests PRIVATE
    OPENWAVE_FONT_IMPORT_PATH="${OpenWaveSDK_QML_IMPORT_PATH}")
set_target_properties(openwave-font-tests PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib64;$ORIGIN/../lib")
add_executable(openwave-font-qml-tests qml_runner.cpp)
target_link_libraries(openwave-font-qml-tests PRIVATE OpenWave::Fonts Qt6::QuickTest)
target_compile_definitions(openwave-font-qml-tests PRIVATE
    OPENWAVE_FONT_IMPORT_PATH="${OpenWaveSDK_QML_IMPORT_PATH}")
set_target_properties(openwave-font-qml-tests PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib64;$ORIGIN/../lib")
