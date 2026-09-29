#include "plugin_manager/plugin_manager.h"

#include <stdlib.h>
#include <string.h>

#define PM_MANAGER_MAX_DISCOVERED 64u
#define PM_MANAGER_MAX_NAME 127u
#define PM_MANAGER_MAX_REQUIRED_SERVICES 64u

struct pm_plugin_record {
    const struct pm_plugin_descriptor *descriptor;
    struct pm_plugin_descriptor descriptor_storage;
    pm_uuid_t required_service_ids[PM_MANAGER_MAX_REQUIRED_SERVICES];
    char name[PM_MAX_NAME_LENGTH + 1u];
    char version[PM_MAX_VERSION_LENGTH + 1u];
    const struct pm_plugin_interface *interface;
    void *instance;
    int instance_active;
    struct pm_service *services;
    struct pm_service_table *service_table;
    int loaded;
    struct pm_elf_image image;
    uint8_t *text_storage;
    size_t text_capacity;
    uint8_t *rodata_storage;
    size_t rodata_capacity;
    uint8_t *data_storage;
    size_t data_capacity;
    uint8_t *bss_storage;
    size_t bss_capacity;
    pm_manager_image_free_fn free_image;
    void *image_allocator_context;
};

static void release_record_image(struct pm_plugin_record *record)
{
    if (record->free_image != NULL) {
        record->free_image(record->image_allocator_context,
                           record->text_storage, record->rodata_storage,
                           record->data_storage, record->bss_storage);
        record->free_image = NULL;
        record->image_allocator_context = NULL;
    }
}

static int own_loaded_descriptor(struct pm_plugin_record *record,
                                 const struct pm_plugin_descriptor *descriptor)
{
    size_t name_length;
    size_t version_length;

    if (descriptor->required_service_count > PM_MANAGER_MAX_REQUIRED_SERVICES) {
        return PM_EOVERFLOW;
    }
    name_length = strlen(descriptor->name);
    version_length = strlen(descriptor->version);
    if (name_length > PM_MAX_NAME_LENGTH || version_length > PM_MAX_VERSION_LENGTH) {
        return PM_EOVERFLOW;
    }
    record->descriptor_storage = *descriptor;
    if (descriptor->required_service_count > 0u) {
        memcpy(record->required_service_ids, descriptor->required_service_ids,
               descriptor->required_service_count * sizeof(pm_uuid_t));
    }
    memcpy(record->name, descriptor->name, name_length + 1u);
    memcpy(record->version, descriptor->version, version_length + 1u);
    record->descriptor_storage.required_service_ids = record->required_service_ids;
    record->descriptor_storage.name = record->name;
    record->descriptor_storage.version = record->version;
    record->descriptor = &record->descriptor_storage;
    return PM_OK;
}

static void rebind_loaded_record(struct pm_plugin_record *record)
{
    if (record->loaded) {
        record->descriptor = &record->descriptor_storage;
        record->descriptor_storage.required_service_ids = record->required_service_ids;
        record->descriptor_storage.name = record->name;
        record->descriptor_storage.version = record->version;
    }
}

struct pm_manager {
    const struct pm_application_contract *contract;
    struct pm_plugin_record *plugins;
    size_t plugin_count;
    size_t max_plugins;
    struct pm_available_plugin available[PM_MANAGER_MAX_DISCOVERED];
    char available_names[PM_MANAGER_MAX_DISCOVERED][PM_MANAGER_MAX_NAME + 1u];
    char available_plugin_names[PM_MANAGER_MAX_DISCOVERED][PM_MAX_NAME_LENGTH + 1u];
    char available_versions[PM_MANAGER_MAX_DISCOVERED][PM_MAX_VERSION_LENGTH + 1u];
    struct pm_storage_adapter available_storage;
    int available_storage_valid;
    size_t available_count;
    enum pm_manager_state state;
    pm_manager_lock_fn lock;
    pm_manager_unlock_fn unlock;
    void *lock_context;
    int filesystem_attached;
    const void *filesystem_identity;
};

