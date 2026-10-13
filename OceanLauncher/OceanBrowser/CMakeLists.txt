cmake_minimum_required(VERSION 3.21)
project(OceanBrowser VERSION 0.1 LANGUAGES CXX)

include(GNUInstallDirs)
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick QuickControls2 Svg Sql Network ShaderTools)
find_package(OpenWaveSDK REQUIRED COMPONENTS Applications Wallpaper FileChooser ApplicationData)
qt_standard_project_setup(REQUIRES 6.10)

qt_add_executable(appOceanBrowser main.cpp BrowserInput.cpp BrowserInput.h BrowserData.cpp BrowserData.h BrowserPreferences.cpp BrowserPreferences.h OceanHistoryManager.cpp OceanHistoryManager.h
    OceanDownloadManager.cpp OceanDownloadManager.h OceanCookieManager.cpp OceanCookieManager.h
)
qt_add_qml_module(appOceanBrowser
    URI OceanBrowser
    VERSION 1.0
    QML_FILES Main.qml BrowserSettingsPage.qml BrowserSwitch.qml BrowserGlyph.qml HistoryPage.qml DownloadsPage.qml AddressLoadingBorder.qml SearchCompatibility.js
    RESOURCES
        assets/Icon.svg assets/globe.svg
        assets/fonts/SourceSansPro-ExtraLight.otf
)
qt_add_shaders(appOceanBrowser address_loading_shader
    PREFIX "/OceanBrowser"
    FILES shaders/address-loading.frag
)
set_target_properties(appOceanBrowser PROPERTIES
    OUTPUT_NAME ocean-browser
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;$ORIGIN/../lib64"
)
target_link_libraries(appOceanBrowser PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::Sql Qt6::Network)
target_link_libraries(appOceanBrowser PRIVATE OpenWave::Applications OpenWave::Wallpaper OpenWave::FileChooser OpenWave::ApplicationData)
if(CMAKE_CROSSCOMPILING)
    target_include_directories(appOceanBrowser PRIVATE
        "${CMAKE_SYSROOT}/usr/include/glib-2.0"
        "${CMAKE_SYSROOT}/usr/include/gio-unix-2.0"
        "${CMAKE_SYSROOT}/usr/include/wpe-webkit-2.0"
        "${CMAKE_SYSROOT}/usr/include/wpe-1.0"
        "${CMAKE_SYSROOT}/usr/include/wpe-webkit-2.0/wpe-platform"
        "${CMAKE_SYSROOT}/usr/include/libsoup-3.0"
        "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
    target_link_libraries(appOceanBrowser PRIVATE gio-2.0 gobject-2.0 glib-2.0 WPEWebKit-2.0)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
    pkg_check_modules(WPE REQUIRED IMPORTED_TARGET wpe-webkit-2.0)
    pkg_check_modules(GIO_UNIX REQUIRED IMPORTED_TARGET gio-unix-2.0)
    target_link_libraries(appOceanBrowser PRIVATE PkgConfig::GIO PkgConfig::GIO_UNIX PkgConfig::WPE)
    target_compile_definitions(appOceanBrowser PRIVATE
        OCEAN_BROWSER_SOURCE_ABOUT_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/about\")
    target_compile_definitions(appOceanBrowser PRIVATE
        OCEAN_BROWSER_DESKTOP_FILE=\"${CMAKE_CURRENT_SOURCE_DIR}/ocean-browser.desktop\")
endif()
install(DIRECTORY about/ DESTINATION ${CMAKE_INSTALL_DATADIR}/ocean-browser/about
    FILES_MATCHING PATTERN "*.html")
install(TARGETS appOceanBrowser RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Do not advertise a nonfunctional browser to the launcher before WPE is installed.
if(EXISTS "${CMAKE_SYSROOT}/usr/lib64/qt6/qml/org/wpewebkit/qtwpe/libqtwpe.so")
    install(FILES ocean-browser.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/Icon.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps
        RENAME ocean-browser.svg)
endif()
install(FILES assets/fonts/SourceSansPro-OFL.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/ocean-browser)
