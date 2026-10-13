add_executable(wallpaperagentd main.c)
target_compile_options(wallpaperagentd PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
target_include_directories(wallpaperagentd PRIVATE
    "${CMAKE_SYSROOT}/usr/include/glib-2.0"
    "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
target_link_libraries(wallpaperagentd PRIVATE OpenWave::Xml gio-2.0 gobject-2.0 glib-2.0 png16)
install(TARGETS wallpaperagentd RUNTIME DESTINATION libexec)
install(FILES org.openwave.Wallpaper.conf DESTINATION share/dbus-1/system.d)