static int available_has_plugin_id(const struct pm_manager *manager,
                                   uint32_t plugin_id);

static void release_record_instance_services(struct pm_plugin_record *record)
{
    free(record->services);
    free(record->service_table);
    record->services = NULL;
    record->service_table = NULL;
}

static int manager_enter(struct pm_manager *manager, enum pm_manager_state state)
{
    int status;

    if (manager->lock != NULL) {
        status = manager->lock(manager->lock_context);
        if (status != PM_OK) {
            return status;
        }
    }
    if (manager->state != PM_MANAGER_IDLE) {
        if (manager->unlock != NULL) {
            manager->unlock(manager->lock_context);
        }
        return PM_EBUSY;
    }
    manager->state = state;
    return PM_OK;
}

static void manager_leave(struct pm_manager *manager)
{
    manager->state = PM_MANAGER_IDLE;
    if (manager->unlock != NULL) {
        manager->unlock(manager->lock_context);
    }
}

struct pm_manager *pm_manager_create(const struct pm_manager_config *config)
{
    struct pm_manager *manager;

    if (config == NULL || config->contract == NULL || config->max_plugins == 0u ||
        pm_contract_validate(config->contract) != PM_OK) {
        return NULL;
    }
    manager = calloc(1u, sizeof(*manager));
    if (manager == NULL) {
        return NULL;
    }
    manager->plugins = calloc(config->max_plugins, sizeof(*manager->plugins));
    if (manager->plugins == NULL) {
        free(manager);
        return NULL;
    }
    manager->contract = config->contract;
    manager->max_plugins = config->max_plugins;
    if ((config->lock == NULL) != (config->unlock == NULL)) {
        free(manager->plugins);
        free(manager);
        return NULL;
    }
    manager->lock = config->lock;
    manager->unlock = config->unlock;
    manager->lock_context = config->lock_context;
    return manager;
}

enum pm_manager_state pm_manager_state_get(const struct pm_manager *manager)
{
    return manager == NULL ? PM_MANAGER_IDLE : manager->state;
}

void pm_manager_destroy(struct pm_manager *manager)
{
    size_t i;

    if (manager == NULL) {
        return;
    }
    for (i = 0u; i < manager->plugin_count; ++i) {
        if (manager->plugins[i].instance_active) {
            (void)manager->plugins[i].descriptor->destroy(
                manager->plugins[i].instance);
        }
        release_record_instance_services(&manager->plugins[i]);
        release_record_image(&manager->plugins[i]);
    }
    free(manager->plugins);
    free(manager);
}

static int has_suffix(const char *name, const char *suffix)
{
    size_t name_length;
    size_t suffix_length;

    if (name == NULL) {
        return 0;
    }
    if (suffix == NULL) {
        suffix = PM_PACKAGE_DEFAULT_SUFFIX;
    }
    if (suffix[0] == '\0') {
        return 0;
    }
    name_length = strlen(name);
    suffix_length = strlen(suffix);
    return name_length >= suffix_length &&
           strcmp(name + name_length - suffix_length, suffix) == 0;
}

