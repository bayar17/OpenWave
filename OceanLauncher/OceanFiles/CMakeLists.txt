cmake_minimum_required(VERSION 3.21)
project(OceanFiles VERSION 0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(GNUInstallDirs)
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick QuickControls2 Svg Network)
find_package(mpg123 CONFIG REQUIRED)
if(CMAKE_CROSSCOMPILING)
    find_library(PIPEWIRE_LIBRARY NAMES pipewire-0.3
        PATHS "${CMAKE_SYSROOT}/usr/lib64" NO_DEFAULT_PATH REQUIRED)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
endif()
find_package(OpenWaveSDK REQUIRED COMPONENTS Fonts Applications Wallpaper Prompts ApplicationData)
qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(appOceanFiles main.cpp FilesBackend.cpp FilesBackend.h
    MediaPlayerBackend.cpp MediaPlayerBackend.h
)
qt_add_qml_module(appOceanFiles
    URI OceanFiles
    VERSION 1.0
    IMPORT_PATH "${OpenWaveSDK_QML_IMPORT_PATH}"
    QML_FILES Main.qml FrostedBackground.qml
    RESOURCES
        assets/icon.svg
        assets/folder-outline.svg assets/file-outline.svg
        assets/search.svg assets/chevron.svg assets/music-outline.svg
        assets/fonts/Inter-Regular.otf assets/fonts/Inter-Light.otf
        assets/fonts/Inter-SemiBold.otf
)
set_target_properties(appOceanFiles PROPERTIES
    OUTPUT_NAME ocean-files
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/../lib64"
)
target_link_libraries(appOceanFiles PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::Network
    MPG123::libmpg123)
target_link_libraries(appOceanFiles PRIVATE OpenWave::Fonts OpenWave::Applications OpenWave::Wallpaper OpenWave::Prompts OpenWave::ApplicationData)
if(CMAKE_CROSSCOMPILING)
    target_include_directories(appOceanFiles PRIVATE
        "${CMAKE_SYSROOT}/usr/include/pipewire-0.3"
        "${CMAKE_SYSROOT}/usr/include/spa-0.2"
        "${CMAKE_SYSROOT}/usr/include/glib-2.0"
        "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
    target_link_libraries(appOceanFiles PRIVATE "${PIPEWIRE_LIBRARY}" gio-2.0 gobject-2.0 glib-2.0)
else()
    target_link_libraries(appOceanFiles PRIVATE PkgConfig::PIPEWIRE)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
    target_link_libraries(appOceanFiles PRIVATE PkgConfig::GIO)
endif()
install(FILES ocean-files.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES assets/icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps RENAME ocean-files.svg)
install(FILES assets/fonts/Inter-Regular.otf assets/fonts/Inter-Light.otf
    assets/fonts/Inter-SemiBold.otf
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/fonts)
install(FILES ../../third_party/mpg123-1.33.7/COPYING
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/ocean-files/mpg123)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/OpenWaveBundle.cmake")
openwave_install_bundle(appOceanFiles com.openwave.files SYSTEM)
install(FILES assets/icon.svg DESTINATION /System/Applications/com.openwave.files/resources)
