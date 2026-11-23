cmake_minimum_required(VERSION 3.21)
project(OceanSettings VERSION 0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)
find_package(Qt6 6.10 REQUIRED COMPONENTS Qml Quick QuickControls2 Svg)
find_package(OpenWaveSDK REQUIRED COMPONENTS Application Controls Fonts Theme Connectivity Time Wallpaper FileChooser Prompts ApplicationData Permissions Applications Identity)
qt_standard_project_setup(REQUIRES 6.10)
if(NOT TARGET openwave-quickmenu)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../userspace/quickmenu" quickmenu)
endif()
qt_add_qml_module(openwave-settings-registry
    STATIC
    URI OpenWave.Settings
    VERSION 1.0
    QML_FILES SettingsModuleHost.qml
    SOURCES SettingsModuleRegistry.cpp SettingsModuleRegistry.h
            SettingsModuleLoader.cpp SettingsModuleLoader.h)
target_link_libraries(openwave-settings-registry PUBLIC Qt6::Qml Qt6::Quick)
target_include_directories(openwave-settings-registry PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
qt_add_executable(appOceanSettings main.cpp SettingsBackend.cpp SettingsBackend.h ApplicationsBackend.cpp ApplicationsBackend.h
    SettingsEnvironment.cpp SettingsEnvironment.h SettingsGeneralService.cpp SettingsGeneralService.h
    AppearanceManager.cpp AppearanceManager.h PreferenceBroker.cpp PreferenceBroker.h
)
qt_add_qml_module(appOceanSettings
    URI OceanSettings
    VERSION 1.0
    IMPORT_PATH "${OpenWaveSDK_QML_IMPORT_PATH}"
    QML_FILES
        Main.qml SettingsPage.qml AppearancePage.qml SubpageHeader.qml QuickMenuPage.qml QuickMenuAppletRow.qml AppearanceCard.qml AppearanceMenu.qml SettingsRow.qml SettingsSymbol.qml AccentPaletteCard.qml
        FrostedBackground.qml PermissionsPage.qml AppDefaultsPage.qml DefaultAppsPage.qml AppsPage.qml
    RESOURCES
        assets/icon.png assets/Pixel.png
)
set_target_properties(appOceanSettings PROPERTIES
    OUTPUT_NAME ocean-settings
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/../lib64"
)
target_link_libraries(appOceanSettings PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Svg openwave-settings-registryplugin openwave-quickmenuplugin openwave-quickmenu)
target_link_libraries(appOceanSettings PRIVATE OpenWave::Application OpenWave::Controls OpenWave::Fonts OpenWave::Theme OpenWave::Connectivity OpenWave::Time OpenWave::Wallpaper OpenWave::FileChooser OpenWave::Prompts OpenWave::ApplicationData OpenWave::Permissions OpenWave::Applications OpenWave::Identity)
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
install(FILES ocean-settings.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps RENAME ocean-settings.png)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/OpenWaveBundle.cmake")
openwave_install_bundle(appOceanSettings com.openwave.settings SYSTEM)
install(FILES assets/icon.png DESTINATION /System/Applications/com.openwave.settings/share/icons)
install(DIRECTORY resources/Modules/ DESTINATION /System/Applications/com.openwave.settings/resources/Modules)
