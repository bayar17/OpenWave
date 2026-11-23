cmake_minimum_required(VERSION 3.21)
if(NOT PROJECT_NAME)
    project(OpenWaveQuickMenu LANGUAGES CXX)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Qml Quick)
find_package(OpenWaveSDK REQUIRED COMPONENTS ApplicationData)
qt_standard_project_setup(REQUIRES 6.10)

set_source_files_properties(qml/Applet.qml PROPERTIES QT_RESOURCE_ALIAS Applet.qml)
set_source_files_properties(qml/AppletHost.qml PROPERTIES QT_RESOURCE_ALIAS AppletHost.qml)
set_source_files_properties(qml/AppearancePreview.qml PROPERTIES QT_RESOURCE_ALIAS AppearancePreview.qml)
qt_add_qml_module(openwave-quickmenu
    STATIC
    URI OpenWave.QuickMenu
    VERSION 1.0
    QML_FILES qml/Applet.qml qml/AppletHost.qml qml/AppearancePreview.qml
    SOURCES QuickMenuAppletRegistry.cpp QuickMenuAppletRegistry.h
            QuickMenuAppearance.cpp QuickMenuAppearance.h
            QuickMenuAppletManager.cpp QuickMenuAppletManager.h
            QuickMenuSystem.cpp QuickMenuSystem.h)
target_link_libraries(openwave-quickmenu PUBLIC Qt6::Qml Qt6::Quick OpenWave::ApplicationData)
target_include_directories(openwave-quickmenu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

install(DIRECTORY applets/ DESTINATION /System/QuickMenu/Applets)
