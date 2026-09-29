#include "plugin_manager/plugin_contract.h"

#include <string.h>

int pm_uuid_equal(const pm_uuid_t *left, const pm_uuid_t *right)
{
    if (left == NULL || right == NULL) {
        return 0;
    }
    return memcmp(left->bytes, right->bytes, PM_UUID_SIZE) == 0;
}

static size_t bounded_length(const char *value, size_t maximum)
{
    size_t length = 0u;

    while (value != NULL && length <= maximum && value[length] != '\0') {
        ++length;
    }
    return length;
}

static int bounded_string_valid(const char *value, size_t maximum)
{
    return value != NULL && bounded_length(value, maximum) <= maximum;
}

int pm_descriptor_validate(const struct pm_plugin_descriptor *descriptor,
                           const pm_uuid_t *application_id)
{
    if (descriptor == NULL || application_id == NULL) {
        return PM_EINVAL;
    }
    if (descriptor->descriptor_size < PM_PLUGIN_DESCRIPTOR_MIN_SIZE ||
        descriptor->framework_abi_version != PM_FRAMEWORK_ABI_VERSION ||
        !pm_uuid_equal(&descriptor->application_id, application_id) ||
        (descriptor->required_service_count > 0u &&
            descriptor->required_service_ids == NULL) ||
        !bounded_string_valid(descriptor->name, PM_MAX_NAME_LENGTH) ||
        !bounded_string_valid(descriptor->version, PM_MAX_VERSION_LENGTH) ||
        descriptor->create == NULL || descriptor->destroy == NULL) {
        return PM_EPROTO;
    }
    return PM_OK;
}

int pm_contract_validate(const struct pm_application_contract *contract)
{
    size_t i;
    size_t j;

    if (contract == NULL ||
        (contract->interface_count > 0u && contract->interfaces == NULL) ||
        (contract->service_count > 0u && contract->services == NULL)) {
        return PM_EINVAL;
    }
    for (i = 0u; i < contract->interface_count; ++i) {
        if (contract->interfaces[i].version == 0u) {
            return PM_EPROTO;
        }
        for (j = i + 1u; j < contract->interface_count; ++j) {
            if (pm_uuid_equal(&contract->interfaces[i].interface_id,
                              &contract->interfaces[j].interface_id)) {
                return PM_EEXIST;
            }
        }
    }
    for (i = 0u; i < contract->service_count; ++i) {
        if (contract->services[i].version == 0u ||
            contract->services[i].vtable == NULL) {
            return PM_EPROTO;
        }
        for (j = i + 1u; j < contract->service_count; ++j) {
            if (pm_uuid_equal(&contract->services[i].service_id,
                              &contract->services[j].service_id)) {
                return PM_EEXIST;
            }
        }
    }
    return PM_OK;
}

int pm_contract_find_interface(const struct pm_application_contract *contract,
                               const pm_uuid_t *interface_id,
                               uint32_t version,
                               const struct pm_plugin_interface **result)
{
    size_t i;

    if (contract == NULL || interface_id == NULL || result == NULL) {
        return PM_EINVAL;
    }
    for (i = 0u; i < contract->interface_count; ++i) {
        if (contract->interfaces[i].version == version &&
            pm_uuid_equal(&contract->interfaces[i].interface_id, interface_id)) {
            *result = &contract->interfaces[i];
            return PM_OK;
        }
    }
    return PM_ENOENT;
}

int pm_contract_build_services(const struct pm_application_contract *contract,
                               const struct pm_plugin_descriptor *descriptor,
                               struct pm_service_table *result,
                               struct pm_service *storage,
                               size_t storage_count)
{
    size_t i;
    size_t j;

    if (contract == NULL || descriptor == NULL || result == NULL ||
        (storage_count > 0u && storage == NULL)) {
        return PM_EINVAL;
    }
    if (descriptor->required_service_count > storage_count) {
        return PM_ENOMEM;
    }
    for (i = 0u; i < descriptor->required_service_count; ++i) {
        for (j = 0u; j < contract->service_count; ++j) {
            if (pm_uuid_equal(&descriptor->required_service_ids[i],
                              &contract->services[j].service_id)) {
                storage[i] = contract->services[j];
                break;
            }
        }
        if (j == contract->service_count) {
            return PM_ENOENT;
        }
    }
    result->count = descriptor->required_service_count;
    result->items = storage;
    return PM_OK;
}
