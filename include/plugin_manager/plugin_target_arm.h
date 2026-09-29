#ifndef PLUGIN_MANAGER_PLUGIN_TARGET_ARM_H
#define PLUGIN_MANAGER_PLUGIN_TARGET_ARM_H

#include "plugin_elf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The conversion callback is the only target-specific function-pointer cast. */
typedef int (*pm_arm_entry_convert_fn)(void *context, uint32_t address,
                                       enum pm_elf_entry_kind kind,
                                       void *function_out);

struct pm_arm_entry_adapter {
    uint32_t executable_base;
    size_t executable_size;
    pm_arm_entry_convert_fn convert;
    void *context;
};

int pm_arm_entry_resolve(void *context, uint32_t address,
                         enum pm_elf_entry_kind kind, void *function_out);

int pm_arm_entry_adapter_validate(const struct pm_arm_entry_adapter *adapter,
                                  const struct pm_elf_image *image);

#ifdef __cplusplus
}
#endif

#endif
