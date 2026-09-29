#include <zephyr/ztest.h>
#include <zephyr/kernel.h>

#include <string.h>

#include "plugin_manager/plugin_manager.h"
#include "plugin_manager/plugin_manager_zephyr.h"
#include "plugin_manager/plugin_descriptor.h"
#include "../plugin_fixture.h"

static uint8_t package_data[132u];
static struct k_mutex manager_mutex;
static int lifecycle_created;
static int lifecycle_destroyed;

static int mutex_lock(void *context)
{
    return k_mutex_lock((struct k_mutex *)context, K_NO_WAIT) == 0 ?
           PM_OK : PM_EBUSY;
}

static void mutex_unlock(void *context)
{
    (void)k_mutex_unlock((struct k_mutex *)context);
}

static int lifecycle_create(const struct pm_service_table *services, void **instance)
{
    zassert_equal(services->count, 0u, NULL);
    *instance = &lifecycle_created;
    ++lifecycle_created;
    return PM_OK;
}

static int lifecycle_destroy(void *instance)
{
    zassert_equal(instance, &lifecycle_created, NULL);
    ++lifecycle_destroyed;
    return PM_OK;
}

static const struct pm_plugin_descriptor lifecycle_descriptor = {
    .framework_abi_version = PM_FRAMEWORK_ABI_VERSION,
    .descriptor_size = sizeof(struct pm_plugin_descriptor),
    .application_id = {{1u}}, .interface_id = {{2u}},
    .interface_version = 1u, .plugin_id = 44u,
    .name = "lifecycle", .version = "1.0",
    .create = lifecycle_create, .destroy = lifecycle_destroy,
};

static int loaded_create(const struct pm_service_table *services, void **instance)
{
    zassert_equal(services->count, 0u, NULL);
    *instance = &lifecycle_created;
    ++lifecycle_created;
    return PM_OK;
}

static int loaded_destroy(void *instance)
{
    zassert_equal(instance, &lifecycle_created, NULL);
    ++lifecycle_destroyed;
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

static int adapt_wire_descriptor(
    const struct pm_wire_plugin_descriptor *wire,
    const struct pm_elf_image *image,
    const struct pm_package_manifest *manifest,
    const struct pm_plugin_descriptor **descriptor,
    void *context)
{
    static struct pm_plugin_descriptor value;
    struct pm_descriptor_bind_config bind = {
        .resolve = resolve_loaded_entry,
    };

    (void)manifest;
    (void)context;
    if (pm_wire_descriptor_bind(wire, image, &bind, &value) != PM_OK) {
        return PM_EPROTO;
    }
    *descriptor = &value;
    return PM_OK;
}

static void put16(uint8_t *buffer, size_t offset, uint16_t value)
{
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1u] = (uint8_t)(value >> 8u);
}

static void put32(uint8_t *buffer, size_t offset, uint32_t value)
{
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1u] = (uint8_t)(value >> 8u);
    buffer[offset + 2u] = (uint8_t)(value >> 16u);
    buffer[offset + 3u] = (uint8_t)(value >> 24u);
}

static void make_package(void)
{
    static const uint8_t image[] = {1u, 2u, 3u, 4u};

    memset(package_data, 0, sizeof(package_data));
    put32(package_data, 0u, PM_PACKAGE_MAGIC);
    put16(package_data, 4u, PM_PACKAGE_FORMAT_VERSION);
    put16(package_data, 6u, PM_PACKAGE_HEADER_SIZE);
    put32(package_data, 8u, sizeof(package_data));
    package_data[12u] = 1u;
    put32(package_data, 44u, 1u);
    put32(package_data, 48u, 7u);
    put16(package_data, 52u, PM_PACKAGE_TARGET_ARM_CORTEX_M);
    put32(package_data, 56u, 128u);
    put32(package_data, 60u, 4u);
    put32(package_data, 76u, 108u);
    put32(package_data, 80u, 13u);
    put32(package_data, 84u, 121u);
    put32(package_data, 88u, 4u);
    put32(package_data, 128u, 0u);
    memcpy(package_data + 108u, "ztest-plugin", 13u);
    memcpy(package_data + 121u, "1.0", 4u);
    memcpy(package_data + 128u, image, sizeof(image));
    put32(package_data, 72u, pm_package_crc32(image, sizeof(image)));
}

