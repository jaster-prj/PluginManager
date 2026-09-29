#include "plugin_manager/plugin_manager.h"
#include "plugin_manager/plugin_package.h"
#include "plugin_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const pm_uuid_t APPLICATION = {{1u}};
static const pm_uuid_t INTERFACE = {{2u}};
static const pm_uuid_t SERVICE = {{3u}};
static int created;
static int destroyed;
static struct pm_manager *callback_manager;
static int callback_reentry_status;
static struct pm_plugin_descriptor *bound_descriptor;
static int image_allocated;
static int image_freed;
static int allocator_fail;
static int allocator_calls;
static int storage_read_generation;
static const struct pm_service_table *retained_service_table;

static size_t make_package(uint8_t *buffer, size_t capacity);


static int allocate_image(void *context, size_t text_size, size_t rodata_size,
                          size_t data_size, size_t bss_size, uint8_t **text,
                          uint8_t **rodata, uint8_t **data, uint8_t **bss)
{
    (void)context;
    ++allocator_calls;
    if (allocator_fail) {
        *text = NULL; *rodata = NULL; *data = NULL; *bss = NULL;
        return PM_ENOMEM;
    }
    *text = malloc(text_size);
    *rodata = malloc(rodata_size);
    *data = malloc(data_size);
    *bss = malloc(bss_size);
    if (*text == NULL || *rodata == NULL || *data == NULL || *bss == NULL) {
        free(*text); free(*rodata); free(*data); free(*bss);
        return PM_ENOMEM;
    }
    ++image_allocated;
    return PM_OK;
}

static int allocate_partial_image(void *context, size_t text_size,
                                  size_t rodata_size, size_t data_size,
                                  size_t bss_size, uint8_t **text,
                                  uint8_t **rodata, uint8_t **data,
                                  uint8_t **bss)
{
    (void)context;
    ++allocator_calls;
    *text = malloc(text_size);
    *rodata = malloc(rodata_size);
    *data = NULL;
    *bss = NULL;
    (void)data_size;
    (void)bss_size;
    return (*text != NULL && *rodata != NULL) ? PM_ENOMEM : PM_ENOMEM;
}

static void free_image(void *context, uint8_t *text, uint8_t *rodata,
                       uint8_t *data, uint8_t *bss)
{
    (void)context;
    free(text); free(rodata); free(data); free(bss);
    ++image_freed;
}

static int reentrant_list(void *context, size_t index, char *name,
                          size_t capacity, int *present)
{
    (void)context;
    (void)index;
    (void)name;
    (void)capacity;
    *present = 0;
    callback_reentry_status = pm_manager_create_instance(callback_manager, 77u);
    return PM_OK;
}

static int reentrant_size(void *context, const char *name, uint32_t *size)
{
    (void)context;
    (void)name;
    *size = 0u;
    return PM_OK;
}

static int reentrant_read(void *context, const char *name, uint32_t offset,
                          void *destination, size_t size)
{
    (void)context;
    (void)name;
    (void)offset;
    (void)destination;
    (void)size;
    return PM_OK;
}

static int mutable_storage_read(void *context, const char *name, uint32_t offset,
                                void *destination, size_t size)
{
    (void)context;
    (void)name;
    (void)offset;
    (void)destination;
    (void)size;
    ++storage_read_generation;
    return PM_EIO;
}

static int loaded_create(const struct pm_service_table *services, void **instance)
{
    assert(services->count == 0u);
    *instance = &created;
    return PM_OK;
}

static int loaded_destroy(void *instance)
{
    assert(instance == &created);
    ++destroyed;
    return PM_OK;
}

static int resolve_loaded_entry(void *context, uint32_t address,
                                enum pm_elf_entry_kind kind, void *out)
{
    (void)context;
    (void)kind;
    if (address == 1u) {
        *(pm_plugin_create_fn *)out = loaded_create;
    } else if (address == 3u) {
        *(pm_plugin_destroy_fn *)out = loaded_destroy;
    } else {
        return PM_ENOENT;
    }
    return PM_OK;
}

