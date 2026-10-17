find_package(Qt6 REQUIRED COMPONENTS Core Gui)
add_executable(appd main.c registry.c process.c dbus.c identity.c isolation.c storage.c user-folders.c namespace.c manifest.cpp services.cpp seccomp.c webapps.cpp uninstall.cpp uninstall-storage.c)
add_executable(openwave-namespace-keeper namespace-keeper.c)
install(TARGETS openwave-namespace-keeper RUNTIME DESTINATION /System/Services)
target_compile_options(appd PRIVATE -Wall -Wextra -Werror=implicit-function-declaration)
target_include_directories(appd PRIVATE
    "${CMAKE_SYSROOT}/usr/include/glib-2.0"
    "${CMAKE_SYSROOT}/usr/lib64/glib-2.0/include")
target_link_libraries(appd PRIVATE Qt6::Core Qt6::Gui OpenWave::Identity openwave-selinux gio-2.0 gobject-2.0 glib-2.0 ${CMAKE_DL_LIBS})
install(TARGETS appd RUNTIME DESTINATION /System/Services)
install(FILES appd.conf DESTINATION /etc/openwave)
install(FILES org.openwave.Applications.conf DESTINATION share/dbus-1/system.d)

set_target_properties(appd PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "/usr/lib64;/usr/lib")

find_package(OpenWaveSDK REQUIRED COMPONENTS Applications)
add_executable(openwave-launch launch.cpp)
target_link_libraries(openwave-launch PRIVATE OpenWave::Applications Qt6::Core)
set_target_properties(openwave-launch PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "/usr/lib64;/usr/lib")
install(TARGETS openwave-launch RUNTIME DESTINATION /System/Services)
