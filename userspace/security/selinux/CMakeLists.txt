find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_library(OPENWAVE_SELINUX_LIBRARY selinux REQUIRED)
add_library(openwave-selinux STATIC OpenWaveSELinux.c)
target_compile_options(openwave-selinux PRIVATE -Wall -Wextra -Werror)
target_include_directories(openwave-selinux PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(openwave-selinux PUBLIC "${OPENWAVE_SELINUX_LIBRARY}")
set(OPENWAVE_POLICY_OUTPUT "${CMAKE_BINARY_DIR}/selinux-policy")
add_custom_target(openwave-selinux-policy ALL
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../../../scripts/build-openwave-selinux.py"
        --sdk "${CMAKE_SYSROOT}/../.." --target "${OPENWAVE_TARGET_TRIPLE}"
        --stage "${OPENWAVE_POLICY_OUTPUT}/stage" --output "${OPENWAVE_POLICY_OUTPUT}/build"
    VERBATIM)
install(DIRECTORY "${OPENWAVE_POLICY_OUTPUT}/stage/etc/selinux" DESTINATION /etc)
