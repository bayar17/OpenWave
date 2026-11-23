cmake_minimum_required(VERSION 3.21)
project(QuickMenuTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick QuickTest Test)
qt_standard_project_setup(REQUIRES 6.10)
set(quickmenu_sources "${CMAKE_CURRENT_LIST_DIR}/..")
set(header_root "${CMAKE_CURRENT_BINARY_DIR}/include")
file(MAKE_DIRECTORY "${header_root}")
if(NOT EXISTS "${header_root}/OpenWave")
    file(CREATE_LINK "${quickmenu_sources}/.." "${header_root}/OpenWave" SYMBOLIC)
endif()
set_source_files_properties("${quickmenu_sources}/qml/Applet.qml" PROPERTIES QT_RESOURCE_ALIAS Applet.qml)
set_source_files_properties("${quickmenu_sources}/qml/AppletHost.qml" PROPERTIES QT_RESOURCE_ALIAS AppletHost.qml)
set_source_files_properties("${quickmenu_sources}/qml/AppearancePreview.qml" PROPERTIES QT_RESOURCE_ALIAS AppearancePreview.qml)
qt_add_qml_module(quickmenu-under-test
    STATIC
    URI OpenWave.QuickMenu
    VERSION 1.0
    QML_FILES "${quickmenu_sources}/qml/Applet.qml" "${quickmenu_sources}/qml/AppletHost.qml" "${quickmenu_sources}/qml/AppearancePreview.qml"
    SOURCES
        "${quickmenu_sources}/QuickMenuAppletRegistry.cpp" "${quickmenu_sources}/QuickMenuAppletRegistry.h"
        "${quickmenu_sources}/QuickMenuAppearance.cpp" "${quickmenu_sources}/QuickMenuAppearance.h"
        "${quickmenu_sources}/QuickMenuAppletManager.cpp" "${quickmenu_sources}/QuickMenuAppletManager.h"
        "${quickmenu_sources}/QuickMenuSystem.cpp" "${quickmenu_sources}/QuickMenuSystem.h")
target_link_libraries(quickmenu-under-test PUBLIC Qt6::Qml Qt6::Quick)
target_include_directories(quickmenu-under-test PUBLIC "${quickmenu_sources}" "${header_root}")

qt_add_executable(quickmenu-cpp-tests tst_quickmenu.cpp)
target_link_libraries(quickmenu-cpp-tests PRIVATE
    quickmenu-under-testplugin quickmenu-under-test Qt6::Test Qt6::Quick Qt6::Gui)
target_compile_definitions(quickmenu-cpp-tests PRIVATE QUICKMENU_SOURCE_DIR="${quickmenu_sources}")

qt_add_executable(quickmenu-qml-tests main.cpp)
target_link_libraries(quickmenu-qml-tests PRIVATE
    quickmenu-under-testplugin quickmenu-under-test Qt6::QuickTest Qt6::Test Qt6::Quick Qt6::Gui)
target_compile_definitions(quickmenu-qml-tests PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}/qml"
    QUICKMENU_SOURCE_DIR="${quickmenu_sources}"
    FIXTURE_DIR="${CMAKE_CURRENT_LIST_DIR}/fixtures")

enable_testing()
add_test(NAME quickmenu-cpp COMMAND quickmenu-cpp-tests)
add_test(NAME quickmenu-qml COMMAND quickmenu-qml-tests)
set_tests_properties(quickmenu-cpp quickmenu-qml PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
