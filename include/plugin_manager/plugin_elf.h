#ifndef PLUGIN_MANAGER_PLUGIN_ELF_H
#define PLUGIN_MANAGER_PLUGIN_ELF_H

#include <stddef.h>
#include <stdint.h>

#include "plugin_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_ELF_CLASS_32 1u
#define PM_ELF_DATA_LSB 1u
#define PM_ELF_MACHINE_ARM 40u
#define PM_ELF_TYPE_REL 1u
#define PM_ELF_MAX_SECTIONS 64u
#define PM_ELF_DESCRIPTOR_SYMBOL "pm_plugin_get_descriptor"
#define PM_ELF_R_ARM_ABS32 2u

typedef int (*pm_elf_resolve_fn)(void *context, const char *name,
                                 uint32_t *address);

enum pm_elf_entry_kind {
    PM_ELF_ENTRY_CREATE = 1,
    PM_ELF_ENTRY_DESTROY = 2,
};

typedef int (*pm_elf_entry_resolve_fn)(void *context, uint32_t address,
                                       enum pm_elf_entry_kind kind,
                                       void *function_out);

struct pm_elf_profile {
    size_t max_sections;
    size_t max_image_size;
    size_t max_ram_size;
    uint32_t text_address;
    uint32_t rodata_address;
    uint32_t data_address;
};

struct pm_elf_image {
    uint8_t *text;
    size_t text_size;
    uint8_t *rodata;
    size_t rodata_size;
    uint8_t *data;
    size_t data_size;
    size_t bss_size;
    size_t descriptor_offset;
    uint32_t descriptor_address;
    uint32_t text_address;
    uint32_t rodata_address;
};

int pm_elf_validate(const uint8_t *buffer, size_t buffer_size,
                    const struct pm_elf_profile *profile);

int pm_elf_resolve_entry(const struct pm_elf_image *image, uint32_t address,
                         enum pm_elf_entry_kind kind,
                         pm_elf_entry_resolve_fn resolve, void *context,
                         void *function_out);

int pm_elf_load(const uint8_t *buffer, size_t buffer_size,
                const struct pm_elf_profile *profile,
                uint8_t *text_storage, size_t text_capacity,
                uint8_t *rodata_storage, size_t rodata_capacity,
                uint8_t *data_storage, size_t data_capacity,
                uint8_t *bss_storage, size_t bss_capacity,
                pm_elf_resolve_fn resolve, void *resolve_context,
                struct pm_elf_image *result);

#ifdef __cplusplus
}
#endif

#endif
