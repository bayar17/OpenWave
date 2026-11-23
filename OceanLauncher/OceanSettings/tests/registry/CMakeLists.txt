cmake_minimum_required(VERSION 3.21)
project(SettingsModuleRegistryTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick Test)
qt_standard_project_setup(REQUIRES 6.10)
set(registry_sources "${CMAKE_CURRENT_LIST_DIR}/../..")
qt_add_qml_module(registry-under-test
    STATIC
    URI OpenWave.Settings
    VERSION 1.0
    SOURCES
        "${registry_sources}/SettingsModuleRegistry.cpp"
        "${registry_sources}/SettingsModuleRegistry.h")
target_link_libraries(registry-under-test PUBLIC Qt6::Qml)
target_include_directories(registry-under-test PUBLIC "${registry_sources}")
qt_add_executable(settings-module-registry-tests tst_settingsmoduleregistry.cpp)
target_link_libraries(settings-module-registry-tests PRIVATE
    registry-under-testplugin registry-under-test Qt6::Test Qt6::Quick Qt6::Gui)
enable_testing()
add_test(NAME settings-module-registry COMMAND settings-module-registry-tests)
set_tests_properties(settings-module-registry PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