static int storage_list(void *context, size_t index, char *name,
                        size_t capacity, int *present)
{
    (void)context;
    if (index == 0u) {
        strncpy(name, "valid.pmp", capacity);
        *present = 1;
    } else if (index == 1u) {
        strncpy(name, "ignored.txt", capacity);
        *present = 1;
    } else {
        *present = 0;
    }
    return PM_OK;
}

static int storage_size(void *context, const char *name, uint32_t *size)
{
    (void)context;
    (void)name;
    *size = sizeof(package_data);
    return PM_OK;
}

static int storage_read(void *context, const char *name, uint32_t offset,
                        void *destination, size_t size)
{
    (void)context;
    (void)name;
    if ((size_t)offset + size > sizeof(package_data)) {
        return PM_EOVERFLOW;
    }
    memcpy(destination, package_data + offset, size);
    return PM_OK;
}

ZTEST(plugin_manager, test_scan_filters_and_records_valid_package)
{
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
    };
    static const struct pm_storage_adapter storage = {
        .list = storage_list, .size = storage_size, .read = storage_read,
        .max_entries = 3u,
    };
    const struct pm_manager_config manager_config = {
        .contract = &contract, .max_plugins = 2u,
    };
    const struct pm_manager_scan_config scan_config = {
         .storage = &storage, .candidate_suffix = NULL,
        .max_name_length = 32u, .max_package_size = sizeof(package_data),
    };
    struct pm_available_plugin available;
    struct pm_package_manifest manifest;
    struct pm_manager *manager;

    make_package();
    zassert_ok(pm_package_parse(package_data, sizeof(package_data), &manifest), NULL);
    manager = pm_manager_create(&manager_config);
    zassert_not_null(manager, NULL);
    zassert_equal(pm_manager_scan(manager, &scan_config), PM_OK, NULL);
    zassert_equal(pm_manager_available_count(manager), 1u, NULL);
    zassert_ok(pm_manager_available_get(manager, 0u, &available), NULL);
    zassert_equal(available.plugin_id, 7u, NULL);
    zassert_equal(strcmp(available.name, "ztest-plugin"), 0, NULL);
     zassert_equal(strcmp(available.storage_name, "valid.pmp"), 0, NULL);
    pm_manager_destroy(manager);
}

ZTEST(plugin_manager, test_state_returns_idle_after_scan)
{
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
    };
    static const struct pm_storage_adapter storage = {
        .list = storage_list, .size = storage_size, .read = storage_read,
        .max_entries = 3u,
    };
    const struct pm_manager_config manager_config = {
        .contract = &contract, .max_plugins = 1u,
    };
    const struct pm_manager_scan_config scan_config = {
         .storage = &storage, .candidate_suffix = NULL,
        .max_name_length = 32u, .max_package_size = sizeof(package_data),
    };
    struct pm_manager *manager;

    make_package();
    manager = pm_manager_create(&manager_config);
    zassert_not_null(manager, NULL);
    zassert_equal(pm_manager_state_get(manager), PM_MANAGER_IDLE, NULL);
    zassert_ok(pm_manager_scan(manager, &scan_config), NULL);
    zassert_equal(pm_manager_state_get(manager), PM_MANAGER_IDLE, NULL);
    pm_manager_destroy(manager);
}

