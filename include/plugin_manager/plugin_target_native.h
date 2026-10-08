#ifndef PLUGIN_MANAGER_PLUGIN_TARGET_NATIVE_H
#define PLUGIN_MANAGER_PLUGIN_TARGET_NATIVE_H

#include "plugin_elf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*pm_native_entry_convert_fn)(void *context, uint32_t address,
                                          enum pm_elf_entry_kind kind,
                                          void *function_out);

struct pm_native_entry_adapter {
    uint32_t executable_base;
    size_t executable_size;
    pm_native_entry_convert_fn convert;
    void *context;
};

int pm_native_entry_resolve(void *context, uint32_t address,
                            enum pm_elf_entry_kind kind, void *function_out);

int pm_native_entry_adapter_validate(const struct pm_native_entry_adapter *adapter,
                                     const struct pm_elf_image *image);

#ifdef __cplusplus
}
#endif

#endif
