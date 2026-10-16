add_executable(timed main.c)
target_compile_options(timed PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
target_include_directories(timed PRIVATE
    "${CMAKE_SYSROOT}/usr/include/glib-2.0"
    "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
target_link_libraries(timed PRIVATE OpenWave::Xml gio-2.0 gobject-2.0 glib-2.0)
install(TARGETS timed RUNTIME DESTINATION /System/Services)
install(FILES org.openwave.Time.conf DESTINATION share/dbus-1/system.d)

set_target_properties(timed PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "/usr/lib64;/usr/lib")