int pm_manager_scan(struct pm_manager *manager,
                    const struct pm_manager_scan_config *config)
{
    size_t index;
    int present;
    int status;
    size_t name_length;
    uint32_t package_size;
    uint8_t *package;
    struct pm_package_manifest manifest;
    char name[PM_MANAGER_MAX_NAME + 1u];

    if (manager == NULL || config == NULL || config->storage == NULL ||
        config->storage->list == NULL || config->storage->size == NULL ||
        config->storage->read == NULL || config->storage->max_entries == 0u ||
        config->max_package_size == 0u ||
        config->max_name_length == 0u ||
        config->max_name_length > PM_MANAGER_MAX_NAME ||
        (config->candidate_suffix != NULL &&
         config->candidate_suffix[0] == '\0')) {
        return PM_EINVAL;
    }
    status = manager_enter(manager, PM_MANAGER_SCANNING);
    if (status != PM_OK) {
        return status;
    }
    memset(manager->available, 0, sizeof(manager->available));
    manager->available_storage = *config->storage;
    manager->available_storage_valid = 1;
    manager->available_count = 0u;
    for (index = 0u; index < config->storage->max_entries; ++index) {
        memset(name, 0, sizeof(name));
        present = 0;
        status = config->storage->list(config->storage->context, index, name,
                                       config->max_name_length + 1u, &present);
        if (status != PM_OK) {
            manager_leave(manager);
            return status;
        }
        if (!present || !has_suffix(name, config->candidate_suffix)) {
            continue;
        }
        name_length = strlen(name);
        if (name_length == 0u || name_length > config->max_name_length ||
            manager->available_count == PM_MANAGER_MAX_DISCOVERED) {
            continue;
        }
        status = config->storage->size(config->storage->context, name,
                                       &package_size);
        if (status != PM_OK || package_size > config->max_package_size ||
            package_size < PM_PACKAGE_HEADER_SIZE) {
            continue;
        }
        package = malloc(package_size);
        if (package == NULL) {
            manager_leave(manager);
            return PM_ENOMEM;
        }
        status = config->storage->read(config->storage->context, name, 0u,
                                       package, package_size);
        if (status != PM_OK ||
            pm_package_parse(package, package_size, &manifest) != PM_OK) {
            free(package);
            continue;
        }
        if (!pm_uuid_equal(&manifest.application_id,
                           &manager->contract->application_id)) {
            free(package);
            continue;
        }
        if (available_has_plugin_id(manager, manifest.plugin_id)) {
            free(package);
            continue;
        }
        memcpy(manager->available_names[manager->available_count], name,
               name_length + 1u);
        memcpy(manager->available_plugin_names[manager->available_count],
               manifest.name, manifest.name_size + 1u);
        memcpy(manager->available_versions[manager->available_count],
               manifest.version, manifest.version_size + 1u);
        manager->available[manager->available_count].plugin_id = manifest.plugin_id;
        manager->available[manager->available_count].package_size = package_size;
        manager->available[manager->available_count].application_id = manifest.application_id;
        manager->available[manager->available_count].interface_id = manifest.interface_id;
        manager->available[manager->available_count].interface_version = manifest.interface_version;
        manager->available[manager->available_count].name =
            manager->available_plugin_names[manager->available_count];
        manager->available[manager->available_count].version =
            manager->available_versions[manager->available_count];
        manager->available[manager->available_count].storage_name =
            manager->available_names[manager->available_count];
        free(package);
        ++manager->available_count;
    }
    manager_leave(manager);
    return PM_OK;
}

size_t pm_manager_available_count(const struct pm_manager *manager)
{
    return manager == NULL ? 0u : manager->available_count;
}

int pm_manager_available_get(const struct pm_manager *manager, size_t index,
                             struct pm_available_plugin *result)
{
    if (manager == NULL || result == NULL) {
        return PM_EINVAL;
    }
    if (index >= manager->available_count) {
        return PM_ENOENT;
    }
    *result = manager->available[index];
    return PM_OK;
}

int pm_manager_load_available(struct pm_manager *manager, size_t index,
                              const struct pm_manager_package_config *config,
                              uint32_t *plugin_id)
{
    if (manager == NULL || config == NULL || plugin_id == NULL) {
        return PM_EINVAL;
    }
    if (index >= manager->available_count) {
        return PM_ENOENT;
    }
    {
        uint8_t *package = malloc(manager->available[index].package_size);
        int status;

        if (package == NULL) {
            return PM_ENOMEM;
        }
        if (!manager->available_storage_valid) {
            free(package);
            return PM_EPERM;
        }
        status = manager->available_storage.read(
            manager->available_storage.context,
            manager->available[index].storage_name, 0u, package,
            manager->available[index].package_size);
        if (status != PM_OK) {
            free(package);
            return status;
        }
        status = pm_manager_load_package(manager, package,
                                         manager->available[index].package_size,
                                         config, plugin_id);
        free(package);
        return status;
    }
}

