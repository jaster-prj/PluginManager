#ifndef PLUGIN_MANAGER_PLUGIN_ABI_H
#define PLUGIN_MANAGER_PLUGIN_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_FRAMEWORK_ABI_VERSION 1u
#define PM_UUID_SIZE 16u
#define PM_MAX_NAME_LENGTH 63u
#define PM_MAX_VERSION_LENGTH 31u

enum pm_status {
    PM_OK = 0,
    PM_EINVAL = -22,
    PM_ENOENT = -2,
    PM_EEXIST = -17,
    PM_ENOMEM = -12,
    PM_ENOTSUP = -95,
    PM_EBUSY = -16,
    PM_EOVERFLOW = -75,
    PM_EPROTO = -71,
    PM_EIO = -5,
    PM_EPERM = -1,
};

typedef struct pm_uuid {
    uint8_t bytes[PM_UUID_SIZE];
} pm_uuid_t;

struct pm_service;
struct pm_service_table;
struct pm_plugin_descriptor;

typedef int (*pm_plugin_create_fn)(const struct pm_service_table *services,
                                   void **instance);
typedef int (*pm_plugin_destroy_fn)(void *instance);

struct pm_service {
    pm_uuid_t service_id;
    uint32_t version;
    const void *vtable;
};

struct pm_service_table {
    size_t count;
    const struct pm_service *items;
};

struct pm_plugin_descriptor {
    uint32_t framework_abi_version;
    uint32_t descriptor_size;
    pm_uuid_t application_id;
    pm_uuid_t interface_id;
    uint32_t interface_version;
    uint32_t plugin_id;
    const pm_uuid_t *required_service_ids;
    size_t required_service_count;
    const char *name;
    const char *version;
    pm_plugin_create_fn create;
    pm_plugin_destroy_fn destroy;
    const uint8_t *extension;
    size_t extension_size;
};

#define PM_PLUGIN_DESCRIPTOR_MIN_SIZE \
    (offsetof(struct pm_plugin_descriptor, destroy) + sizeof(pm_plugin_destroy_fn))

int pm_uuid_equal(const pm_uuid_t *left, const pm_uuid_t *right);
int pm_descriptor_validate(const struct pm_plugin_descriptor *descriptor,
                           const pm_uuid_t *application_id);

#ifdef __cplusplus
}
#endif

#endif
