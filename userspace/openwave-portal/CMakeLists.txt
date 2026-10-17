add_executable(openwave-portal main.c)
target_compile_options(openwave-portal PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
target_include_directories(openwave-portal PRIVATE
    "${CMAKE_SYSROOT}/usr/include/glib-2.0"
    "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
target_link_libraries(openwave-portal PRIVATE OpenWave::Identity gio-2.0 gobject-2.0 glib-2.0)
install(TARGETS openwave-portal RUNTIME DESTINATION /System/Services)
install(FILES org.openwave.portal.conf DESTINATION share/dbus-1/system.d)

set_target_properties(openwave-portal PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "/usr/lib64;/usr/lib")