static struct pm_plugin_record *find_plugin(struct pm_manager *manager,
                                            uint32_t plugin_id)
{
    size_t i;

    for (i = 0u; i < manager->plugin_count; ++i) {
        if (manager->plugins[i].descriptor->plugin_id == plugin_id) {
            return &manager->plugins[i];
        }
    }
    return NULL;
}

static int ranges_overlap(const uint8_t *left, size_t left_size,
                          const uint8_t *right, size_t right_size)
{
    uintptr_t left_start;
    uintptr_t right_start;

    if (left == NULL || right == NULL || left_size == 0u || right_size == 0u) {
        return 0;
    }
    left_start = (uintptr_t)left;
    right_start = (uintptr_t)right;
    return left_start < right_start + right_size &&
           right_start < left_start + left_size;
}

static int storage_overlaps(const struct pm_manager *manager,
                            const struct pm_manager_package_config *config)
{
    size_t i;
    const struct pm_plugin_record *record;

    for (i = 0u; i < manager->plugin_count; ++i) {
        record = &manager->plugins[i];
        if (ranges_overlap(config->text_storage, config->text_capacity,
                           record->text_storage, record->text_capacity) ||
            ranges_overlap(config->rodata_storage, config->rodata_capacity,
                           record->rodata_storage, record->rodata_capacity) ||
            ranges_overlap(config->data_storage, config->data_capacity,
                           record->data_storage, record->data_capacity) ||
            ranges_overlap(config->bss_storage, config->bss_capacity,
                           record->bss_storage, record->bss_capacity)) {
            return 1;
        }
    }
    return 0;
}

static int available_has_plugin_id(const struct pm_manager *manager,
                                   uint32_t plugin_id)
{
    size_t index;

    for (index = 0u; index < manager->available_count; ++index) {
        if (manager->available[index].plugin_id == plugin_id) {
            return 1;
        }
    }
    return 0;
}

static int register_descriptor(struct pm_manager *manager,
                               const struct pm_plugin_descriptor *descriptor,
                               int loaded, const struct pm_elf_image *image)
{
    const struct pm_plugin_interface *interface;
    size_t i;
    int status;

    status = pm_descriptor_validate(descriptor, &manager->contract->application_id);
    if (status != PM_OK) {
        return status;
    }
    status = pm_contract_find_interface(manager->contract, &descriptor->interface_id,
                                        descriptor->interface_version, &interface);
    if (status != PM_OK) {
        return status;
    }
    if (interface->validate != NULL) {
        status = interface->validate(descriptor);
        if (status != PM_OK) {
            return status;
        }
    }
    for (i = 0u; i < manager->plugin_count; ++i) {
        if (manager->plugins[i].descriptor->plugin_id == descriptor->plugin_id) {
            return PM_EEXIST;
        }
    }
    if (manager->plugin_count == manager->max_plugins) {
        return PM_ENOMEM;
    }
    if (loaded) {
        status = own_loaded_descriptor(&manager->plugins[manager->plugin_count],
                                       descriptor);
        if (status != PM_OK) {
            return status;
        }
    } else {
        manager->plugins[manager->plugin_count].descriptor = descriptor;
    }
    manager->plugins[manager->plugin_count].interface = interface;
    manager->plugins[manager->plugin_count].loaded = loaded;
    if (image != NULL) {
        manager->plugins[manager->plugin_count].image = *image;
    }
    ++manager->plugin_count;
    return PM_OK;
}

int pm_manager_register_static(struct pm_manager *manager,
                               const struct pm_plugin_descriptor *descriptor)
{
    if (manager == NULL || descriptor == NULL) {
        return PM_EINVAL;
    }
    if (manager_enter(manager, PM_MANAGER_REGISTERING) != PM_OK) {
        return PM_EBUSY;
    }
    {
        int status = register_descriptor(manager, descriptor, 0, NULL);
        manager_leave(manager);
        return status;
    }
}

