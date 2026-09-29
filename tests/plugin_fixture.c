#include "plugin_fixture.h"

#include "plugin_manager/plugin_package.h"

#include <assert.h>
#include <string.h>

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

static size_t make_elf(uint8_t *buffer, size_t capacity)
{
    const size_t text_offset = 0x80u;
    const size_t rodata_offset = 0x400u;
    const size_t symtab_offset = 0x100u;
    const size_t strtab_offset = 0x130u;
    const size_t shstrtab_offset = 0x149u;
    const size_t section_offset = 0x500u;
    const size_t total_size = section_offset + 6u * 40u;
    static const uint8_t text[] = {0x00u, 0xBEu, 0x00u, 0xBFu};
    static uint8_t rodata[114u];
    static const uint8_t strings[] = "\0pm_plugin_get_descriptor\0";
    static const uint8_t section_strings[] =
        "\0.text\0.rodata\0.symtab\0.strtab\0.shstrtab\0";
    size_t sh;

    assert(capacity >= total_size);
    memset(buffer, 0, total_size);
    memset(rodata, 0, sizeof(rodata));
    put32(rodata, 0u, 0x53444D50u);
    put16(rodata, 4u, 1u);
    put16(rodata, 6u, 80u);
    rodata[8u] = 1u;
    rodata[24u] = 2u;
    put32(rodata, 40u, 1u);
    put32(rodata, 44u, 99u);
    put32(rodata, 48u, 0u);
    put32(rodata, 52u, 80u);
    put32(rodata, 56u, 96u);
    put32(rodata, 60u, 14u);
    put32(rodata, 64u, 110u);
    put32(rodata, 68u, 4u);
    put32(rodata, 72u, 1u);
    put32(rodata, 76u, 3u);
    memcpy(rodata + 96u, "loaded-plugin", 14u);
    memcpy(rodata + 110u, "1.0", 4u);
    buffer[0] = 0x7Fu;
    memcpy(buffer + 1u, "ELF", 3u);
    buffer[4] = 1u;
    buffer[5] = 1u;
    buffer[6] = 1u;
    put16(buffer, 16u, 1u);
    put16(buffer, 18u, 40u);
    put32(buffer, 20u, 1u);
    put32(buffer, 32u, (uint32_t)section_offset);
    put16(buffer, 40u, 52u);
    put16(buffer, 46u, 40u);
    put16(buffer, 48u, 6u);
    put16(buffer, 50u, 5u);
    memcpy(buffer + text_offset, text, sizeof(text));
    memcpy(buffer + rodata_offset, rodata, sizeof(rodata));
    memcpy(buffer + strtab_offset, strings, sizeof(strings));
    memcpy(buffer + shstrtab_offset, section_strings, sizeof(section_strings));

    put32(buffer, symtab_offset + 16u, 1u);
    put16(buffer, symtab_offset + 16u + 14u, 2u);

    sh = section_offset + 40u;
    put32(buffer, sh + 0u, 1u);
    put32(buffer, sh + 4u, 1u);
    put32(buffer, sh + 8u, 6u);
    put32(buffer, sh + 16u, (uint32_t)text_offset);
    put32(buffer, sh + 20u, (uint32_t)sizeof(text));
    sh += 40u;
    put32(buffer, sh + 0u, 7u);
    put32(buffer, sh + 4u, 1u);
    put32(buffer, sh + 8u, 2u);
    put32(buffer, sh + 16u, (uint32_t)rodata_offset);
    put32(buffer, sh + 20u, (uint32_t)sizeof(rodata));
    sh += 40u;
    put32(buffer, sh + 0u, 15u);
    put32(buffer, sh + 4u, 2u);
    put32(buffer, sh + 16u, (uint32_t)symtab_offset);
    put32(buffer, sh + 20u, 32u);
    put32(buffer, sh + 24u, 4u);
    put32(buffer, sh + 36u, 16u);
    sh += 40u;
    put32(buffer, sh + 0u, 23u);
    put32(buffer, sh + 4u, 3u);
    put32(buffer, sh + 16u, (uint32_t)strtab_offset);
    put32(buffer, sh + 20u, (uint32_t)sizeof(strings));
    sh += 40u;
    put32(buffer, sh + 0u, 31u);
    put32(buffer, sh + 4u, 3u);
    put32(buffer, sh + 16u, (uint32_t)shstrtab_offset);
    put32(buffer, sh + 20u, (uint32_t)sizeof(section_strings));
    return total_size;
}

size_t pm_test_make_loaded_package(uint8_t *buffer, size_t capacity)
{
    const size_t image_offset = 1024u;
    const size_t image_capacity = 1600u;
    const size_t name_offset = PM_PACKAGE_HEADER_SIZE;
    const size_t version_offset = name_offset + 14u;
    uint8_t *image;
    size_t image_size;
    size_t total_size;

    assert(capacity >= image_offset + image_capacity);
    memset(buffer, 0, capacity);
    image = buffer + image_offset;
    image_size = make_elf(image, image_capacity);
    total_size = image_offset + image_size;
    put32(buffer, 0u, PM_PACKAGE_MAGIC);
    put16(buffer, 4u, PM_PACKAGE_FORMAT_VERSION);
    put16(buffer, 6u, PM_PACKAGE_HEADER_SIZE);
    put32(buffer, 8u, (uint32_t)total_size);
    buffer[12u] = 1u;
    buffer[28u] = 2u;
    put32(buffer, 44u, 1u);
    put32(buffer, 48u, 99u);
    put16(buffer, 52u, PM_PACKAGE_TARGET_ARM_CORTEX_M);
    put32(buffer, 56u, (uint32_t)image_offset);
    put32(buffer, 60u, (uint32_t)image_size);
    put32(buffer, 64u, 0u);
    put32(buffer, 68u, 0u);
    put32(buffer, 72u, pm_package_crc32(image, image_size));
    put32(buffer, 76u, (uint32_t)name_offset);
    put32(buffer, 80u, 14u);
    put32(buffer, 84u, (uint32_t)version_offset);
    put32(buffer, 88u, 4u);
    memcpy(buffer + name_offset, "loaded-plugin", 14u);
    memcpy(buffer + version_offset, "1.0", 4u);
    return total_size;
}
