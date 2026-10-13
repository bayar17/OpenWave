add_executable(appd main.c registry.c process.c dbus.c)
target_compile_options(appd PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
target_include_directories(appd PRIVATE
    "${CMAKE_SYSROOT}/usr/include/glib-2.0"
    "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
target_link_libraries(appd PRIVATE OpenWave::Xml gio-2.0 gobject-2.0 glib-2.0)
install(TARGETS appd RUNTIME DESTINATION libexec)
install(FILES appd.conf DESTINATION /etc/openwave)
install(FILES org.openwave.Applications.conf DESTINATION share/dbus-1/system.d)
