#ifndef PLUGIN_MANAGER_PLUGIN_DESCRIPTOR_H
#define PLUGIN_MANAGER_PLUGIN_DESCRIPTOR_H

#include <stddef.h>
#include <stdint.h>

#include "plugin_abi.h"
#include "plugin_elf.h"
#include "plugin_target_arm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_WIRE_DESCRIPTOR_MAGIC 0x53444D50u /* "PMDS" */
#define PM_WIRE_DESCRIPTOR_VERSION 2u
#define PM_WIRE_DESCRIPTOR_HEADER_SIZE 88u
#define PM_WIRE_DESCRIPTOR_MAX_REQUIRED_SERVICES 64u

struct pm_wire_plugin_descriptor {
    pm_uuid_t application_id;
    pm_uuid_t interface_id;
    uint32_t interface_version;
    uint32_t plugin_id;
    uint32_t required_service_count;
    const pm_uuid_t *required_service_ids;
    const char *name;
    size_t name_size;
    const char *version;
    size_t version_size;
    uint32_t create_address;
    uint32_t destroy_address;
    const uint8_t *extension;
    size_t extension_size;
};

#define PM_DESCRIPTOR_CREATE PM_ELF_ENTRY_CREATE
#define PM_DESCRIPTOR_DESTROY PM_ELF_ENTRY_DESTROY

struct pm_descriptor_bind_config {
    pm_elf_entry_resolve_fn resolve;
    void *context;
};

int pm_wire_descriptor_bind(const struct pm_wire_plugin_descriptor *wire,
                            const struct pm_elf_image *image,
                            const struct pm_descriptor_bind_config *config,
                            struct pm_plugin_descriptor *result);

int pm_wire_descriptor_decode(const struct pm_elf_image *image,
                              size_t descriptor_offset,
                              struct pm_wire_plugin_descriptor *result);

#ifdef __cplusplus
}
#endif

#endif