int pm_manager_load_package(struct pm_manager *manager, const uint8_t *package,
                            size_t package_size,
                            const struct pm_manager_package_config *config,
                            uint32_t *plugin_id)
{
    struct pm_package_manifest manifest;
    struct pm_elf_image image;
    const struct pm_plugin_descriptor *descriptor = NULL;
    struct pm_manager_package_config effective;
    uint8_t *allocated_text = NULL;
    uint8_t *allocated_rodata = NULL;
    uint8_t *allocated_data = NULL;
    uint8_t *allocated_bss = NULL;
    int allocated = 0;
    size_t i;
    int status;

    if (manager == NULL || package == NULL || config == NULL ||
        config->elf_profile == NULL ||
        (config->get_descriptor == NULL && config->adapt_wire_descriptor == NULL) ||
        plugin_id == NULL) {
        return PM_EINVAL;
    }
    status = manager_enter(manager, PM_MANAGER_LOADING);
    if (status != PM_OK) {
        return status;
    }
    status = pm_package_parse(package, package_size, &manifest);
    if (status != PM_OK ||
        !pm_uuid_equal(&manifest.application_id, &manager->contract->application_id)) {
        status = status == PM_OK ? PM_EPROTO : status;
        manager_leave(manager);
        return status;
    }
    if (manifest.image_size > config->elf_profile->max_image_size ||
        manifest.ram_size > config->elf_profile->max_ram_size) {
        manager_leave(manager);
        return PM_EOVERFLOW;
    }
    effective = *config;
    if (config->alloc_image != NULL) {
        if (config->free_image == NULL) {
            manager_leave(manager);
            return PM_EINVAL;
        }
        status = config->alloc_image(config->image_allocator_context,
                                     manifest.image_size, manifest.image_size,
                                     manifest.ram_size, manifest.ram_size,
                                     &allocated_text, &allocated_rodata,
                                     &allocated_data, &allocated_bss);
        if (status != PM_OK) {
            config->free_image(config->image_allocator_context,
                               allocated_text, allocated_rodata,
                               allocated_data, allocated_bss);
            manager_leave(manager);
            return status;
        }
        effective.text_storage = allocated_text;
        effective.text_capacity = manifest.image_size;
        effective.rodata_storage = allocated_rodata;
        effective.rodata_capacity = manifest.image_size;
        effective.data_storage = allocated_data;
        effective.data_capacity = manifest.ram_size;
        effective.bss_storage = allocated_bss;
        effective.bss_capacity = manifest.ram_size;
        allocated = 1;
    }
    if (storage_overlaps(manager, &effective)) {
        if (allocated) {
            config->free_image(config->image_allocator_context,
                               allocated_text, allocated_rodata,
                               allocated_data, allocated_bss);
        }
        manager_leave(manager);
        return PM_EBUSY;
    }
    status = pm_elf_load(manifest.image, manifest.image_size, effective.elf_profile,
                         effective.text_storage, effective.text_capacity,
                         effective.rodata_storage, effective.rodata_capacity,
                         effective.data_storage, effective.data_capacity,
                         effective.bss_storage, effective.bss_capacity,
                         effective.resolve, effective.resolve_context, &image);
    if (status != PM_OK) {
        if (allocated) {
            config->free_image(config->image_allocator_context,
                               allocated_text, allocated_rodata,
                               allocated_data, allocated_bss);
        }
        manager_leave(manager);
        return status;
    }
    if (config->adapt_wire_descriptor != NULL) {
        struct pm_wire_plugin_descriptor wire;

        status = pm_wire_descriptor_decode(&image, image.descriptor_offset, &wire);
        if (status == PM_OK) {
            status = effective.adapt_wire_descriptor(&wire, &image, &manifest,
                                                   &descriptor,
                                                    effective.wire_descriptor_context);
        }
    } else {
        status = effective.get_descriptor(&image, &manifest, &descriptor,
                                          effective.descriptor_context);
    }
    if (status != PM_OK || descriptor == NULL) {
        status = status == PM_OK ? PM_EPROTO : status;
        if (allocated) {
            config->free_image(config->image_allocator_context,
                               allocated_text, allocated_rodata,
                               allocated_data, allocated_bss);
        }
        manager_leave(manager);
        return status;
    }
    if (descriptor->plugin_id != manifest.plugin_id ||
        !pm_uuid_equal(&descriptor->application_id, &manifest.application_id) ||
        !pm_uuid_equal(&descriptor->interface_id, &manifest.interface_id) ||
        descriptor->interface_version != manifest.interface_version ||
        descriptor->required_service_count != manifest.required_service_count) {
        if (allocated) {
            config->free_image(config->image_allocator_context,
                               allocated_text, allocated_rodata,
                               allocated_data, allocated_bss);
        }
        manager_leave(manager);
        return PM_EPROTO;
    }
    for (i = 0u; i < manifest.required_service_count; ++i) {
        if (!pm_uuid_equal(&descriptor->required_service_ids[i],
                           &manifest.required_service_ids[i])) {
            if (allocated) {
                config->free_image(config->image_allocator_context,
                                   allocated_text, allocated_rodata,
                                   allocated_data, allocated_bss);
            }
            manager_leave(manager);
            return PM_EPROTO;
        }
    }
    status = register_descriptor(manager, descriptor, 1, &image);
    if (status == PM_OK) {
        struct pm_plugin_record *record = &manager->plugins[manager->plugin_count - 1u];

        record->text_storage = effective.text_storage;
        record->text_capacity = effective.text_capacity;
        record->rodata_storage = effective.rodata_storage;
        record->rodata_capacity = effective.rodata_capacity;
        record->data_storage = effective.data_storage;
        record->data_capacity = effective.data_capacity;
        record->bss_storage = effective.bss_storage;
        record->bss_capacity = effective.bss_capacity;
        record->free_image = allocated ? config->free_image : NULL;
        record->image_allocator_context = config->image_allocator_context;
        *plugin_id = descriptor->plugin_id;
    } else if (allocated) {
        config->free_image(config->image_allocator_context,
                           allocated_text, allocated_rodata,
                           allocated_data, allocated_bss);
    }
    manager_leave(manager);
    return status;
}