static int loaded_descriptor(
    const struct pm_elf_image *image,
    const struct pm_package_manifest *manifest,
    const struct pm_plugin_descriptor **descriptor,
    void *context)
{
    static const struct pm_plugin_descriptor value = {
        .framework_abi_version = PM_FRAMEWORK_ABI_VERSION,
        .descriptor_size = sizeof(struct pm_plugin_descriptor),
        .application_id = {{1u}}, .interface_id = {{2u}},
        .interface_version = 1u, .plugin_id = 99u,
        .name = "loaded-plugin", .version = "1.0",
        .create = loaded_create, .destroy = loaded_destroy,
    };

    (void)image;
    (void)manifest;
    (void)context;
    *descriptor = &value;
    return PM_OK;
}

static int adapt_wire_descriptor(
    const struct pm_wire_plugin_descriptor *wire,
    const struct pm_elf_image *image,
    const struct pm_package_manifest *manifest,
    const struct pm_plugin_descriptor **descriptor,
    void *context)
{
    static struct pm_plugin_descriptor value;
    struct pm_descriptor_bind_config bind = {
        .resolve = NULL,
    };

    (void)manifest;
    (void)context;
    bind.resolve = resolve_loaded_entry;
    if (pm_wire_descriptor_bind(wire, image, &bind, &value) != PM_OK) {
        return PM_EPROTO;
    }
    bound_descriptor = &value;
    *descriptor = &value;
    return PM_OK;
}

static int reject_wire_descriptor(
    const struct pm_wire_plugin_descriptor *wire,
    const struct pm_elf_image *image,
    const struct pm_package_manifest *manifest,
    const struct pm_plugin_descriptor **descriptor,
    void *context)
{
    (void)wire;
    (void)image;
    (void)manifest;
    (void)descriptor;
    (void)context;
    return PM_EPROTO;
}

static void test_loaded_package_lifecycle(void)
{
    uint8_t package[2800u];
    uint8_t text[64u];
    uint8_t rodata[256u];
    struct pm_elf_profile profile = {
        .max_sections = 16u, .max_image_size = 2048u, .max_ram_size = 64u,
        .rodata_address = 0x1000u,
    };
    struct pm_manager_package_config package_config = {
        .elf_profile = &profile,
        .text_storage = text, .text_capacity = sizeof(text),
        .rodata_storage = rodata, .rodata_capacity = sizeof(rodata),
        .adapt_wire_descriptor = adapt_wire_descriptor,
    };
    static const struct pm_plugin_interface interface = {
        .interface_id = {{2u}}, .version = 1u,
    };
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
        .interfaces = &interface, .interface_count = 1u,
    };
    struct pm_manager_config config = {.contract = &contract, .max_plugins = 1u};
    struct pm_manager *manager = pm_manager_create(&config);
    uint32_t plugin_id = 0u;
    int load_status;
    size_t package_size = pm_test_make_loaded_package(package, sizeof(package));

    assert(manager != NULL);
    load_status = pm_manager_load_package(manager, package, package_size,
                                          &package_config, &plugin_id);
    assert(load_status == PM_OK);
    assert(plugin_id == 99u);
    assert(pm_manager_create_instance(manager, plugin_id) == PM_OK);
    assert(pm_manager_unload(manager, plugin_id) == PM_EBUSY);
    assert(pm_manager_destroy_instance(manager, plugin_id) == PM_OK);
    assert(pm_manager_unload(manager, plugin_id) == PM_OK);
    assert(pm_manager_unload(manager, plugin_id) == PM_ENOENT);
    pm_manager_destroy(manager);
}