ZTEST(plugin_manager, test_scan_skips_duplicate_plugin_ids)
{
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
    };
    static const struct pm_storage_adapter storage = {
        .list = storage_list, .size = storage_size, .read = storage_read,
        .max_entries = 3u,
    };
    const struct pm_manager_config manager_config = {
        .contract = &contract, .max_plugins = 2u,
    };
    const struct pm_manager_scan_config scan_config = {
        .storage = &storage, .candidate_suffix = NULL,
        .max_name_length = 32u, .max_package_size = sizeof(package_data),
    };
    struct pm_manager *manager;

    make_package();
    manager = pm_manager_create(&manager_config);
    zassert_not_null(manager, NULL);
    zassert_ok(pm_manager_scan(manager, &scan_config), NULL);
    zassert_equal(pm_manager_available_count(manager), 1u, NULL);
    pm_manager_destroy(manager);
}

ZTEST(plugin_manager, test_scan_skips_corrupt_package)
{
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
    };
    static const struct pm_storage_adapter storage = {
        .list = storage_list, .size = storage_size, .read = storage_read,
        .max_entries = 3u,
    };
    const struct pm_manager_config config = {
        .contract = &contract, .max_plugins = 2u,
    };
    const struct pm_manager_scan_config scan = {
        .storage = &storage, .candidate_suffix = NULL,
        .max_name_length = 32u, .max_package_size = sizeof(package_data),
    };
    struct pm_manager *manager;

    make_package();
    package_data[0u] ^= 0xffu;
    manager = pm_manager_create(&config);
    zassert_not_null(manager, NULL);
    zassert_ok(pm_manager_scan(manager, &scan), NULL);
    zassert_equal(pm_manager_available_count(manager), 0u, NULL);
    pm_manager_destroy(manager);
}

ZTEST(plugin_manager, test_filesystem_attach_and_busy_detach)
{
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
    };
    static const struct fs_mount_t mount = {
        .mnt_point = "/test:",
    };
    const struct pm_zephyr_filesystem filesystem = {
        .mount = &mount,
        .plugin_directory = "plugins",
        .candidate_suffix = NULL,
        .max_entries = 2u,
        .max_name_length = 32u,
        .max_package_size = 256u,
    };
    const struct pm_manager_config config = {
        .contract = &contract, .max_plugins = 1u,
    };
    struct pm_manager *manager = pm_manager_create(&config);

    zassert_not_null(manager, NULL);
    zassert_ok(pm_manager_zephyr_attach_filesystem(manager, &filesystem), NULL);
    zassert_equal(pm_manager_zephyr_attach_filesystem(manager, &filesystem),
                  PM_EBUSY, NULL);
    zassert_ok(pm_manager_zephyr_detach_filesystem(manager), NULL);
    pm_manager_destroy(manager);
}

ZTEST(plugin_manager, test_zephyr_mutex_guards_lifecycle)
{
    static const struct pm_plugin_interface interface = {
        .interface_id = {{2u}}, .version = 1u,
    };
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
        .interfaces = &interface, .interface_count = 1u,
    };
    struct pm_manager_config config = {
        .contract = &contract, .max_plugins = 1u,
        .lock = mutex_lock, .unlock = mutex_unlock,
        .lock_context = &manager_mutex,
    };
    struct pm_manager *manager;
    void *instance;

    k_mutex_init(&manager_mutex);
    lifecycle_created = 0;
    lifecycle_destroyed = 0;
    manager = pm_manager_create(&config);
    zassert_not_null(manager, NULL);
    zassert_ok(pm_manager_register_static(manager, &lifecycle_descriptor), NULL);
    zassert_equal(pm_manager_state_get(manager), PM_MANAGER_IDLE, NULL);
    zassert_ok(pm_manager_create_instance(manager, 44u), NULL);
    zassert_equal(pm_manager_state_get(manager), PM_MANAGER_IDLE, NULL);
    zassert_equal(pm_manager_get_instance(manager, 44u, &instance), PM_OK, NULL);
    zassert_equal(instance, &lifecycle_created, NULL);
    zassert_ok(pm_manager_destroy_instance(manager, 44u), NULL);
    zassert_equal(lifecycle_destroyed, 1, NULL);
    zassert_equal(pm_manager_state_get(manager), PM_MANAGER_IDLE, NULL);
    zassert_equal(pm_manager_unload(manager, 44u), PM_ENOTSUP, NULL);
    zassert_equal(pm_manager_state_get(manager), PM_MANAGER_IDLE, NULL);
    pm_manager_destroy(manager);
}

