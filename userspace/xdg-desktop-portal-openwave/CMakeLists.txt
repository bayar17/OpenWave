cmake_minimum_required(VERSION 3.21)
project(OpenWaveDesktopPortal LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(OpenWaveSDK REQUIRED COMPONENTS Xml)
add_executable(xdg-desktop-portal-openwave main.cpp SettingsPortal.cpp)
target_compile_features(xdg-desktop-portal-openwave PRIVATE cxx_std_17)
target_compile_options(xdg-desktop-portal-openwave PRIVATE -Wall -Wextra -Werror)
target_include_directories(xdg-desktop-portal-openwave PRIVATE
    "${CMAKE_SYSROOT}/usr/include/glib-2.0"
    "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
target_link_libraries(xdg-desktop-portal-openwave PRIVATE Qt6::Core OpenWave::Xml gio-2.0 gobject-2.0 glib-2.0)
set_target_properties(xdg-desktop-portal-openwave PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "/usr/lib64;/usr/lib")
install(TARGETS xdg-desktop-portal-openwave RUNTIME DESTINATION /System/Services)
install(FILES org.freedesktop.impl.portal.desktop.openwave.service
    DESTINATION share/dbus-1/services)
install(FILES openwave.portal DESTINATION share/xdg-desktop-portal/portals)
install(FILES openwave-portals.conf DESTINATION share/xdg-desktop-portal)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    install(FILES ../dbus/interfaces/org.freedesktop.portal.Settings.xml
        ../dbus/interfaces/org.freedesktop.impl.portal.Settings.xml DESTINATION share/dbus-1/interfaces)
endif()
