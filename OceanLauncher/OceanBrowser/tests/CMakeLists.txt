cmake_minimum_required(VERSION 3.21)
project(BrowserDataTests LANGUAGES C CXX)
enable_testing()
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Quick Test Sql Network ShaderTools)
find_package(OpenWaveSDK REQUIRED COMPONENTS FileChooser ApplicationData Applications)
add_executable(browser-data-tests tst_browserdata.cpp ../BrowserData.cpp ../BrowserData.h)
target_link_libraries(browser-data-tests PRIVATE Qt6::Quick Qt6::Test OpenWave::ApplicationData)
add_executable(browser-history-tests tst_history.cpp ../OceanHistoryManager.cpp ../OceanHistoryManager.h)
target_link_libraries(browser-history-tests PRIVATE Qt6::Gui Qt6::Sql Qt6::Network Qt6::Test)
add_executable(address-loading-tests tst_addressloading.cpp)
target_link_libraries(address-loading-tests PRIVATE Qt6::Quick Qt6::Test)
target_compile_definitions(address-loading-tests PRIVATE
    ADDRESS_BORDER_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../AddressLoadingBorder.qml")
qt_add_shaders(address-loading-tests address_loading_test_shader
    PREFIX "/OceanBrowser"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/.."
    FILES ../shaders/address-loading.frag
)
add_test(NAME browser-data COMMAND browser-data-tests)
add_test(NAME browser-history COMMAND browser-history-tests)
add_test(NAME address-loading COMMAND address-loading-tests)
add_executable(search-compatibility-tests tst_searchcompatibility.cpp)
target_link_libraries(search-compatibility-tests PRIVATE Qt6::Qml Qt6::Test)
target_compile_definitions(search-compatibility-tests PRIVATE
    SEARCH_COMPATIBILITY_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../SearchCompatibility.js")
add_test(NAME search-compatibility COMMAND search-compatibility-tests)
set_tests_properties(browser-data browser-history address-loading PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Native WPE integration is tested against OpenWave's own runtime in QEMU.
if(CMAKE_CROSSCOMPILING)
    add_executable(browser-input-tests tst_input.cpp ../BrowserInput.cpp ../BrowserInput.h)
    target_include_directories(browser-input-tests PRIVATE
        "${CMAKE_SYSROOT}/usr/include/libsoup-3.0"
        "${CMAKE_SYSROOT}/usr/include/glib-2.0"
        "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include"
        "${CMAKE_SYSROOT}/usr/include/wpe-webkit-2.0"
        "${CMAKE_SYSROOT}/usr/include/wpe-webkit-2.0/wpe-platform")
    target_link_libraries(browser-input-tests PRIVATE Qt6::Quick Qt6::Test
        gio-2.0 gobject-2.0 glib-2.0 WPEWebKit-2.0)
    set_target_properties(browser-input-tests PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "/usr/lib;/usr/lib64")
    add_test(NAME browser-input COMMAND browser-input-tests)
    add_executable(browser-input-init input_init.c)
    set_target_properties(browser-input-init PROPERTIES AUTOMOC OFF
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "/usr/lib;/usr/lib64")
    target_include_directories(browser-input-init PRIVATE
        "${CMAKE_SYSROOT}/usr/include/glib-2.0"
        "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
    target_link_libraries(browser-input-init PRIVATE gio-2.0 gobject-2.0 glib-2.0)
    add_executable(browser-engine-probe engine_probe.cpp)
    target_include_directories(browser-engine-probe PRIVATE
        "${CMAKE_SYSROOT}/usr/include/libsoup-3.0"
        "${CMAKE_SYSROOT}/usr/include/glib-2.0"
        "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include"
        "${CMAKE_SYSROOT}/usr/include/wpe-webkit-2.0"
        "${CMAKE_SYSROOT}/usr/include/wpe-webkit-2.0/wpe-platform"
        "../WPEWebKit/Source/WebKit/UIProcess/API/wpe/qt6")
    target_link_libraries(browser-engine-probe PRIVATE Qt6::Quick
        gio-2.0 gobject-2.0 glib-2.0 WPEWebKit-2.0
        "${CMAKE_SYSROOT}/usr/lib64/qt6/qml/org/wpewebkit/qtwpe/libqtwpe.so")
    set_target_properties(browser-engine-probe PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "/usr/lib;/usr/lib64;/usr/lib64/qt6/qml/org/wpewebkit/qtwpe")
    add_executable(browser-download-tests tst_downloads.cpp
        ../OceanDownloadManager.cpp ../OceanDownloadManager.h
        ../OceanCookieManager.cpp ../OceanCookieManager.h
        ../BrowserPreferences.cpp ../BrowserPreferences.h)
    target_include_directories(browser-download-tests PRIVATE
        "${CMAKE_SYSROOT}/usr/include/glib-2.0"
        "${CMAKE_SYSROOT}/usr/include/gio-unix-2.0"
        "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include"
        "${CMAKE_SYSROOT}/usr/include/wpe-webkit-2.0"
        "${CMAKE_SYSROOT}/usr/include/wpe-webkit-2.0/wpe-platform"
        "${CMAKE_SYSROOT}/usr/include/wpe-1.0"
        "${CMAKE_SYSROOT}/usr/include/libsoup-3.0")
    target_link_libraries(browser-download-tests PRIVATE OpenWave::FileChooser OpenWave::Applications Qt6::Core Qt6::Sql Qt6::Test Qt6::Network
        gio-2.0 gobject-2.0 glib-2.0 WPEWebKit-2.0)
    set_target_properties(browser-download-tests PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "/usr/lib;/usr/lib64")
    add_test(NAME browser-downloads COMMAND browser-download-tests)
endif()

add_executable(browser-settings-tests tst_settings.cpp ../BrowserPreferences.cpp ../BrowserPreferences.h)
target_link_libraries(browser-settings-tests PRIVATE Qt6::Quick Qt6::Test OpenWave::FileChooser)
target_compile_definitions(browser-settings-tests PRIVATE
    BROWSER_SETTINGS_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/../BrowserSettingsPage.qml")
add_test(NAME browser-settings COMMAND browser-settings-tests)
set_tests_properties(browser-settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
