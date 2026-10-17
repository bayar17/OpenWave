find_package(Qt6 6.10 REQUIRED COMPONENTS Quick)
find_package(OpenWaveSDK REQUIRED COMPONENTS Web Fonts Applications ApplicationData)
qt_standard_project_setup(REQUIRES 6.10)
qt_add_executable(openwave-webapp main.cpp WebAppAppearance.h)
qt_add_qml_module(openwave-webapp
    URI OpenWaveWebApp VERSION 1.0
    IMPORT_PATH ${OpenWaveSDK_QML_IMPORT_PATH}
    QML_FILES Main.qml)
target_link_libraries(openwave-webapp PRIVATE Qt6::Quick OpenWave::Web OpenWave::Fonts
    OpenWave::Applications OpenWave::ApplicationData)
set_target_properties(openwave-webapp PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib;/usr/lib64;/usr/lib")
install(TARGETS openwave-webapp RUNTIME DESTINATION libexec)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../OceanLauncher/OceanBrowser/assets/globe.svg"
    DESTINATION share/pixmaps RENAME openwave-webapp.svg)
