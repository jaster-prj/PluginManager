#include "plugin_manager/plugin_descriptor.h"

int pm_wire_descriptor_bind(const struct pm_wire_plugin_descriptor *wire,
                            const struct pm_elf_image *image,
                            const struct pm_descriptor_bind_config *config,
                            struct pm_plugin_descriptor *result)
{
    int status;

    if (wire == NULL || image == NULL || config == NULL ||
        config->resolve == NULL || result == NULL) {
        return PM_EINVAL;
    }
    *result = (struct pm_plugin_descriptor){
        .framework_abi_version = PM_FRAMEWORK_ABI_VERSION,
        .descriptor_size = sizeof(*result),
        .application_id = wire->application_id,
        .interface_id = wire->interface_id,
        .interface_version = wire->interface_version,
        .plugin_id = wire->plugin_id,
        .required_service_ids = wire->required_service_ids,
        .required_service_count = wire->required_service_count,
        .name = wire->name,
        .version = wire->version,
    };
    status = pm_elf_resolve_entry(image, wire->create_address,
                                   PM_ELF_ENTRY_CREATE, config->resolve,
                                   config->context, &result->create);
    if (status != PM_OK) {
        return status;
    }
    status = pm_elf_resolve_entry(image, wire->destroy_address,
                                   PM_ELF_ENTRY_DESTROY, config->resolve,
                                   config->context, &result->destroy);
    if (status != PM_OK) {
        return status;
    }
    return result->create != NULL && result->destroy != NULL ? PM_OK : PM_EPROTO;
}
