cmake_minimum_required(VERSION 3.21)
project(OpenWaveUserspace LANGUAGES C CXX)
find_package(OpenWaveSDK REQUIRED COMPONENTS Xml Identity)
install(DIRECTORY dbus/interfaces/ DESTINATION share/dbus-1/interfaces
    FILES_MATCHING PATTERN "*.xml")
add_subdirectory(security/selinux)

add_executable(openwave-init init.c)
target_compile_options(openwave-init PRIVATE -Wall -Wextra)
target_link_libraries(openwave-init PRIVATE openwave-selinux)
set_target_properties(openwave-init PROPERTIES OUTPUT_NAME init)
install(TARGETS openwave-init RUNTIME DESTINATION /)

add_subdirectory(connectivityd)
add_subdirectory(timed)
add_subdirectory(audiod)
add_subdirectory(wallpaperagentd)
add_subdirectory(openwave-portal)
add_subdirectory(promptd)
add_subdirectory(appd)
add_subdirectory(permissiond)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../OceanLauncher/OceanLauncher"
                 "OceanLauncher")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../OceanLauncher/OceanSettings"
                 "OceanSettings")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../OceanLauncher/OceanFiles"
                 "OceanFiles")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../OceanLauncher/OceanBrowser"
                 "OceanBrowser")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../OceanLauncher/OceanPhotos"
                 "OceanPhotos")

install(FILES config/kms.json DESTINATION /etc/openwave)
install(FILES config/machine-info DESTINATION /etc)
install(FILES config/avahi-daemon.conf DESTINATION /etc/avahi)
install(FILES config/10-openwave-wireplumber.conf
        DESTINATION /etc/wireplumber/wireplumber.conf.d)
install(FILES config/openwave-session-bus.conf
        DESTINATION /etc/dbus-1)
