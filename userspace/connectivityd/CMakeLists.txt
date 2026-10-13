add_executable(connectivityd
    main.c
    service.c
    dbus.c
    network_manager.c
    hostname.c
    wifi.c
    wifi_profiles.c
    wifi_operations.c
    bluetooth.c
    bluetooth_agent.c)
target_compile_options(connectivityd PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
target_include_directories(connectivityd PRIVATE
    "${CMAKE_SYSROOT}/usr/include/libnm"
    "${CMAKE_SYSROOT}/usr/include/glib-2.0"
    "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
target_link_libraries(connectivityd PRIVATE OpenWave::Xml nm gio-2.0 gobject-2.0 glib-2.0)
install(TARGETS connectivityd RUNTIME DESTINATION libexec)
install(FILES org.openwave.Connectivity.conf
        DESTINATION share/dbus-1/system.d)