ZTEST(plugin_manager, test_loaded_package_full_lifecycle)
{
    static const struct pm_plugin_interface interface = {
        .interface_id = {{2u}}, .version = 1u,
    };
    static const struct pm_application_contract contract = {
        .application_id = {{1u}}, .version = 1u,
        .interfaces = &interface, .interface_count = 1u,
    };
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
    struct pm_manager_config manager_config = {
        .contract = &contract, .max_plugins = 1u,
        .lock = mutex_lock, .unlock = mutex_unlock,
        .lock_context = &manager_mutex,
    };
    struct pm_manager *manager;
    uint32_t plugin_id = 0u;
    size_t package_size;

    k_mutex_init(&manager_mutex);
    lifecycle_created = 0;
    lifecycle_destroyed = 0;
    package_size = pm_test_make_loaded_package(package, sizeof(package));
    manager = pm_manager_create(&manager_config);
    zassert_not_null(manager, NULL);
    zassert_ok(pm_manager_load_package(manager, package, package_size,
                                       &package_config, &plugin_id), NULL);
    zassert_equal(plugin_id, 99u, NULL);
    zassert_equal(pm_manager_state_get(manager), PM_MANAGER_IDLE, NULL);
    zassert_ok(pm_manager_create_instance(manager, plugin_id), NULL);
    zassert_equal(pm_manager_unload(manager, plugin_id), PM_EBUSY, NULL);
    zassert_ok(pm_manager_destroy_instance(manager, plugin_id), NULL);
    zassert_equal(lifecycle_destroyed, 1, NULL);
    zassert_ok(pm_manager_unload(manager, plugin_id), NULL);
    zassert_equal(pm_manager_state_get(manager), PM_MANAGER_IDLE, NULL);
    pm_manager_destroy(manager);
}

ZTEST(plugin_manager, test_wire_descriptor_decode)
{
    uint8_t rodata[128u] = {0};
    struct pm_elf_image image = {.rodata = rodata, .rodata_size = sizeof(rodata)};
    struct pm_wire_plugin_descriptor descriptor;

    rodata[0u] = (uint8_t)PM_WIRE_DESCRIPTOR_MAGIC;
    rodata[1u] = (uint8_t)(PM_WIRE_DESCRIPTOR_MAGIC >> 8u);
    rodata[2u] = (uint8_t)(PM_WIRE_DESCRIPTOR_MAGIC >> 16u);
    rodata[3u] = (uint8_t)(PM_WIRE_DESCRIPTOR_MAGIC >> 24u);
    rodata[4u] = PM_WIRE_DESCRIPTOR_VERSION;
    rodata[6u] = PM_WIRE_DESCRIPTOR_HEADER_SIZE;
    rodata[56u] = 96u;
    rodata[60u] = 5u;
    rodata[64u] = 104u;
    rodata[68u] = 4u;
    rodata[72u] = 1u;
    rodata[76u] = 2u;
    memcpy(rodata + 96u, "name", 5u);
    memcpy(rodata + 104u, "1.0", 4u);
    zassert_ok(pm_wire_descriptor_decode(&image, 0u, &descriptor), NULL);
    zassert_equal(strcmp(descriptor.name, "name"), 0, NULL);
    zassert_equal(descriptor.create_address, 1u, NULL);
}

ZTEST_SUITE(plugin_manager, NULL, NULL, NULL, NULL, NULL);