static void test_loaded_storage_cannot_be_reused(void)
{
    uint8_t package[2800u];
    uint8_t text[64u];
    uint8_t rodata[256u];
    struct pm_elf_profile profile = {
        .max_sections = 16u, .max_image_size = 2048u, .max_ram_size = 64u,
        .rodata_address = 0x1000u,
    };
    struct pm_manager_package_config package_config = {
        .elf_profile = &profile,
        .text_storage = text, .text_capacity = sizeof(text),
        .rodata_storage = rodata, .rodata_capacity = sizeof(rodata),
        .adapt_wire_descriptor = adapt_wire_descriptor,
    };
    static const struct pm_plugin_interface interface = {
        .interface_id = {{2u}}, .version = 1u,
    };
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
        .interfaces = &interface, .interface_count = 1u,
    };
    struct pm_manager_config config = {.contract = &contract, .max_plugins = 2u};
    struct pm_manager *manager = pm_manager_create(&config);
    uint32_t plugin_id;
    size_t package_size = pm_test_make_loaded_package(package, sizeof(package));

    assert(manager != NULL);
    assert(pm_manager_load_package(manager, package, package_size,
                                   &package_config, &plugin_id) == PM_OK);
    assert(pm_manager_load_package(manager, package, package_size,
                                   &package_config, &plugin_id) == PM_EBUSY);
    assert(pm_manager_unload(manager, 99u) == PM_OK);
    assert(pm_manager_load_package(manager, package, package_size,
                                   &package_config, &plugin_id) == PM_OK);
    assert(pm_manager_unload(manager, 99u) == PM_OK);
    pm_manager_destroy(manager);
}

static void test_loaded_descriptor_is_owned(void)
{
    uint8_t package[2800u];
    uint8_t text[64u];
    uint8_t rodata[256u];
    struct pm_elf_profile profile = {
        .max_sections = 16u, .max_image_size = 2048u, .max_ram_size = 64u,
        .rodata_address = 0x1000u,
    };
    struct pm_manager_package_config package_config = {
        .elf_profile = &profile, .text_storage = text, .text_capacity = sizeof(text),
        .rodata_storage = rodata, .rodata_capacity = sizeof(rodata),
        .adapt_wire_descriptor = adapt_wire_descriptor,
    };
    static const struct pm_plugin_interface interface = {
        .interface_id = {{2u}}, .version = 1u,
    };
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
        .interfaces = &interface, .interface_count = 1u,
    };
    struct pm_manager_config config = {.contract = &contract, .max_plugins = 1u};
    struct pm_manager *manager = pm_manager_create(&config);
    uint32_t plugin_id;

    assert(manager != NULL);
    assert(pm_manager_load_package(manager, package,
                                   pm_test_make_loaded_package(package, sizeof(package)),
                                   &package_config, &plugin_id) == PM_OK);
    assert(bound_descriptor != NULL);
    bound_descriptor->plugin_id = 100u;
    assert(pm_manager_create_instance(manager, 99u) == PM_OK);
    assert(pm_manager_destroy_instance(manager, 99u) == PM_OK);
    assert(pm_manager_unload(manager, 99u) == PM_OK);
    pm_manager_destroy(manager);
}

static void test_manager_owned_image_lifetime(void)
{
    uint8_t package[2800u];
    struct pm_elf_profile profile = {
        .max_sections = 16u, .max_image_size = 2048u, .max_ram_size = 64u,
        .rodata_address = 0x1000u,
    };
    struct pm_manager_package_config package_config = {
        .elf_profile = &profile, .adapt_wire_descriptor = adapt_wire_descriptor,
        .alloc_image = allocate_image, .free_image = free_image,
    };
    static const struct pm_plugin_interface interface = {
        .interface_id = {{2u}}, .version = 1u,
    };
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
        .interfaces = &interface, .interface_count = 1u,
    };
    struct pm_manager_config config = {.contract = &contract, .max_plugins = 1u};
    struct pm_manager *manager = pm_manager_create(&config);
    uint32_t plugin_id;

    image_allocated = 0;
    image_freed = 0;
    assert(manager != NULL);
    assert(pm_manager_load_package(manager, package,
                                   pm_test_make_loaded_package(package, sizeof(package)),
                                   &package_config, &plugin_id) == PM_OK);
    assert(image_allocated == 1 && image_freed == 0);
    assert(pm_manager_unload(manager, plugin_id) == PM_OK);
    assert(image_freed == 1);
    pm_manager_destroy(manager);
}

