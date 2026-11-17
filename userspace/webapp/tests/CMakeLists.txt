cmake_minimum_required(VERSION 3.21)
project(WebAppDefinitionTests LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Test Network)
set(CMAKE_AUTOMOC ON)
add_executable(webapp-definition-tests tst_definition.cpp)
target_link_libraries(webapp-definition-tests PRIVATE Qt6::Core Qt6::Test)
set_target_properties(webapp-definition-tests PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "/usr/lib64;/usr/lib")
if(CMAKE_CROSSCOMPILING)
    find_package(OpenWaveSDK REQUIRED COMPONENTS Applications)
    add_executable(webapp-runtime-tests runtime.cpp)
    target_link_libraries(webapp-runtime-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network OpenWave::Applications)
    set_target_properties(webapp-runtime-tests PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "/usr/lib64;/usr/lib")
endif()

add_executable(webapp-icon-tests tst_icons.cpp ../../web/WebsiteIconFetcher.cpp ../../web/WebsiteIconFetcher.h)
target_link_libraries(webapp-icon-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Test Qt6::Network)
