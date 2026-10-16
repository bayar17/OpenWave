cmake_minimum_required(VERSION 3.21)

project(OceanLauncher VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)

find_package(Qt6 6.10 REQUIRED COMPONENTS Quick QuickControls2 Svg WaylandCompositor Network)
find_package(OpenWaveSDK REQUIRED COMPONENTS Fonts Applications Wallpaper Time Prompts SystemControls ApplicationData)
qt_standard_project_setup(REQUIRES 6.10)

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/importedcontent/CMakeLists.txt")
    add_subdirectory(importedcontent)
endif()

qt_add_executable(appOceanLauncher main.cpp AppController.cpp AppController.h
    PortalFileChooserBackend.cpp PortalFileChooserBackend.h
    ../OceanFiles/FilesBackend.cpp ../OceanFiles/FilesBackend.h
)

qt_add_qml_module(appOceanLauncher
    URI OceanLauncher
    VERSION 1.0
    IMPORT_PATH "${OpenWaveSDK_QML_IMPORT_PATH}"
    QML_FILES
        Main.qml
        AppIcon.qml
        DrawerAppIcon.qml
        DrawerAppGrid.qml
        IconImage.qml
        SignalIcon.qml
        Chevron.qml
        AppLoading.qml
        FrostedBackground.qml
        PortalBackdrop.qml
        PortalTitle.qml
        RecentAppCard.qml
        RecentAppGrid.qml
        RecentAppTransition.qml
        QuickMenu.qml
        QuickMenuTile.qml
        QuickMenuSlider.qml
        PortalFileChooser.qml
        PromptDialog.qml
        PromptDropdown.qml
        VerticalUnfoldTransition.qml
    RESOURCES
        assets/icons/Files.svg
        assets/icons/Settings.png
        assets/icons/Browser.svg
        assets/status/battery.svg
        assets/status/cellular.svg
        assets/status/wifi.svg
        assets/fonts/DejaVuSans.ttf
        assets/fonts/LICENSE.txt
        assets/fonts/Inter-Regular.otf
        assets/fonts/Inter-Light.otf
        assets/fonts/Inter-LICENSE.txt
        assets/fonts/Inter-Apache-2.0.txt
)

set(PORTAL_ASSET_DIR "${CMAKE_CURRENT_LIST_DIR}/../OceanFiles/assets")
set_source_files_properties("${PORTAL_ASSET_DIR}/folder-outline.svg" PROPERTIES
    QT_RESOURCE_ALIAS "assets/folder-outline.svg")
set_source_files_properties("${PORTAL_ASSET_DIR}/file-outline.svg" PROPERTIES
    QT_RESOURCE_ALIAS "assets/file-outline.svg")
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/assets/portal-image-outline.svg" PROPERTIES
    QT_RESOURCE_ALIAS "assets/image-outline.svg")
qt_add_resources(appOceanLauncher ocean_portal_assets
    PREFIX "/OceanPortal"
    FILES
        "${PORTAL_ASSET_DIR}/folder-outline.svg"
        "${PORTAL_ASSET_DIR}/file-outline.svg"
        "${CMAKE_CURRENT_LIST_DIR}/assets/portal-image-outline.svg")

# The compositor owns the status and Quick Menu artwork. Keep the resource
# URLs stable so the bar stays visible over every application surface.
set(OCEAN_TOPBAR_ASSET_DIR "${CMAKE_CURRENT_LIST_DIR}/assets/topbar")
set(OCEAN_TOPBAR_RESOURCES)
foreach(icon IN ITEMS wifi bluetooth cellular airplane volume brightness)
    set(asset "${OCEAN_TOPBAR_ASSET_DIR}/QuickMenu/${icon}.svg")
    set_source_files_properties("${asset}" PROPERTIES
        QT_RESOURCE_ALIAS "assets/QuickMenu/${icon}.svg")
    list(APPEND OCEAN_TOPBAR_RESOURCES "${asset}")
endforeach()
foreach(level IN ITEMS 0bar 1bar 2bar 3bar fullbar)
    set(base "${OCEAN_TOPBAR_ASSET_DIR}/TopBar/wifi/${level}.svg")
    set(active "${OCEAN_TOPBAR_ASSET_DIR}/TopBar/wifi/${level}-active.svg")
    set_source_files_properties("${base}" PROPERTIES
        QT_RESOURCE_ALIAS "assets/TopBar/wifi/${level}.svg")
    set_source_files_properties("${active}" PROPERTIES
        QT_RESOURCE_ALIAS "assets/TopBar/wifi/${level}-active.svg")
    list(APPEND OCEAN_TOPBAR_RESOURCES "${base}" "${active}")
endforeach()
foreach(level IN ITEMS 0bar 1bar 2bar 3bar 4bar fullbar)
    set(base "${OCEAN_TOPBAR_ASSET_DIR}/TopBar/cellular/${level}.svg")
    set(active "${OCEAN_TOPBAR_ASSET_DIR}/TopBar/cellular/${level}-active.svg")
    set_source_files_properties("${base}" PROPERTIES
        QT_RESOURCE_ALIAS "assets/TopBar/cellular/${level}.svg")
    set_source_files_properties("${active}" PROPERTIES
        QT_RESOURCE_ALIAS "assets/TopBar/cellular/${level}-active.svg")
    list(APPEND OCEAN_TOPBAR_RESOURCES "${base}" "${active}")
endforeach()
qt_add_resources(appOceanLauncher ocean_topbar_assets
    PREFIX "/OceanTopbard"
    FILES ${OCEAN_TOPBAR_RESOURCES})

set_target_properties(appOceanLauncher PROPERTIES
    OUTPUT_NAME ocean-launcher
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/../lib64"
)

target_link_libraries(appOceanLauncher
    PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::WaylandCompositor Qt6::Network
)
target_link_libraries(appOceanLauncher PRIVATE OpenWave::Fonts OpenWave::Applications OpenWave::Wallpaper OpenWave::Time OpenWave::Prompts OpenWave::SystemControls OpenWave::ApplicationData)
if(CMAKE_CROSSCOMPILING)
    target_include_directories(appOceanLauncher PRIVATE
        "${CMAKE_SYSROOT}/usr/include/glib-2.0"
        "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
    target_link_libraries(appOceanLauncher PRIVATE gio-2.0 gobject-2.0 glib-2.0)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
    target_link_libraries(appOceanLauncher PRIVATE PkgConfig::GIO)
endif()


# Qt's embedded FreeType backend also needs a system font directory.
install(FILES assets/fonts/DejaVuSans.ttf DESTINATION ${CMAKE_INSTALL_LIBDIR}/fonts)
install(FILES assets/fonts/LICENSE.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/ocean-launcher/DejaVuSans)
install(FILES assets/fonts/Inter-Regular.otf assets/fonts/Inter-Light.otf
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/fonts)
install(FILES assets/fonts/Inter-LICENSE.txt assets/fonts/Inter-Apache-2.0.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/ocean-launcher/Inter)

option(OPENWAVE_PERMISSION_TESTS "Enable the test-only permission integration driver" OFF)
if(OPENWAVE_PERMISSION_TESTS)
    set_property(SOURCE main.cpp APPEND PROPERTY COMPILE_DEFINITIONS OPENWAVE_PERMISSION_TESTS)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/OpenWaveBundle.cmake")
openwave_install_bundle(appOceanLauncher com.openwave.shell SYSTEM)