static void test_allocator_failure_paths(void)
{
    uint8_t package[2800u];
    struct pm_elf_profile profile = {
        .max_sections = 16u, .max_image_size = 2048u, .max_ram_size = 64u,
        .rodata_address = 0x1000u,
    };
    struct pm_manager_package_config package_config = {
        .elf_profile = &profile, .adapt_wire_descriptor = adapt_wire_descriptor,
        .alloc_image = allocate_image, .free_image = free_image,
    };
    static const struct pm_plugin_interface interface = {
        .interface_id = {{2u}}, .version = 1u,
    };
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
        .interfaces = &interface, .interface_count = 1u,
    };
    struct pm_manager_config config = {.contract = &contract, .max_plugins = 1u};
    struct pm_manager *manager = pm_manager_create(&config);
    uint32_t plugin_id;

    allocator_fail = 1;
    allocator_calls = 0;
    assert(manager != NULL);
    assert(pm_manager_load_package(manager, package,
                                   pm_test_make_loaded_package(package, sizeof(package)),
                                   &package_config, &plugin_id) == PM_ENOMEM);
    assert(allocator_calls == 1);
    assert(pm_manager_available_count(manager) == 0u);
    pm_manager_destroy(manager);

    manager = pm_manager_create(&config);
    package_config.alloc_image = allocate_partial_image;
    allocator_fail = 0;
    allocator_calls = 0;
    image_freed = 0;
    assert(pm_manager_load_package(manager, package,
                                   pm_test_make_loaded_package(package, sizeof(package)),
                                   &package_config, &plugin_id) == PM_ENOMEM);
    assert(allocator_calls == 1);
    assert(image_freed == 1);
    assert(pm_manager_available_count(manager) == 0u);
    pm_manager_destroy(manager);
}

static void test_allocated_image_released_after_descriptor_failure(void)
{
    uint8_t package[2800u];
    struct pm_elf_profile profile = {
        .max_sections = 16u, .max_image_size = 2048u, .max_ram_size = 64u,
        .rodata_address = 0x1000u,
    };
    struct pm_manager_package_config package_config = {
        .elf_profile = &profile, .adapt_wire_descriptor = reject_wire_descriptor,
        .alloc_image = allocate_image, .free_image = free_image,
    };
    static const struct pm_plugin_interface interface = {
        .interface_id = {{2u}}, .version = 1u,
    };
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
        .interfaces = &interface, .interface_count = 1u,
    };
    struct pm_manager_config config = {.contract = &contract, .max_plugins = 1u};
    struct pm_manager *manager = pm_manager_create(&config);
    uint32_t plugin_id;

    image_allocated = 0;
    image_freed = 0;
    assert(manager != NULL);
    assert(pm_manager_load_package(manager, package,
                                   pm_test_make_loaded_package(package, sizeof(package)),
                                   &package_config, &plugin_id) == PM_EPROTO);
    assert(image_allocated == 1);
    assert(image_freed == 1);
    pm_manager_destroy(manager);
}

static void test_allocator_requires_release_callback(void)
{
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
    };
    struct pm_manager_config config = {.contract = &contract, .max_plugins = 1u};
    struct pm_manager *manager = pm_manager_create(&config);
    struct pm_manager_package_config package_config = {
        .alloc_image = allocate_image,
    };
    uint8_t package[2800u];
    uint32_t plugin_id;

    assert(manager != NULL);
    assert(pm_manager_load_package(manager, package,
                                   pm_test_make_loaded_package(package, sizeof(package)),
                                   &package_config, &plugin_id) == PM_EINVAL);
    pm_manager_destroy(manager);
}

