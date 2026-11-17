add_executable(waydroidd waydroidd.c)
target_compile_options(waydroidd PRIVATE -Wall -Wextra -Werror)
install(TARGETS waydroidd RUNTIME DESTINATION /System/Services)
install(FILES zz-openwave-waydroid.conf DESTINATION /etc/dbus-1/system.d)
install(FILES prepare_binder.py DESTINATION /usr/libexec RENAME openwave-waydroid-binder.py)
add_executable(waydroid-dnsmasq dnsmasq-log.c)
set_target_properties(waydroid-dnsmasq PROPERTIES OUTPUT_NAME dnsmasq)
target_compile_options(waydroid-dnsmasq PRIVATE -Wall -Wextra -Werror)
install(TARGETS waydroid-dnsmasq RUNTIME DESTINATION /usr/libexec/openwave-waydroid)

# Stage 2A helper can also be cross-built independently of all Qt modules.
# It is NOT exposed as mount(8)/umount(8) and remains inactive in Waydroid.
add_subdirectory(mount-helper)
