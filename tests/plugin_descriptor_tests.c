#include "plugin_manager/plugin_descriptor.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void put16(uint8_t *p, size_t o, uint16_t v)
{
    p[o] = (uint8_t)v;
    p[o + 1u] = (uint8_t)(v >> 8u);
}

static void put32(uint8_t *p, size_t o, uint32_t v)
{
    p[o] = (uint8_t)v;
    p[o + 1u] = (uint8_t)(v >> 8u);
    p[o + 2u] = (uint8_t)(v >> 16u);
    p[o + 3u] = (uint8_t)(v >> 24u);
}

static void test_decode(void)
{
    uint8_t rodata[160u] = {0};
    struct pm_elf_image image = {.rodata = rodata, .rodata_size = sizeof(rodata)};
    struct pm_wire_plugin_descriptor descriptor;

    put32(rodata, 0u, PM_WIRE_DESCRIPTOR_MAGIC);
    put16(rodata, 4u, PM_WIRE_DESCRIPTOR_VERSION);
    put16(rodata, 6u, PM_WIRE_DESCRIPTOR_HEADER_SIZE);
    rodata[8u] = 1u;
    rodata[24u] = 2u;
    put32(rodata, 40u, 3u);
    put32(rodata, 44u, 9u);
    put32(rodata, 48u, 1u);
    put32(rodata, 52u, 80u);
    put32(rodata, 56u, 96u);
    put32(rodata, 60u, 5u);
    put32(rodata, 64u, 104u);
    put32(rodata, 68u, 4u);
    put32(rodata, 72u, 0x1001u);
    put32(rodata, 76u, 0x1005u);
    rodata[80u] = 3u;
    rodata[96u] = 'n'; rodata[97u] = 'a'; rodata[98u] = 'm'; rodata[99u] = 'e';
    rodata[100u] = '\0';
    rodata[104u] = '1'; rodata[105u] = '.'; rodata[106u] = '0'; rodata[107u] = '\0';

    assert(pm_wire_descriptor_decode(&image, 0u, &descriptor) == PM_OK);
    assert(descriptor.plugin_id == 9u);
    assert(descriptor.interface_version == 3u);
    assert(descriptor.required_service_count == 1u);
    assert(descriptor.required_service_ids[0].bytes[0] == 3u);
    assert(strcmp(descriptor.name, "name") == 0);
    assert(strcmp(descriptor.version, "1.0") == 0);
    assert(descriptor.create_address == 0x1001u);
}

static void test_rejects_malformed(void)
{
    uint8_t rodata[80u] = {0};
    struct pm_elf_image image = {.rodata = rodata, .rodata_size = sizeof(rodata)};
    struct pm_wire_plugin_descriptor descriptor;

    put32(rodata, 0u, PM_WIRE_DESCRIPTOR_MAGIC);
    put16(rodata, 4u, PM_WIRE_DESCRIPTOR_VERSION);
    put16(rodata, 6u, PM_WIRE_DESCRIPTOR_HEADER_SIZE);
    assert(pm_wire_descriptor_decode(&image, 1u, &descriptor) == PM_EINVAL);
    assert(pm_wire_descriptor_decode(&image, 0u, &descriptor) == PM_EPROTO);
    put32(rodata, 0u, PM_WIRE_DESCRIPTOR_MAGIC);
    put32(rodata, 48u, PM_WIRE_DESCRIPTOR_MAX_REQUIRED_SERVICES + 1u);
    assert(pm_wire_descriptor_decode(&image, 0u, &descriptor) == PM_EOVERFLOW);
}

static int resolve_entry(void *context, uint32_t address,
                         enum pm_elf_entry_kind kind, void *out)
{
    (void)context;
    (void)kind;
    if (address == 1u) {
        *(pm_plugin_create_fn *)out = NULL;
    } else {
        *(pm_plugin_destroy_fn *)out = NULL;
    }
    return PM_OK;
}

static void test_bind_checks_thumb_text_range(void)
{
    struct pm_wire_plugin_descriptor wire = {
        .create_address = 0x1001u, .destroy_address = 0x1003u,
    };
    uint8_t text[8u];
    struct pm_elf_image image = {
        .text = text, .text_size = sizeof(text), .text_address = 0x1000u,
    };
    struct pm_descriptor_bind_config config = {.resolve = resolve_entry};
    struct pm_plugin_descriptor descriptor;

    assert(pm_wire_descriptor_bind(&wire, &image, &config, &descriptor) == PM_EPROTO);
    wire.create_address = 0x1000u;
    assert(pm_wire_descriptor_bind(&wire, &image, &config, &descriptor) == PM_EPROTO);
    wire.create_address = 0x2001u;
    assert(pm_wire_descriptor_bind(&wire, &image, &config, &descriptor) == PM_EPROTO);
}

static int convert_entry(void *context, uint32_t address,
                         enum pm_elf_entry_kind kind, void *out)
{
    (void)context;
    (void)kind;
    if (address == 0x1001u) {
        *(pm_plugin_create_fn *)out = NULL;
    } else {
        *(pm_plugin_destroy_fn *)out = NULL;
    }
    return PM_OK;
}

static void test_arm_adapter_region(void)
{
    uint8_t text[8u];
    struct pm_elf_image image = {
        .text = text, .text_size = sizeof(text), .text_address = 0x1000u,
    };
    struct pm_arm_entry_adapter adapter = {
        .executable_base = 0x1000u, .executable_size = sizeof(text),
        .convert = convert_entry,
    };
    pm_plugin_create_fn create = NULL;

    assert(pm_arm_entry_adapter_validate(&adapter, &image) == PM_OK);
    assert(pm_arm_entry_resolve(&adapter, 0x1001u, PM_ELF_ENTRY_CREATE,
                                &create) == PM_OK);
    assert(pm_arm_entry_resolve(&adapter, 0x1000u, PM_ELF_ENTRY_CREATE,
                                &create) == PM_EPROTO);
    assert(pm_arm_entry_resolve(&adapter, 0x2001u, PM_ELF_ENTRY_CREATE,
                                &create) == PM_EPROTO);
}

int main(void)
{
    test_decode();
    test_rejects_malformed();
    test_bind_checks_thumb_text_range();
    test_arm_adapter_region();
    puts("PluginManager descriptor tests passed");
    return 0;
}