static void test_available_load_rereads_storage(void)
{
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
    };
    static const struct pm_storage_adapter storage = {
        .list = reentrant_list, .size = reentrant_size,
        .read = mutable_storage_read, .max_entries = 1u,
    };
    struct pm_manager_config config = {.contract = &contract, .max_plugins = 1u};
    struct pm_manager *manager = pm_manager_create(&config);
    struct pm_manager_scan_config scan = {
        .storage = &storage, .candidate_suffix = NULL,
        .max_name_length = 32u, .max_package_size = 256u,
    };

    /* Empty discovery leaves no selected package; the API remains safe. */
    assert(manager != NULL);
    assert(pm_manager_scan(manager, &scan) == PM_OK);
    assert(pm_manager_load_available(manager, 0u, NULL, NULL) == PM_EINVAL);
    pm_manager_destroy(manager);
}

struct discovery_fixture {
    uint8_t valid[132u];
    uint8_t corrupt[132u];
};

static int discovery_list(void *context, size_t index, char *name,
                          size_t capacity, int *present)
{
    (void)context;
    if (index == 0u) {
        strncpy(name, "corrupt.pmp", capacity); *present = 1;
    } else if (index == 1u) {
        strncpy(name, "valid.pmp", capacity); *present = 1;
    } else if (index == 2u) {
        strncpy(name, "read-error.pmp", capacity); *present = 1;
    } else {
        *present = 0;
    }
    return PM_OK;
}

static int discovery_size(void *context, const char *name, uint32_t *size)
{
    (void)context;
    *size = strcmp(name, "read-error.pmp") == 0 ? 132u : 132u;
    return PM_OK;
}

static int discovery_read(void *context, const char *name, uint32_t offset,
                          void *destination, size_t size)
{
    struct discovery_fixture *fixture = context;
    if (strcmp(name, "read-error.pmp") == 0) {
        return PM_EIO;
    }
    if ((size_t)offset + size > sizeof(fixture->valid)) {
        return PM_EOVERFLOW;
    }
    memcpy(destination,
           strcmp(name, "corrupt.pmp") == 0 ? fixture->corrupt : fixture->valid,
           size);
    return PM_OK;
}

static void test_scan_skips_corrupt_and_read_error_candidates(void)
{
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
    };
    struct discovery_fixture fixture;
    struct pm_storage_adapter storage;
    struct pm_manager_scan_config scan;
    struct pm_manager_config config = {.contract = &contract, .max_plugins = 2u};
    struct pm_manager *manager;
    struct pm_available_plugin available;

    memcpy(fixture.valid, (uint8_t[132u]){0}, sizeof(fixture.valid));
    (void)make_package(fixture.valid, sizeof(fixture.valid));
    memcpy(fixture.corrupt, fixture.valid, sizeof(fixture.corrupt));
    fixture.corrupt[0] ^= 0xffu;
    storage = (struct pm_storage_adapter){
        .context = &fixture, .list = discovery_list, .size = discovery_size,
        .read = discovery_read, .max_entries = 3u,
    };
    scan = (struct pm_manager_scan_config){
        .storage = &storage, .candidate_suffix = NULL,
        .max_name_length = 32u, .max_package_size = sizeof(fixture.valid),
    };
    manager = pm_manager_create(&config);
    assert(manager != NULL);
    assert(pm_manager_scan(manager, &scan) == PM_OK);
    assert(pm_manager_available_count(manager) == 1u);
    assert(pm_manager_available_get(manager, 0u, &available) == PM_OK);
    assert(strcmp(available.storage_name, "valid.pmp") == 0);
    pm_manager_destroy(manager);
}

