cmake_minimum_required(VERSION 3.21)
project(OpenWaveSDK VERSION 0.1.0 LANGUAGES C CXX)
include(CMakePackageConfigHelpers)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Network)
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
openwave_client(Prompts openwave-prompts
    "${clients}/prompt-client/OpenWavePromptClient.cpp"
    "${clients}/prompt-client/OpenWavePromptClient.h"
    "${clients}/prompt-client/PromptTransport.cpp"
    "${clients}/prompt-client/PromptTransport.h"
    "${clients}/prompt-client/PromptRendererBackend.cpp"
    "${clients}/prompt-client/PromptRendererBackend.h")
openwave_bus(openwave-prompts)
openwave_client(FileChooser openwave-filechooser
    "${clients}/portal-client/OpenWavePortals.cpp" "${clients}/portal-client/OpenWavePortals.h")
openwave_bus(openwave-filechooser)
target_include_directories(openwave-filechooser PRIVATE "${CMAKE_SYSROOT}/usr/include/gio-unix-2.0")
openwave_client(Applications openwave-applications
    "${clients}/application-client/ApplicationChannel.cpp" "${clients}/application-client/ApplicationChannel.h"
    "${clients}/application-client/ApplicationsClient.cpp" "${clients}/application-client/ApplicationsClient.h")
openwave_bus(openwave-applications)
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
