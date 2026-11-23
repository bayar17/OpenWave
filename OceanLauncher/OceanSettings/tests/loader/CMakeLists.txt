cmake_minimum_required(VERSION 3.21)
project(SettingsModuleLoaderTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick Test)
qt_standard_project_setup(REQUIRES 6.10)
set(settings_sources "${CMAKE_CURRENT_LIST_DIR}/../..")
set_source_files_properties("${settings_sources}/SettingsModuleHost.qml"
    PROPERTIES QT_RESOURCE_ALIAS SettingsModuleHost.qml)
qt_add_qml_module(loader-under-test
    STATIC
    URI OpenWave.Settings
    VERSION 1.0
    QML_FILES "${settings_sources}/SettingsModuleHost.qml"
    SOURCES
        "${settings_sources}/SettingsModuleRegistry.cpp"
        "${settings_sources}/SettingsModuleRegistry.h"
        "${settings_sources}/SettingsModuleLoader.cpp"
        "${settings_sources}/SettingsModuleLoader.h")
target_link_libraries(loader-under-test PUBLIC Qt6::Qml Qt6::Quick)
target_include_directories(loader-under-test PUBLIC "${settings_sources}")
qt_add_executable(settings-module-loader-tests tst_settingsmoduleloader.cpp)
target_link_libraries(settings-module-loader-tests PRIVATE
    loader-under-testplugin loader-under-test Qt6::Test Qt6::Quick Qt6::Gui)
enable_testing()
add_test(NAME settings-module-loader COMMAND settings-module-loader-tests)
set_tests_properties(settings-module-loader PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