static void test_scan_copies_storage_adapter(void)
{
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
    };
    struct discovery_fixture fixture;
    struct pm_storage_adapter storage = {
        .context = &fixture, .list = discovery_list, .size = discovery_size,
        .read = discovery_read, .max_entries = 3u,
    };
    struct pm_manager_scan_config scan = {
        .storage = &storage, .candidate_suffix = NULL,
        .max_name_length = 32u, .max_package_size = sizeof(fixture.valid),
    };
    struct pm_manager_config config = {.contract = &contract, .max_plugins = 1u};
    struct pm_manager_package_config package_config = {0};
    struct pm_manager *manager;
    uint32_t plugin_id;

    (void)make_package(fixture.valid, sizeof(fixture.valid));
    memcpy(fixture.corrupt, fixture.valid, sizeof(fixture.corrupt));
    fixture.corrupt[0] ^= 0xffu;
    manager = pm_manager_create(&config);
    assert(manager != NULL);
    assert(pm_manager_scan(manager, &scan) == PM_OK);
    assert(pm_manager_available_count(manager) == 1u);
    storage.read = NULL;
    assert(pm_manager_load_available(manager, 0u, &package_config,
                                     &plugin_id) == PM_EINVAL);
    pm_manager_destroy(manager);
}

static void write_u16(uint8_t *buffer, size_t offset, uint16_t value)
{
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1u] = (uint8_t)(value >> 8u);
}

static void write_u32(uint8_t *buffer, size_t offset, uint32_t value)
{
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1u] = (uint8_t)(value >> 8u);
    buffer[offset + 2u] = (uint8_t)(value >> 16u);
    buffer[offset + 3u] = (uint8_t)(value >> 24u);
}

static size_t make_package(uint8_t *buffer, size_t capacity)
{
    static const uint8_t image[] = {0x01u, 0x02u, 0x03u, 0x04u};
    const size_t image_offset = 128u;
    const size_t name_offset = 108u;
    const size_t version_offset = 120u;
    const size_t total_size = image_offset + sizeof(image);

    assert(capacity >= total_size);
    memset(buffer, 0, total_size);
    write_u32(buffer, 0u, PM_PACKAGE_MAGIC);
    write_u16(buffer, 4u, PM_PACKAGE_FORMAT_VERSION);
    write_u16(buffer, 6u, PM_PACKAGE_HEADER_SIZE);
    write_u32(buffer, 8u, (uint32_t)total_size);
    buffer[12u] = 1u;
    buffer[28u] = 2u;
    write_u32(buffer, 44u, 1u);
    write_u32(buffer, 48u, 42u);
    write_u16(buffer, 52u, PM_PACKAGE_TARGET_ARM_CORTEX_M);
    write_u32(buffer, 56u, (uint32_t)image_offset);
    write_u32(buffer, 60u, (uint32_t)sizeof(image));
    write_u32(buffer, 64u, 256u);
    write_u32(buffer, 68u, 128u);
    write_u32(buffer, 76u, (uint32_t)name_offset);
    write_u32(buffer, 80u, 12u);
    write_u32(buffer, 84u, (uint32_t)version_offset);
    write_u32(buffer, 88u, 4u);
    memcpy(buffer + name_offset, "test-plugin", 12u);
    memcpy(buffer + version_offset, "1.0", 4u);
    memcpy(buffer + image_offset, image, sizeof(image));
    write_u32(buffer, 72u, pm_package_crc32(image, sizeof(image)));
    return total_size;
}

static void test_package_parser(void)
{
    uint8_t buffer[132u];
    struct pm_package_manifest manifest;
    size_t size = make_package(buffer, sizeof(buffer));

    assert(pm_package_parse(buffer, size, &manifest) == PM_OK);
    assert(manifest.plugin_id == 42u);
    assert(manifest.image_size == 4u);
    assert(strcmp(manifest.name, "test-plugin") == 0);
    assert(strcmp(manifest.version, "1.0") == 0);
    assert(manifest.ram_size == 256u);

    buffer[0u] = 0u;
    assert(pm_package_parse(buffer, size, &manifest) == PM_EPROTO);
    size = make_package(buffer, sizeof(buffer));
    write_u32(buffer, 60u, 200u);
    assert(pm_package_parse(buffer, size, &manifest) == PM_EPROTO);
    size = make_package(buffer, sizeof(buffer));
    buffer[128u] ^= 1u;
    assert(pm_package_parse(buffer, size, &manifest) == PM_EPROTO);
    size = make_package(buffer, sizeof(buffer));
    write_u32(buffer, 80u, 1000u);
    assert(pm_package_parse(buffer, size, &manifest) == PM_EPROTO);
}