int pm_manager_unload(struct pm_manager *manager, uint32_t plugin_id)
{
    struct pm_plugin_record *record;
    size_t index;

    if (manager == NULL) {
        return PM_EINVAL;
    }
    if (manager_enter(manager, PM_MANAGER_UNLOADING) != PM_OK) {
        return PM_EBUSY;
    }
    record = find_plugin(manager, plugin_id);
    if (record == NULL) {
        manager_leave(manager);
        return PM_ENOENT;
    }
    if (!record->loaded) {
        manager_leave(manager);
        return PM_ENOTSUP;
    }
    if (record->instance_active) {
        manager_leave(manager);
        return PM_EBUSY;
    }
    release_record_image(record);
    index = (size_t)(record - manager->plugins);
    if (index + 1u < manager->plugin_count) {
        memmove(record, record + 1u,
                (manager->plugin_count - index - 1u) * sizeof(*record));
    }
    --manager->plugin_count;
    for (index = 0u; index < manager->plugin_count; ++index) {
        rebind_loaded_record(&manager->plugins[index]);
    }
    memset(&manager->plugins[manager->plugin_count], 0,
           sizeof(manager->plugins[manager->plugin_count]));
    manager_leave(manager);
    return PM_OK;
}

int pm_manager_create_instance(struct pm_manager *manager, uint32_t plugin_id)
{
    struct pm_plugin_record *record;
    struct pm_service *services;
    struct pm_service_table *table;
    void *instance = NULL;
    int status;

    if (manager == NULL) {
        return PM_EINVAL;
    }
    status = manager_enter(manager, PM_MANAGER_CREATING);
    if (status != PM_OK) {
        return status;
    }
    record = find_plugin(manager, plugin_id);
    if (record == NULL) {
        manager_leave(manager);
        return PM_ENOENT;
    }
    if (record->instance_active) {
        manager_leave(manager);
        return PM_EBUSY;
    }
    services = calloc(record->descriptor->required_service_count,
                      sizeof(*services));
    if (record->descriptor->required_service_count > 0u && services == NULL) {
        manager_leave(manager);
        return PM_ENOMEM;
    }
    table = calloc(1u, sizeof(*table));
    if (table == NULL) {
        free(services);
        manager_leave(manager);
        return PM_ENOMEM;
    }
    status = pm_contract_build_services(manager->contract, record->descriptor,
                                         table, services,
                                         record->descriptor->required_service_count);
    if (status == PM_OK) {
        status = record->descriptor->create(table, &instance);
    }
    if (status != PM_OK) {
        free(services);
        free(table);
        manager_leave(manager);
        return status;
    }
    if (instance == NULL) {
        free(services);
        free(table);
        manager_leave(manager);
        return PM_EPROTO;
    }
    record->instance = instance;
    record->instance_active = 1;
    record->services = services;
    record->service_table = table;
    manager_leave(manager);
    return PM_OK;
}

