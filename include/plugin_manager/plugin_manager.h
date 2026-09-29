#ifndef PLUGIN_MANAGER_PLUGIN_MANAGER_H
#define PLUGIN_MANAGER_PLUGIN_MANAGER_H

#include "plugin_contract.h"
#include "plugin_elf.h"
#include "plugin_package.h"
#include "plugin_descriptor.h"

#ifdef __cplusplus
extern "C" {
#endif

struct pm_manager;

enum pm_manager_state {
    PM_MANAGER_IDLE = 0,
    PM_MANAGER_REGISTERING,
    PM_MANAGER_SCANNING,
    PM_MANAGER_LOADING,
    PM_MANAGER_CREATING,
    PM_MANAGER_DESTROYING,
    PM_MANAGER_UNLOADING,
};

typedef int (*pm_manager_lock_fn)(void *context);
typedef void (*pm_manager_unlock_fn)(void *context);

struct pm_manager_config {
    const struct pm_application_contract *contract;
    size_t max_plugins;
    pm_manager_lock_fn lock;
    pm_manager_unlock_fn unlock;
    void *lock_context;
};

typedef int (*pm_manager_descriptor_fn)(const struct pm_elf_image *image,
                                        const struct pm_package_manifest *manifest,
                                        const struct pm_plugin_descriptor **descriptor,
                                        void *context);

typedef int (*pm_manager_wire_descriptor_fn)(
    const struct pm_wire_plugin_descriptor *wire,
    const struct pm_elf_image *image,
    const struct pm_package_manifest *manifest,
    const struct pm_plugin_descriptor **descriptor,
    void *context);

typedef int (*pm_manager_image_alloc_fn)(void *context, size_t text_size,
                                         size_t rodata_size, size_t data_size,
                                         size_t bss_size, uint8_t **text,
                                         uint8_t **rodata, uint8_t **data,
                                         uint8_t **bss);
typedef void (*pm_manager_image_free_fn)(void *context, uint8_t *text,
                                         uint8_t *rodata, uint8_t *data,
                                         uint8_t *bss);

struct pm_manager_package_config {
    const struct pm_elf_profile *elf_profile;
    uint8_t *text_storage;
    size_t text_capacity;
    uint8_t *rodata_storage;
    size_t rodata_capacity;
    uint8_t *data_storage;
    size_t data_capacity;
    uint8_t *bss_storage;
    size_t bss_capacity;
    pm_elf_resolve_fn resolve;
    void *resolve_context;
    pm_manager_descriptor_fn get_descriptor;
    void *descriptor_context;
    pm_manager_wire_descriptor_fn adapt_wire_descriptor;
    void *wire_descriptor_context;
    pm_manager_image_alloc_fn alloc_image;
    pm_manager_image_free_fn free_image;
    void *image_allocator_context;
};

typedef int (*pm_storage_list_fn)(void *context, size_t index, char *name,
                                  size_t name_capacity, int *present);
typedef int (*pm_storage_size_fn)(void *context, const char *name,
                                  uint32_t *size);
typedef int (*pm_storage_read_fn)(void *context, const char *name,
                                  uint32_t offset, void *destination,
                                  size_t size);

struct pm_storage_adapter {
    void *context;
    pm_storage_list_fn list;
    pm_storage_size_fn size;
    pm_storage_read_fn read;
    size_t max_entries;
};

struct pm_manager_scan_config {
    /* The adapter is copied; its context must remain valid until the next scan
     * or until the associated filesystem is detached. */
    const struct pm_storage_adapter *storage;
    const char *candidate_suffix;
    size_t max_name_length;
    size_t max_package_size;
};

struct pm_available_plugin {
    uint32_t plugin_id;
    uint32_t package_size;
    pm_uuid_t application_id;
    pm_uuid_t interface_id;
    uint32_t interface_version;
    const char *name;
    const char *version;
    const char *storage_name;
};

struct pm_manager *pm_manager_create(const struct pm_manager_config *config);
void pm_manager_destroy(struct pm_manager *manager);
enum pm_manager_state pm_manager_state_get(const struct pm_manager *manager);

int pm_manager_register_static(struct pm_manager *manager,
                               const struct pm_plugin_descriptor *descriptor);
int pm_manager_load_package(struct pm_manager *manager, const uint8_t *package,
                            size_t package_size,
                            const struct pm_manager_package_config *config,
                            uint32_t *plugin_id);
int pm_manager_unload(struct pm_manager *manager, uint32_t plugin_id);
int pm_manager_scan(struct pm_manager *manager,
                    const struct pm_manager_scan_config *config);
size_t pm_manager_available_count(const struct pm_manager *manager);
int pm_manager_available_get(const struct pm_manager *manager, size_t index,
                             struct pm_available_plugin *result);
int pm_manager_load_available(struct pm_manager *manager, size_t index,
                              const struct pm_manager_package_config *config,
                              uint32_t *plugin_id);
int pm_manager_create_instance(struct pm_manager *manager, uint32_t plugin_id);
int pm_manager_destroy_instance(struct pm_manager *manager, uint32_t plugin_id);
int pm_manager_get_instance(struct pm_manager *manager, uint32_t plugin_id,
                            void **instance);

/* Used by platform storage adapters to bind their resource lifetime. */
int pm_manager_filesystem_attach(struct pm_manager *manager,
                                  const void *identity);
int pm_manager_filesystem_detach(struct pm_manager *manager);
int pm_manager_filesystem_is_attached(const struct pm_manager *manager);
int pm_manager_filesystem_is_same(const struct pm_manager *manager,
                                  const void *identity);

#ifdef __cplusplus
}
#endif

#endif