static int plugin_create(const struct pm_service_table *services, void **instance)
{
    assert(services->count == 1u);
    assert(services->items[0].vtable != NULL);
    retained_service_table = services;
    *instance = &created;
    ++created;
    return PM_OK;
}

static int plugin_destroy(void *instance)
{
    assert(instance == &created);
    ++destroyed;
    return PM_OK;
}

static int validate_plugin(const struct pm_plugin_descriptor *descriptor)
{
    return strcmp(descriptor->name, "test-plugin") == 0 ? PM_OK : PM_EPROTO;
}

static void test_static_lifecycle(void)
{
    static int fake_service;
    static const struct pm_service services[] = {
        {.service_id = {{3u}}, .version = 1u, .vtable = &fake_service},
    };
    static const pm_uuid_t required[] = {{{3u}}};
    static const struct pm_plugin_interface interfaces[] = {
        {.interface_id = {{2u}}, .version = 1u, .validate = validate_plugin},
    };
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
        .interfaces = interfaces, .interface_count = 1u,
        .services = services, .service_count = 1u,
    };
    static const struct pm_plugin_descriptor descriptor = {
        .framework_abi_version = PM_FRAMEWORK_ABI_VERSION,
        .descriptor_size = sizeof(struct pm_plugin_descriptor),
        .application_id = {{1u}}, .interface_id = {{2u}},
        .interface_version = 1u, .plugin_id = 42u,
        .required_service_ids = required, .required_service_count = 1u,
        .name = "test-plugin", .version = "1.0",
        .create = plugin_create, .destroy = plugin_destroy,
    };
    struct pm_manager_config config = {.contract = &contract, .max_plugins = 2u};
    struct pm_manager *manager = pm_manager_create(&config);
    void *instance = NULL;

    assert(manager != NULL);
    assert(pm_manager_register_static(manager, &descriptor) == PM_OK);
    assert(pm_manager_register_static(manager, &descriptor) == PM_EEXIST);
    assert(pm_manager_create_instance(manager, 42u) == PM_OK);
    assert(retained_service_table != NULL);
    assert(retained_service_table->count == 1u);
    assert(retained_service_table->items[0].vtable == &fake_service);
    assert(pm_manager_create_instance(manager, 42u) == PM_EBUSY);
    assert(pm_manager_get_instance(manager, 42u, &instance) == PM_OK);
    assert(instance == &created);
    assert(pm_manager_destroy_instance(manager, 42u) == PM_OK);
    assert(pm_manager_destroy_instance(manager, 42u) == PM_ENOENT);
    assert(pm_manager_unload(manager, 42u) == PM_ENOTSUP);
    pm_manager_destroy(manager);
    assert(created == 1);
    assert(destroyed == 1);
}

static void test_package_load_rejects_non_elf(void)
{
    uint8_t package[132u];
    uint8_t text[64u];
    uint8_t rodata[64u];
    struct pm_elf_profile profile = {
        .max_sections = 16u, .max_image_size = 64u, .max_ram_size = 512u,
    };
    struct pm_manager_package_config package_config = {
        .elf_profile = &profile,
        .text_storage = text, .text_capacity = sizeof(text),
        .rodata_storage = rodata, .rodata_capacity = sizeof(rodata),
        .get_descriptor = loaded_descriptor,
    };
    static const struct pm_plugin_interface interfaces[] = {
        {.interface_id = {{2u}}, .version = 1u},
    };
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
        .interfaces = interfaces, .interface_count = 1u,
    };
    struct pm_manager_config manager_config = {
        .contract = &contract, .max_plugins = 1u,
    };
    struct pm_manager *manager;
    uint32_t plugin_id = 0u;
    size_t package_size = make_package(package, sizeof(package));

    manager = pm_manager_create(&manager_config);
    assert(manager != NULL);
    assert(pm_manager_load_package(manager, package, package_size,
                                   &package_config, &plugin_id) == PM_EPROTO);
    assert(pm_manager_unload(manager, 99u) == PM_ENOENT);
    pm_manager_destroy(manager);
}

