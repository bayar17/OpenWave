cmake_minimum_required(VERSION 3.21)
project(SettingsEnvironmentTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.10 REQUIRED COMPONENTS Core Test)
set(settings_sources "${CMAKE_CURRENT_LIST_DIR}/../..")
add_executable(settings-environment-tests tst_settingsenvironment.cpp
    "${settings_sources}/SettingsEnvironment.cpp" "${settings_sources}/SettingsEnvironment.h"
    "${settings_sources}/SettingsBackend.cpp" "${settings_sources}/SettingsBackend.h")
target_include_directories(settings-environment-tests PRIVATE "${settings_sources}")
target_link_libraries(settings-environment-tests PRIVATE Qt6::Core Qt6::Test)
enable_testing()
add_test(NAME settings-environment COMMAND settings-environment-tests)
add_executable(settings-general-service-tests tst_settingsgeneralservice.cpp
    "${settings_sources}/SettingsGeneralService.cpp" "${settings_sources}/SettingsGeneralService.h"
    "${settings_sources}/SettingsBackend.cpp" "${settings_sources}/SettingsBackend.h")
target_include_directories(settings-general-service-tests PRIVATE "${settings_sources}")
target_link_libraries(settings-general-service-tests PRIVATE Qt6::Core Qt6::Test)
add_test(NAME settings-general-service COMMAND settings-general-service-tests)
