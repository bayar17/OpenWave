add_executable(promptd main.c)
target_compile_options(promptd PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
target_include_directories(promptd PRIVATE
    "${CMAKE_SYSROOT}/usr/include/glib-2.0"
    "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
target_link_libraries(promptd PRIVATE OpenWave::Identity gio-2.0 gobject-2.0 glib-2.0)
install(TARGETS promptd RUNTIME DESTINATION /System/Services)
install(FILES org.openwave.Prompt.service DESTINATION share/dbus-1/services)

set_target_properties(promptd PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "/usr/lib64;/usr/lib")
