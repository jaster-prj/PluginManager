#ifndef PLUGIN_MANAGER_PLUGIN_CONTRACT_H
#define PLUGIN_MANAGER_PLUGIN_CONTRACT_H

#include "plugin_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

struct pm_plugin_interface {
    pm_uuid_t interface_id;
    uint32_t version;
    const pm_uuid_t *supported_service_ids;
    size_t supported_service_count;
    int (*validate)(const struct pm_plugin_descriptor *descriptor);
    int (*create)(const struct pm_plugin_descriptor *descriptor,
                  const struct pm_service_table *services,
                  void **instance);
};

struct pm_application_contract {
    pm_uuid_t application_id;
    uint32_t version;
    const struct pm_plugin_interface *interfaces;
    size_t interface_count;
    const struct pm_service *services;
    size_t service_count;
};

int pm_contract_validate(const struct pm_application_contract *contract);
int pm_contract_find_interface(const struct pm_application_contract *contract,
                               const pm_uuid_t *interface_id,
                               uint32_t version,
                               const struct pm_plugin_interface **result);
int pm_contract_build_services(const struct pm_application_contract *contract,
                               const struct pm_plugin_descriptor *descriptor,
                               struct pm_service_table *result,
                               struct pm_service *storage,
                               size_t storage_count);

#ifdef __cplusplus
}
#endif

#endif
