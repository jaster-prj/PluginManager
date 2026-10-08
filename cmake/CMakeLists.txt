# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(plugin_manager)

zephyr_library_sources_ifdef(CONFIG_PLUGIN_MANAGER
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugin_manager.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugin_registry.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugin_package.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugin_elf.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugin_descriptor.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugin_descriptor_bind.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugin_target_arm.c
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugin_target_native.c
)

zephyr_library_sources_ifdef(CONFIG_PLUGIN_MANAGER_ZEPHYR_FS
    ${CMAKE_CURRENT_LIST_DIR}/../src/plugin_manager_zephyr.c
)

zephyr_include_directories(${CMAKE_CURRENT_LIST_DIR}/../include)
