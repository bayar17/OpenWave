cmake_minimum_required(VERSION 3.21)
project(SettingsBluetoothModuleTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick Test)
qt_standard_project_setup(REQUIRES 6.10)
set(settings_sources "${CMAKE_CURRENT_LIST_DIR}/../..")
set_source_files_properties("${settings_sources}/SettingsModuleHost.qml"
    PROPERTIES QT_RESOURCE_ALIAS SettingsModuleHost.qml)
qt_add_qml_module(bluetooth-tests-settings-module
    STATIC
    URI OpenWave.Settings
    VERSION 1.0
    QML_FILES "${settings_sources}/SettingsModuleHost.qml"
    SOURCES
        "${settings_sources}/SettingsModuleRegistry.cpp"
        "${settings_sources}/SettingsModuleRegistry.h"
        "${settings_sources}/SettingsModuleLoader.cpp"
        "${settings_sources}/SettingsModuleLoader.h")
target_link_libraries(bluetooth-tests-settings-module PUBLIC Qt6::Qml Qt6::Quick)
target_include_directories(bluetooth-tests-settings-module PUBLIC "${settings_sources}")
qt_add_executable(settings-bluetooth-module-tests tst_bluetoothmodule.cpp)
target_link_libraries(settings-bluetooth-module-tests PRIVATE
    bluetooth-tests-settings-moduleplugin bluetooth-tests-settings-module Qt6::Test Qt6::Quick Qt6::Gui)
target_compile_definitions(settings-bluetooth-module-tests PRIVATE
    MODULES_DIR="${settings_sources}/resources/Modules"
    STUB_IMPORT_DIR="${CMAKE_CURRENT_LIST_DIR}/../stubs/imports"
    MOCK_DIR="${CMAKE_CURRENT_LIST_DIR}/mocks")
enable_testing()
add_test(NAME settings-bluetooth-module COMMAND settings-bluetooth-module-tests)
set_tests_properties(settings-bluetooth-module PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
