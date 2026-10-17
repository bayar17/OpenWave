cmake_minimum_required(VERSION 3.21)
project(OpenWaveSDK VERSION 0.1.0 LANGUAGES C CXX)
include(CMakePackageConfigHelpers)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Network Qml)
qt_standard_project_setup(REQUIRES 6.10)
find_package(LibXml2 REQUIRED)
if(NOT OPENWAVE_TARGET_TRIPLE)
    set(OPENWAVE_TARGET_TRIPLE "${CMAKE_CXX_COMPILER_TARGET}")
endif()
if(NOT OPENWAVE_TARGET_TRIPLE)
    message(FATAL_ERROR "Specify OPENWAVE_TARGET_TRIPLE for this target SDK")
endif()
set(api_libdir "sysroot/${OPENWAVE_TARGET_TRIPLE}/usr/lib64")
set(api_cmakedir "lib/cmake/OpenWaveSDK/${OPENWAVE_TARGET_TRIPLE}")
set(api_configdir "lib/cmake/OpenWaveSDK")
set(api_includedir "sysroot/${OPENWAVE_TARGET_TRIPLE}/usr/include")
set(clients "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(openwave-xml SHARED "${clients}/xml/OpenWaveXml.c")
add_library(OpenWave::Xml ALIAS openwave-xml)
set_target_properties(openwave-xml PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0
    EXPORT_NAME Xml AUTOMOC OFF C_STANDARD 11 BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib;$ORIGIN/../lib64")
target_compile_options(openwave-xml PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
target_include_directories(openwave-xml PUBLIC
    "$<BUILD_INTERFACE:${clients}/xml>"
    "$<BUILD_INTERFACE:${CMAKE_SYSROOT}/usr/include/glib-2.0>"
    "$<BUILD_INTERFACE:${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include>"
    "$<INSTALL_INTERFACE:${api_includedir}>"
    "$<INSTALL_INTERFACE:${api_includedir}/glib-2.0>"
    "$<INSTALL_INTERFACE:sysroot/${OPENWAVE_TARGET_TRIPLE}/usr/lib64/glib-2.0/include>")
target_link_libraries(openwave-xml PUBLIC gio-2.0 gobject-2.0 glib-2.0 PRIVATE LibXml2::LibXml2)
install(TARGETS openwave-xml EXPORT OpenWaveSDKTargets LIBRARY DESTINATION "${api_libdir}")
install(FILES "${clients}/xml/OpenWaveXml.h" DESTINATION include/OpenWave)
add_library(openwave-identity SHARED "${clients}/security/OpenWaveIdentity.c")
add_library(OpenWave::Identity ALIAS openwave-identity)
set_target_properties(openwave-identity PROPERTIES EXPORT_NAME Identity VERSION ${PROJECT_VERSION} SOVERSION 0 AUTOMOC OFF
    BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib;$ORIGIN/../lib64")
target_link_libraries(openwave-identity PUBLIC openwave-xml)
target_include_directories(openwave-identity PUBLIC "$<BUILD_INTERFACE:${clients}/security>")
install(TARGETS openwave-identity EXPORT OpenWaveSDKTargets LIBRARY DESTINATION "${api_libdir}")
install(FILES "${clients}/security/OpenWaveIdentity.h" DESTINATION include/OpenWave)
install(DIRECTORY "${clients}/dbus/interfaces/"
    DESTINATION "sysroot/${OPENWAVE_TARGET_TRIPLE}/usr/share/dbus-1/interfaces"
    FILES_MATCHING PATTERN "*.xml")

function(openwave_client component name)
    add_library(${name} SHARED ${ARGN})
    add_library(OpenWave::${component} ALIAS ${name})
    set_target_properties(${name} PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 0
        EXPORT_NAME ${component} BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib;$ORIGIN/../lib64")
    target_compile_features(${name} PUBLIC cxx_std_17)
    target_include_directories(${name} PUBLIC
        "$<INSTALL_INTERFACE:${api_includedir}>" "$<INSTALL_INTERFACE:${api_includedir}/OpenWave>")
    target_link_libraries(${name} PUBLIC Qt6::Core)
    install(TARGETS ${name} EXPORT OpenWaveSDKTargets
        LIBRARY DESTINATION "${api_libdir}")
endfunction()
function(openwave_bus name)
    target_include_directories(${name} PRIVATE
        "${CMAKE_SYSROOT}/usr/include/glib-2.0"
        "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
    target_link_libraries(${name} PRIVATE gio-2.0 gobject-2.0 glib-2.0)
endfunction()
openwave_client(Fonts openwave-fonts
    "${clients}/fonts/FontManager.cpp" "${clients}/fonts/FontManager.h")
target_include_directories(openwave-fonts PUBLIC "$<BUILD_INTERFACE:${clients}/fonts>")
target_link_libraries(openwave-fonts PUBLIC Qt6::Gui Qt6::Qml)
qt_add_qml_module(openwave-fonts
    URI OpenWave.Fonts VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/OpenWave/Fonts")
set(api_qmldir "sysroot/${OPENWAVE_TARGET_TRIPLE}/usr/qml/OpenWave/Fonts")
set_target_properties(openwave-fontsplugin PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../../../lib64;$ORIGIN/../../../lib")
install(TARGETS openwave-fontsplugin LIBRARY DESTINATION "${api_qmldir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qml/OpenWave/Fonts/qmldir"
    "${CMAKE_CURRENT_BINARY_DIR}/qml/OpenWave/Fonts/openwave-fonts.qmltypes"
    DESTINATION "${api_qmldir}")
install(FILES "${clients}/fonts/FontManager.h" DESTINATION include/OpenWave)
install(DIRECTORY "${clients}/fonts/assets/"
    DESTINATION "sysroot/${OPENWAVE_TARGET_TRIPLE}/usr/share/fonts/openwave/SourceSansPro"
    FILES_MATCHING PATTERN "*.otf")
install(FILES "${clients}/fonts/assets/OFL.txt"
    DESTINATION "sysroot/${OPENWAVE_TARGET_TRIPLE}/usr/share/licenses/SourceSansPro")
set(openwave_has_wpe OFF)
if(EXISTS "${CMAKE_SYSROOT}/usr/lib64/qt6/qml/org/wpewebkit/qtwpe/libqtwpe.so")
    set(openwave_has_wpe ON)
endif()
option(OPENWAVE_BUILD_WEB "Build the WPE-backed Qt Quick WebView" ${openwave_has_wpe})
if(OPENWAVE_BUILD_WEB)
    if(NOT openwave_has_wpe)
        message(FATAL_ERROR "OpenWave Web requires the target WPE Qt Quick plugin; build browser dependencies first")
    endif()
    find_package(Qt6 6.10 REQUIRED COMPONENTS Quick)
    openwave_client(Web openwave-web
        "${clients}/web/WebRuntime.cpp" "${clients}/web/WebRuntime.h"
        "${clients}/web/WebInput.cpp" "${clients}/web/WebInput.h"
        "${clients}/web/WebsiteIconFetcher.cpp" "${clients}/web/WebsiteIconFetcher.h")
    openwave_bus(openwave-web)
    target_include_directories(openwave-web PUBLIC "$<BUILD_INTERFACE:${clients}/web>")
    target_include_directories(openwave-web PRIVATE
        "${CMAKE_SYSROOT}/usr/include/wpe-webkit-2.0"
        "${CMAKE_SYSROOT}/usr/include/wpe-webkit-2.0/wpe-platform"
        "${CMAKE_SYSROOT}/usr/include/libsoup-3.0")
    target_link_libraries(openwave-web PUBLIC Qt6::Quick Qt6::Network PRIVATE WPEWebKit-2.0)
    set_source_files_properties("${clients}/web/WebView.qml" PROPERTIES QT_RESOURCE_ALIAS WebView.qml)
    qt_add_qml_module(openwave-web
        URI OpenWave.Web VERSION 1.0
        OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/OpenWave/Web"
        IMPORT_PATH "${CMAKE_SYSROOT}/usr/lib64/qt6/qml"
        QML_FILES "${clients}/web/WebView.qml")
    set(web_qmldir "sysroot/${OPENWAVE_TARGET_TRIPLE}/usr/qml/OpenWave/Web")
    set_target_properties(openwave-webplugin PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/../../../lib64;$ORIGIN/../../../lib")
    install(TARGETS openwave-webplugin LIBRARY DESTINATION "${web_qmldir}")
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/qml/OpenWave/Web/qmldir"
        "${CMAKE_CURRENT_BINARY_DIR}/qml/OpenWave/Web/openwave-web.qmltypes"
        "${clients}/web/WebView.qml"
        DESTINATION "${web_qmldir}")
    install(FILES "${clients}/web/WebRuntime.h" "${clients}/web/WebInput.h" "${clients}/web/WebsiteIconFetcher.h" DESTINATION include/OpenWave)
endif()
openwave_client(Prompts openwave-prompts
    "${clients}/prompt-client/OpenWavePromptClient.cpp"
    "${clients}/prompt-client/OpenWavePromptClient.h"
    "${clients}/prompt-client/PromptTransport.cpp"
    "${clients}/prompt-client/PromptTransport.h"
    "${clients}/prompt-client/PromptRendererBackend.cpp"
    "${clients}/prompt-client/PromptRendererBackend.h")
openwave_bus(openwave-prompts)
target_link_libraries(openwave-prompts PRIVATE openwave-identity)
openwave_client(Permissions openwave-permissions
    "${clients}/permission-client/PermissionClient.cpp" "${clients}/permission-client/PermissionClient.h")
openwave_bus(openwave-permissions)
target_link_libraries(openwave-permissions PRIVATE openwave-identity)
install(FILES "${clients}/permission-client/PermissionClient.h" DESTINATION include/OpenWave)
openwave_client(FileChooser openwave-filechooser
    "${clients}/portal-client/OpenWavePortals.cpp" "${clients}/portal-client/OpenWavePortals.h")
openwave_bus(openwave-filechooser)
target_link_libraries(openwave-filechooser PRIVATE openwave-identity)
target_include_directories(openwave-filechooser PRIVATE "${CMAKE_SYSROOT}/usr/include/gio-unix-2.0")
openwave_client(Applications openwave-applications
    "${clients}/application-client/ApplicationChannel.cpp" "${clients}/application-client/ApplicationChannel.h"
    "${clients}/application-client/ApplicationsClient.cpp" "${clients}/application-client/ApplicationsClient.h")
openwave_bus(openwave-applications)
target_link_libraries(openwave-applications PRIVATE openwave-identity)
target_link_libraries(openwave-applications PUBLIC Qt6::Network)
openwave_client(Wallpaper openwave-wallpaper
    "${clients}/wallpaper-client/WallpaperClient.cpp" "${clients}/wallpaper-client/WallpaperClient.h")
openwave_bus(openwave-wallpaper)
target_link_libraries(openwave-wallpaper PUBLIC Qt6::Gui)
openwave_client(Time openwave-time
    "${clients}/time-client/TimeBackend.cpp" "${clients}/time-client/TimeBackend.h")
openwave_bus(openwave-time)
openwave_client(Connectivity openwave-connectivity
    "${clients}/../OceanLauncher/OceanSettings/ConnectivityBackend.cpp"
    "${clients}/../OceanLauncher/OceanSettings/ConnectivityBackend.h")
openwave_bus(openwave-connectivity)
openwave_client(SystemControls openwave-system-controls
    "${clients}/../OceanLauncher/OceanLauncher/QuickMenuBackend.cpp"
    "${clients}/../OceanLauncher/OceanLauncher/QuickMenuBackend.h")
openwave_bus(openwave-system-controls)
add_library(openwave-application-services INTERFACE)
set_target_properties(openwave-application-services PROPERTIES EXPORT_NAME ApplicationServices)
target_link_libraries(openwave-application-services INTERFACE openwave-applications openwave-permissions openwave-filechooser openwave-prompts)
install(TARGETS openwave-application-services EXPORT OpenWaveSDKTargets)
install(FILES "${clients}/application-services/ApplicationServices.h" DESTINATION include/OpenWave)
add_library(openwave-application-data INTERFACE)
set_target_properties(openwave-application-data PROPERTIES EXPORT_NAME ApplicationData)
target_link_libraries(openwave-application-data INTERFACE Qt6::Gui)
target_include_directories(openwave-application-data INTERFACE
    "$<INSTALL_INTERFACE:${api_includedir}>" "$<INSTALL_INTERFACE:${api_includedir}/OpenWave>")
install(TARGETS openwave-application-data EXPORT OpenWaveSDKTargets)
install(FILES
    "${clients}/prompt-client/OpenWavePromptClient.h" "${clients}/prompt-client/PromptTransport.h"
    "${clients}/prompt-client/PromptRendererBackend.h" "${clients}/portal-client/OpenWavePortals.h"
    "${clients}/application-client/ApplicationChannel.h" "${clients}/application-client/ApplicationsClient.h"
    "${clients}/wallpaper-client/WallpaperClient.h"
    "${clients}/time-client/TimeBackend.h" "${clients}/application-data.h" "${clients}/preferences.h"
    "${clients}/../OceanLauncher/OceanSettings/ConnectivityBackend.h"
    "${clients}/../OceanLauncher/OceanLauncher/QuickMenuBackend.h"
    DESTINATION include/OpenWave)
# Expose the shared public headers through the target include root. Adding the
# top-level SDK/include to a target's search path would also expose host Qt.
install(CODE "
    file(MAKE_DIRECTORY \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${api_includedir}\")
    file(CREATE_LINK \"../../../../include/OpenWave\"
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${api_includedir}/OpenWave\"
        SYMBOLIC RESULT openwave_header_link_result)
    if(NOT openwave_header_link_result STREQUAL \"0\")
        message(FATAL_ERROR \"Cannot expose target API headers: \${openwave_header_link_result}\")
    endif()
")
install(EXPORT OpenWaveSDKTargets NAMESPACE OpenWave:: DESTINATION "${api_cmakedir}")
configure_package_config_file(OpenWaveSDKConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/OpenWaveSDKConfig.cmake" INSTALL_DESTINATION "${api_configdir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/OpenWaveSDKConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/OpenWaveSDKConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/OpenWaveSDKConfigVersion.cmake" DESTINATION "${api_configdir}")

install(FILES "${clients}/application-id.h" DESTINATION include/OpenWave)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../cmake/OpenWaveBundle.cmake" DESTINATION lib/cmake/OpenWaveSDK)
