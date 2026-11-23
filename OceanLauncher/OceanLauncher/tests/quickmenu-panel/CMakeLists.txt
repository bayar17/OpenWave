cmake_minimum_required(VERSION 3.21)
project(QuickMenuPanelTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest Test)
qt_standard_project_setup(REQUIRES 6.10)
set(quickmenu_sources "${CMAKE_CURRENT_LIST_DIR}/../../../../userspace/quickmenu")
set(launcher_sources "${CMAKE_CURRENT_LIST_DIR}/../..")
set(header_root "${CMAKE_CURRENT_BINARY_DIR}/include")
file(MAKE_DIRECTORY "${header_root}")
if(NOT EXISTS "${header_root}/OpenWave")
    file(CREATE_LINK "${quickmenu_sources}/.." "${header_root}/OpenWave" SYMBOLIC)
endif()
set_source_files_properties("${quickmenu_sources}/qml/Applet.qml" PROPERTIES QT_RESOURCE_ALIAS Applet.qml)
set_source_files_properties("${quickmenu_sources}/qml/AppletHost.qml" PROPERTIES QT_RESOURCE_ALIAS AppletHost.qml)
set_source_files_properties("${quickmenu_sources}/qml/AppearancePreview.qml" PROPERTIES QT_RESOURCE_ALIAS AppearancePreview.qml)
qt_add_qml_module(quickmenu-panel-core
    STATIC
    URI OpenWave.QuickMenu
    VERSION 1.0
    QML_FILES "${quickmenu_sources}/qml/Applet.qml" "${quickmenu_sources}/qml/AppletHost.qml" "${quickmenu_sources}/qml/AppearancePreview.qml"
    SOURCES
        "${quickmenu_sources}/QuickMenuAppletRegistry.cpp" "${quickmenu_sources}/QuickMenuAppletRegistry.h"
        "${quickmenu_sources}/QuickMenuAppearance.cpp" "${quickmenu_sources}/QuickMenuAppearance.h"
        "${quickmenu_sources}/QuickMenuAppletManager.cpp" "${quickmenu_sources}/QuickMenuAppletManager.h"
        "${quickmenu_sources}/QuickMenuSystem.cpp" "${quickmenu_sources}/QuickMenuSystem.h")
target_link_libraries(quickmenu-panel-core PUBLIC Qt6::Qml Qt6::Quick)
target_include_directories(quickmenu-panel-core PUBLIC "${quickmenu_sources}" "${header_root}")
qt_add_executable(quickmenu-panel-tests "${quickmenu_sources}/tests/main.cpp")
target_link_libraries(quickmenu-panel-tests PRIVATE
    quickmenu-panel-coreplugin quickmenu-panel-core Qt6::QuickTest Qt6::Test Qt6::Quick Qt6::Gui)
target_compile_definitions(quickmenu-panel-tests PRIVATE
    QUICK_TEST_SOURCE_DIR="${launcher_sources}/tests"
    QUICKMENU_SOURCE_DIR="${quickmenu_sources}"
    FIXTURE_DIR="${quickmenu_sources}/tests/fixtures")
enable_testing()
add_test(NAME quickmenu-panel COMMAND quickmenu-panel-tests -input "${launcher_sources}/tests/tst_quickmenu.qml")
add_test(NAME wallpaper-crossfade COMMAND quickmenu-panel-tests -input "${launcher_sources}/tests/tst_wallpaper_crossfade.qml")
add_test(NAME appearance-notice COMMAND quickmenu-panel-tests -input "${launcher_sources}/tests/tst_appearance_notice.qml")
set_tests_properties(quickmenu-panel wallpaper-crossfade appearance-notice PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
