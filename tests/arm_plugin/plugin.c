#include <stdint.h>
#include <stddef.h>

struct example_plugin_instance {
    int (*get_info)(void *instance, char *buffer, size_t capacity);
};

static int pm_plugin_get_info(void *instance, char *buffer, size_t capacity)
{
    (void)instance;
    if (capacity < 29u) {
        return -75;
    }
    static const char text[] = "PluginManager ARM example v1";
    size_t index;

    for (index = 0u; index < 29u; ++index) {
        buffer[index] = text[index];
    }
    return 0;
}

static struct example_plugin_instance pm_plugin_instance = {
    .get_info = pm_plugin_get_info,
};

__attribute__((used))
int pm_plugin_destroy(void *instance);

__attribute__((used))
int pm_plugin_create(void *services, void **instance)
{
    (void)services;
    *instance = &pm_plugin_instance;
    return 0;
}

/* The descriptor is serialized by the SDK/application contract generator. */
struct pm_wire_fixture {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint8_t application_id[16];
    uint8_t interface_id[16];
    uint32_t interface_version;
    uint32_t plugin_id;
    uint32_t required_count;
    uint32_t required_offset;
    uint32_t name_offset;
    uint32_t name_size;
    uint32_t version_offset;
    uint32_t version_size;
    const void *create_address;
    const void *destroy_address;
    char name[14];
    char plugin_version[4];
};

__attribute__((section(".rodata.pm_descriptor"), used))
const struct pm_wire_fixture pm_plugin_get_descriptor = {
    .magic = 0x53444D50u,
    .version = 1u,
    .size = 80u,
    .application_id = {1u},
    .interface_id = {2u},
    .interface_version = 1u,
    .plugin_id = 42u,
    .required_count = 0u,
    .required_offset = 0u,
    .name_offset = 80u,
    .name_size = 14u,
    .version_offset = 94u,
    .version_size = 4u,
    .create_address = pm_plugin_create,
    .destroy_address = pm_plugin_destroy,
    .name = "arm-plugin",
    .plugin_version = "1.0"
};

__attribute__((used))
int pm_plugin_destroy(void *instance)
{
    (void)instance;
    return 0;
}