int pm_manager_destroy_instance(struct pm_manager *manager, uint32_t plugin_id)
{
    struct pm_plugin_record *record;
    int status;

    if (manager == NULL) {
        return PM_EINVAL;
    }
    status = manager_enter(manager, PM_MANAGER_DESTROYING);
    if (status != PM_OK) {
        return status;
    }
    record = find_plugin(manager, plugin_id);
    if (record == NULL) {
        manager_leave(manager);
        return PM_ENOENT;
    }
    if (!record->instance_active) {
        manager_leave(manager);
        return PM_ENOENT;
    }
    status = record->descriptor->destroy(record->instance);
    if (status == PM_OK) {
        record->instance = NULL;
        record->instance_active = 0;
        release_record_instance_services(record);
    }
    manager_leave(manager);
    return status;
}

int pm_manager_get_instance(struct pm_manager *manager, uint32_t plugin_id,
                            void **instance)
{
    struct pm_plugin_record *record;

    if (manager == NULL || instance == NULL) {
        return PM_EINVAL;
    }
    record = find_plugin(manager, plugin_id);
    if (record == NULL || !record->instance_active) {
        return PM_ENOENT;
    }
    *instance = record->instance;
    return PM_OK;
}

int pm_manager_filesystem_attach(struct pm_manager *manager,
                                 const void *identity)
{
    if (manager == NULL || identity == NULL) {
        return PM_EINVAL;
    }
    if (manager->filesystem_attached) {
        return PM_EBUSY;
    }
    manager->filesystem_attached = 1;
    manager->filesystem_identity = identity;
    return PM_OK;
}

int pm_manager_filesystem_detach(struct pm_manager *manager)
{
    if (manager == NULL) {
        return PM_EINVAL;
    }
    if (manager->state != PM_MANAGER_IDLE || manager->plugin_count != 0u) {
        return PM_EBUSY;
    }
    manager->available_count = 0u;
    memset(&manager->available_storage, 0, sizeof(manager->available_storage));
    manager->available_storage_valid = 0;
    manager->filesystem_attached = 0;
    manager->filesystem_identity = NULL;
    return PM_OK;
}

int pm_manager_filesystem_is_attached(const struct pm_manager *manager)
{
    return manager != NULL && manager->filesystem_attached;
}

int pm_manager_filesystem_is_same(const struct pm_manager *manager,
                                  const void *identity)
{
    return manager != NULL && manager->filesystem_attached &&
           identity != NULL && manager->filesystem_identity == identity;
}
