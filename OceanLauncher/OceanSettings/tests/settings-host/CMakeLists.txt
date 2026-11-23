cmake_minimum_required(VERSION 3.21)
project(SettingsHostTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest Test)
qt_standard_project_setup(REQUIRES 6.10)
set(settings_sources "${CMAKE_CURRENT_LIST_DIR}/../..")
set_source_files_properties("${settings_sources}/SettingsModuleHost.qml"
    PROPERTIES QT_RESOURCE_ALIAS SettingsModuleHost.qml)
qt_add_qml_module(host-tests-settings-module
    STATIC
    URI OpenWave.Settings
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/OpenWave/Settings"
    QML_FILES "${settings_sources}/SettingsModuleHost.qml"
    SOURCES
        "${settings_sources}/SettingsModuleRegistry.cpp"
        "${settings_sources}/SettingsModuleRegistry.h"
        "${settings_sources}/SettingsModuleLoader.cpp"
        "${settings_sources}/SettingsModuleLoader.h")
target_link_libraries(host-tests-settings-module PUBLIC Qt6::Qml Qt6::Quick)
target_include_directories(host-tests-settings-module PUBLIC "${settings_sources}")
set(quickmenu_sources "${CMAKE_CURRENT_LIST_DIR}/../../../../userspace/quickmenu")
set(header_root "${CMAKE_CURRENT_BINARY_DIR}/include")
file(MAKE_DIRECTORY "${header_root}")
if(NOT EXISTS "${header_root}/OpenWave")
    file(CREATE_LINK "${quickmenu_sources}/.." "${header_root}/OpenWave" SYMBOLIC)
endif()
set_source_files_properties("${quickmenu_sources}/qml/Applet.qml" PROPERTIES QT_RESOURCE_ALIAS Applet.qml)
set_source_files_properties("${quickmenu_sources}/qml/AppletHost.qml" PROPERTIES QT_RESOURCE_ALIAS AppletHost.qml)
set_source_files_properties("${quickmenu_sources}/qml/AppearancePreview.qml" PROPERTIES QT_RESOURCE_ALIAS AppearancePreview.qml)
qt_add_qml_module(host-tests-quickmenu
    STATIC
    URI OpenWave.QuickMenu
    VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/OpenWave/QuickMenu"
    QML_FILES "${quickmenu_sources}/qml/Applet.qml" "${quickmenu_sources}/qml/AppletHost.qml" "${quickmenu_sources}/qml/AppearancePreview.qml"
    SOURCES
        "${quickmenu_sources}/QuickMenuAppletRegistry.cpp" "${quickmenu_sources}/QuickMenuAppletRegistry.h"
        "${quickmenu_sources}/QuickMenuAppearance.cpp" "${quickmenu_sources}/QuickMenuAppearance.h"
        "${quickmenu_sources}/QuickMenuAppletManager.cpp" "${quickmenu_sources}/QuickMenuAppletManager.h"
        "${quickmenu_sources}/QuickMenuSystem.cpp" "${quickmenu_sources}/QuickMenuSystem.h")
target_link_libraries(host-tests-quickmenu PUBLIC Qt6::Qml Qt6::Quick)
target_include_directories(host-tests-quickmenu PUBLIC "${quickmenu_sources}" "${header_root}")
qt_add_executable(settings-host-tests main.cpp)
target_link_libraries(settings-host-tests PRIVATE
    host-tests-settings-moduleplugin host-tests-settings-module host-tests-quickmenuplugin host-tests-quickmenu
    Qt6::QuickTest Qt6::Test Qt6::Quick Qt6::Gui)
target_compile_definitions(settings-host-tests PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}/.."
    STUB_IMPORT_DIR="${CMAKE_CURRENT_LIST_DIR}/../stubs/imports"
    MODULES_DIR="${settings_sources}/resources/Modules")
enable_testing()
add_test(NAME settings-host COMMAND settings-host-tests)
set_tests_properties(settings-host PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
