cmake_minimum_required(VERSION 3.16)
project(plugin_manager_arm_fixture C)

get_filename_component(PLUGIN_MANAGER_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(NOT PM_ARM_TOOLCHAIN_PREFIX)
    message(FATAL_ERROR "Set PM_ARM_TOOLCHAIN_PREFIX, e.g. arm-zephyr-eabi")
endif()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER "${PM_ARM_TOOLCHAIN_PREFIX}-gcc")
set(CMAKE_OBJCOPY "${PM_ARM_TOOLCHAIN_PREFIX}-objcopy" CACHE FILEPATH "ARM objcopy")

set(CMAKE_C_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=soft -ffreestanding -fno-builtin -fno-common -ffunction-sections -fdata-sections -Wall -Wextra" CACHE STRING "ARM fixture flags")
set(PM_PLUGIN_TEXT_ORIGIN 0x00000000 CACHE STRING "Plugin text origin")
set(PM_PLUGIN_TEXT_LENGTH 0x00001000 CACHE STRING "Plugin text length")
set(PM_PLUGIN_RODATA_ORIGIN 0x00001000 CACHE STRING "Plugin rodata origin")
set(PM_PLUGIN_RODATA_LENGTH 0x00001000 CACHE STRING "Plugin rodata length")
set(PM_PLUGIN_DATA_ORIGIN 0x20000000 CACHE STRING "Plugin data origin")
set(PM_PLUGIN_DATA_LENGTH 0x00001000 CACHE STRING "Plugin data length")

configure_file(${CMAKE_CURRENT_LIST_DIR}/memory.ld.in
               ${CMAKE_CURRENT_BINARY_DIR}/memory.ld @ONLY)

add_executable(plugin_manager_arm_fixture plugin.c)
target_link_options(plugin_manager_arm_fixture PRIVATE
    -r -nostdlib -nostartfiles -nodefaultlibs
    "-T${CMAKE_CURRENT_BINARY_DIR}/memory.ld"
    "-Wl,--emit-relocs")
set_target_properties(plugin_manager_arm_fixture PROPERTIES
    SUFFIX ".elf"
    LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/memory.ld")
