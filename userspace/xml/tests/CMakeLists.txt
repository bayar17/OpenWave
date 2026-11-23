cmake_minimum_required(VERSION 3.21)
project(OpenWaveXmlTests LANGUAGES C CXX)
find_package(OpenWaveSDK REQUIRED COMPONENTS Xml)
enable_testing()
add_executable(openwave-xml-tests test_xml.c)
target_compile_options(openwave-xml-tests PRIVATE -Wall -Wextra -Werror)
target_link_libraries(openwave-xml-tests PRIVATE OpenWave::Xml)
set_target_properties(openwave-xml-tests PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib64;$ORIGIN/../lib")
add_test(NAME openwave-xml COMMAND openwave-xml-tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../dbus/interfaces")
add_executable(openwave-xml-qt-test test_qt.cpp)
target_compile_options(openwave-xml-qt-test PRIVATE -Wall -Wextra -Werror)
target_link_libraries(openwave-xml-qt-test PRIVATE OpenWave::Xml Qt6::Core)
set_target_properties(openwave-xml-qt-test PROPERTIES BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../lib64;$ORIGIN/../lib")
add_test(NAME openwave-xml-qt COMMAND openwave-xml-qt-test
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/unicode.xml")
