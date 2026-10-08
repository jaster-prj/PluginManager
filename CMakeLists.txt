cmake_minimum_required(VERSION 3.16)
project(PluginManager LANGUAGES C CXX)

option(PM_BUILD_TESTS "Build PluginManager host tests" ON)
option(PM_BUILD_ARM_FIXTURE "Build the ARM constrained plugin fixture" OFF)

add_library(plugin_manager
    src/plugin_manager.c
    src/plugin_registry.c
    src/plugin_package.c
    src/plugin_elf.c
    src/plugin_descriptor.c
    src/plugin_descriptor_bind.c
    src/plugin_target_arm.c
    src/plugin_target_native.c
)


target_include_directories(plugin_manager
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_compile_features(plugin_manager PUBLIC c_std_11)
target_compile_options(plugin_manager PRIVATE -Wall -Wextra -Wpedantic)

if(PM_BUILD_TESTS)
    enable_testing()
    add_executable(plugin_manager_tests tests/plugin_manager_tests.c)
    target_sources(plugin_manager_tests PRIVATE tests/plugin_fixture.c)
    target_link_libraries(plugin_manager_tests PRIVATE plugin_manager)
    target_compile_features(plugin_manager_tests PRIVATE c_std_11)
    add_test(NAME plugin_manager_tests COMMAND plugin_manager_tests)

    add_executable(plugin_manager_elf_tests tests/plugin_elf_tests.c)
    target_link_libraries(plugin_manager_elf_tests PRIVATE plugin_manager)
    target_compile_features(plugin_manager_elf_tests PRIVATE c_std_11)
    add_test(NAME plugin_manager_elf_tests COMMAND plugin_manager_elf_tests)

    add_executable(plugin_manager_descriptor_tests tests/plugin_descriptor_tests.c)
    target_link_libraries(plugin_manager_descriptor_tests PRIVATE plugin_manager)
    target_compile_features(plugin_manager_descriptor_tests PRIVATE c_std_11)
    add_test(NAME plugin_manager_descriptor_tests COMMAND plugin_manager_descriptor_tests)

    add_executable(plugin_manager_package_tool tests/package_tool.c)
    target_link_libraries(plugin_manager_package_tool PRIVATE plugin_manager)
    target_compile_features(plugin_manager_package_tool PRIVATE c_std_11)
endif()

if(PM_BUILD_ARM_FIXTURE)
    add_subdirectory(tests/arm_plugin)
    add_executable(plugin_manager_arm_package_test tests/arm_package_test.c)
    target_link_libraries(plugin_manager_arm_package_test PRIVATE plugin_manager)
    target_compile_features(plugin_manager_arm_package_test PRIVATE c_std_11)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/arm-plugin.package"
        COMMAND plugin_manager_package_tool
            "$<TARGET_FILE:plugin_manager_arm_fixture>"
            "${CMAKE_CURRENT_BINARY_DIR}/arm-plugin.package"
        DEPENDS plugin_manager_package_tool plugin_manager_arm_fixture
        VERBATIM)
    add_custom_target(plugin_manager_arm_package ALL
        DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/arm-plugin.package")
    add_test(NAME plugin_manager_arm_package
        COMMAND plugin_manager_arm_package_test
            "${CMAKE_CURRENT_BINARY_DIR}/arm-plugin.package")
    add_dependencies(plugin_manager_arm_package_test plugin_manager_arm_package)
endif()

install(TARGETS plugin_manager)
install(DIRECTORY include/ DESTINATION include)
