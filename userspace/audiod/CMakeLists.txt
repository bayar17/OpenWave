add_executable(audiod main.c)
target_compile_options(audiod PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
target_include_directories(audiod PRIVATE
    "${CMAKE_SYSROOT}/usr/include/wireplumber-0.5"
    "${CMAKE_SYSROOT}/usr/include/pipewire-0.3"
    "${CMAKE_SYSROOT}/usr/include/spa-0.2"
    "${CMAKE_SYSROOT}/usr/include/glib-2.0"
    "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
target_link_libraries(audiod PRIVATE OpenWave::Xml wireplumber-0.5 pipewire-0.3 gio-2.0 gobject-2.0 glib-2.0)
install(TARGETS audiod RUNTIME DESTINATION libexec)
install(FILES org.openwave.Audio.conf DESTINATION share/dbus-1/system.d)
