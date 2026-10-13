cmake_minimum_required(VERSION 3.21)
project(OceanSettings VERSION 0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick QuickControls2 Svg)
find_package(OpenWaveSDK REQUIRED COMPONENTS Connectivity Time Wallpaper FileChooser Prompts ApplicationData)
qt_standard_project_setup(REQUIRES 6.10)
qt_add_executable(appOceanSettings main.cpp SettingsBackend.cpp SettingsBackend.h
)
qt_add_qml_module(appOceanSettings
    URI OceanSettings
    VERSION 1.0
    QML_FILES
        Main.qml SettingsPage.qml SettingsRow.qml SettingsSymbol.qml WifiNetworkRow.qml BluetoothDeviceRow.qml
        FrostedBackground.qml
    RESOURCES
        assets/icon.png assets/Pixel.png
        assets/fonts/Inter-Regular.otf assets/fonts/Inter-Light.otf
        assets/fonts/Inter-ExtraLight.otf assets/fonts/Inter-SemiBold.otf
        assets/fonts/Inter-LICENSE.txt assets/fonts/Inter-Apache-2.0.txt
)
set_target_properties(appOceanSettings PROPERTIES
    OUTPUT_NAME ocean-settings
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/../lib64"
)
target_link_libraries(appOceanSettings PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Svg)
target_link_libraries(appOceanSettings PRIVATE OpenWave::Connectivity OpenWave::Time OpenWave::Wallpaper OpenWave::FileChooser OpenWave::Prompts OpenWave::ApplicationData)
if(CMAKE_CROSSCOMPILING)
    target_include_directories(appOceanSettings PRIVATE
        "${CMAKE_SYSROOT}/usr/include/glib-2.0"
        "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
    target_link_libraries(appOceanSettings PRIVATE gio-2.0 gobject-2.0 glib-2.0)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
    target_link_libraries(appOceanSettings PRIVATE PkgConfig::GIO)
endif()
install(TARGETS appOceanSettings
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ocean-settings.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps RENAME ocean-settings.png)
install(FILES assets/fonts/Inter-Regular.otf assets/fonts/Inter-Light.otf
    assets/fonts/Inter-ExtraLight.otf assets/fonts/Inter-SemiBold.otf
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/fonts)
install(FILES assets/fonts/Inter-LICENSE.txt assets/fonts/Inter-Apache-2.0.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/ocean-settings/Inter)