static void test_missing_service(void)
{
    static const pm_uuid_t required[] = {{{3u}}};
    static const struct pm_plugin_interface interfaces[] = {
        {.interface_id = {{2u}}, .version = 1u},
    };
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
        .interfaces = interfaces, .interface_count = 1u,
    };
    static const struct pm_plugin_descriptor descriptor = {
        .framework_abi_version = PM_FRAMEWORK_ABI_VERSION,
        .descriptor_size = sizeof(struct pm_plugin_descriptor),
        .application_id = {{1u}}, .interface_id = {{2u}},
        .interface_version = 1u, .plugin_id = 7u,
        .required_service_ids = required, .required_service_count = 1u,
        .name = "missing-service", .version = "1.0",
        .create = plugin_create, .destroy = plugin_destroy,
    };
    struct pm_manager_config config = {.contract = &contract, .max_plugins = 1u};
    struct pm_manager *manager = pm_manager_create(&config);

    assert(manager != NULL);
    assert(pm_manager_register_static(manager, &descriptor) == PM_OK);
    assert(pm_manager_create_instance(manager, 7u) == PM_ENOENT);
    pm_manager_destroy(manager);
}

static void test_operation_state_blocks_reentry(void)
{
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
    };
    static const struct pm_storage_adapter storage = {
        .list = reentrant_list, .size = reentrant_size, .read = reentrant_read,
        .max_entries = 1u,
    };
    const struct pm_manager_scan_config scan = {
        .storage = &storage, .candidate_suffix = NULL,
        .max_name_length = 32u, .max_package_size = 256u,
    };
    const struct pm_manager_config config = {.contract = &contract, .max_plugins = 1u};

    callback_manager = pm_manager_create(&config);
    assert(callback_manager != NULL);
    assert(pm_manager_state_get(callback_manager) == PM_MANAGER_IDLE);
    assert(pm_manager_scan(callback_manager, &scan) == PM_OK);
    assert(callback_reentry_status == PM_EBUSY);
    assert(pm_manager_state_get(callback_manager) == PM_MANAGER_IDLE);
    pm_manager_destroy(callback_manager);
    callback_manager = NULL;
}

static void test_filesystem_lifetime(void)
{
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
    };
    struct pm_manager_config config = {
        .contract = &contract, .max_plugins = 1u,
    };
    struct pm_manager *manager = pm_manager_create(&config);

    assert(manager != NULL);
    assert(pm_manager_filesystem_attach(manager, &contract) == PM_OK);
    assert(pm_manager_filesystem_attach(manager, &contract) == PM_EBUSY);
    assert(pm_manager_filesystem_is_same(manager, &contract));
    assert(pm_manager_filesystem_detach(manager) == PM_OK);
    pm_manager_destroy(manager);
}

int main(void)
{
    test_package_parser();
    test_static_lifecycle();
    test_missing_service();
    test_package_load_rejects_non_elf();
    test_loaded_package_lifecycle();
    test_loaded_storage_cannot_be_reused();
    test_loaded_descriptor_is_owned();
    test_manager_owned_image_lifetime();
    test_allocator_failure_paths();
    test_allocated_image_released_after_descriptor_failure();
    test_allocator_requires_release_callback();
    test_available_load_rereads_storage();
    test_scan_skips_corrupt_and_read_error_candidates();
    test_scan_copies_storage_adapter();
    test_operation_state_blocks_reentry();
    test_filesystem_lifetime();
    puts("PluginManager tests passed");
    return 0;
}
