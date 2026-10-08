#include "plugin_manager/plugin_target_native.h"

static int address_in_executable_region(const struct pm_native_entry_adapter *adapter,
                                        uint32_t address)
{
    return address >= adapter->executable_base &&
           (uint64_t)(address - adapter->executable_base) <
               adapter->executable_size;
}

int pm_native_entry_adapter_validate(const struct pm_native_entry_adapter *adapter,
                                     const struct pm_elf_image *image)
{
    if (adapter == NULL || image == NULL || adapter->convert == NULL ||
        adapter->executable_size == 0u ||
        adapter->executable_base != image->text_address ||
        adapter->executable_size != image->text_size) {
        return PM_EINVAL;
    }
    return PM_OK;
}

int pm_native_entry_resolve(void *context, uint32_t address,
                            enum pm_elf_entry_kind kind, void *function_out)
{
    struct pm_native_entry_adapter *adapter = context;

    if (adapter == NULL || function_out == NULL ||
        (kind != PM_ELF_ENTRY_CREATE && kind != PM_ELF_ENTRY_DESTROY) ||
        !address_in_executable_region(adapter, address)) {
        return PM_EPROTO;
    }
    return adapter->convert(adapter->context, address, kind, function_out);
}
