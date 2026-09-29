#include "plugin_manager/plugin_elf.h"
#include "plugin_manager/plugin_descriptor.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
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
    const size_t rodata_offset = 0x84u;
    const size_t symtab_offset = 0x88u;
    const size_t strtab_offset = 0xB8u;
    const size_t shstrtab_offset = 0xD1u;
    const size_t section_offset = 0x100u;
    const size_t total_size = section_offset + 6u * 40u;
    static const uint8_t text[] = {0x00u, 0xBEu, 0x00u, 0xBFu};
    static const uint8_t rodata[PM_WIRE_DESCRIPTOR_HEADER_SIZE] = {
        0xA5u, 0x5Au, 0x00u, 0x00u,
    };
    static const uint8_t strings[] = "\0pm_plugin_get_descriptor\0";
    static const uint8_t section_strings[] = "\0.text\0.rodata\0.symtab\0.strtab\0.shstrtab\0";
    size_t sh;

    assert(capacity >= total_size);
    memset(buffer, 0, total_size);
    buffer[0] = 0x7Fu;
    memcpy(buffer + 1u, "ELF", 3u);
    buffer[4] = PM_ELF_CLASS_32;
    buffer[5] = PM_ELF_DATA_LSB;
    buffer[6] = 1u;
    put16(buffer, 16u, PM_ELF_TYPE_REL);
    put16(buffer, 18u, PM_ELF_MACHINE_ARM);
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

    /* Symbol 0 is null; symbol 1 is the descriptor at .rodata+0. */
    put32(buffer, symtab_offset + 16u, 1u);
    put16(buffer, symtab_offset + 16u + 14u, 2u);

    sh = section_offset + 40u;
    put32(buffer, sh + 0u, 1u); /* .text */
    put32(buffer, sh + 4u, 1u);
    put32(buffer, sh + 8u, 6u);
    put32(buffer, sh + 16u, (uint32_t)text_offset);
    put32(buffer, sh + 20u, (uint32_t)sizeof(text));
    sh += 40u;
    put32(buffer, sh + 0u, 7u); /* .rodata */
    put32(buffer, sh + 4u, 1u);
    put32(buffer, sh + 8u, 2u);
    put32(buffer, sh + 16u, (uint32_t)rodata_offset);
    put32(buffer, sh + 20u, (uint32_t)sizeof(rodata));
    sh += 40u;
    put32(buffer, sh + 0u, 15u); /* .symtab */
    put32(buffer, sh + 4u, 2u);
    put32(buffer, sh + 16u, (uint32_t)symtab_offset);
    put32(buffer, sh + 20u, 32u);
    put32(buffer, sh + 24u, 4u);
    put32(buffer, sh + 36u, 16u);
    sh += 40u;
    put32(buffer, sh + 0u, 23u); /* .strtab */
    put32(buffer, sh + 4u, 3u);
    put32(buffer, sh + 16u, (uint32_t)strtab_offset);
    put32(buffer, sh + 20u, (uint32_t)sizeof(strings));
    sh += 40u;
    put32(buffer, sh + 0u, 31u); /* .shstrtab */
    put32(buffer, sh + 4u, 3u);
    put32(buffer, sh + 16u, (uint32_t)shstrtab_offset);
    put32(buffer, sh + 20u, (uint32_t)sizeof(section_strings));
    return total_size;
}

static void test_elf_profile(void)
{
    uint8_t elf[6u * 40u + 0x100u];
    uint8_t text[8u];
    uint8_t rodata[PM_WIRE_DESCRIPTOR_HEADER_SIZE];
    struct pm_elf_profile profile = {
        .max_sections = 16u, .max_image_size = 128u, .max_ram_size = 64u,
        .rodata_address = 0x1000u,
    };
    struct pm_elf_image image;
    size_t size = make_elf(elf, sizeof(elf));

    assert(pm_elf_validate(elf, size, &profile) == PM_OK);
    assert(pm_elf_load(elf, size, &profile, text, sizeof(text), rodata,
                       sizeof(rodata), NULL, 0u, NULL, 0u, NULL, NULL,
                       &image) == PM_OK);
    assert(image.text_size == 4u);
    assert(image.rodata_size == PM_WIRE_DESCRIPTOR_HEADER_SIZE);
    assert(image.descriptor_offset == 0u);
    assert(image.data == NULL && image.data_size == 0u);
    assert(image.bss_size == 0u);
    assert(text[0] == 0x00u && rodata[0] == 0xA5u);

    profile.max_image_size = 7u;
    assert(pm_elf_load(elf, size, &profile, text, sizeof(text), rodata,
                       sizeof(rodata), NULL, 0u, NULL, 0u, NULL, NULL,
                       &image) == PM_EOVERFLOW);
    profile.max_image_size = 128u;
    assert(pm_elf_load(elf, size, &profile, text, 2u, rodata, sizeof(rodata),
                       NULL, 0u, NULL, 0u, NULL, NULL, &image) == PM_EOVERFLOW);
    assert(pm_elf_load(elf, size, &profile, NULL, sizeof(text), rodata,
                       sizeof(rodata), NULL, 0u, NULL, 0u, NULL, NULL,
                       &image) == PM_EOVERFLOW);

    elf[0xB9u] = 'x';
    assert(pm_elf_validate(elf, size, &profile) == PM_ENOENT);

    size = make_elf(elf, sizeof(elf));
    elf[0xB8u + sizeof("\0pm_plugin_get_descriptor\0") - 2u] = 'x';
    elf[0xB8u + sizeof("\0pm_plugin_get_descriptor\0") - 1u] = 'x';
    assert(pm_elf_validate(elf, size, &profile) == PM_EPROTO);

    elf[4u] = 2u;
    assert(pm_elf_validate(elf, size, &profile) == PM_EPROTO);
    size = make_elf(elf, sizeof(elf));
    put16(elf, 16u, 2u);
    assert(pm_elf_validate(elf, size, &profile) == PM_EPROTO);
    size = make_elf(elf, sizeof(elf));
    put32(elf, 32u, 0xFFFFu);
    assert(pm_elf_validate(elf, size, &profile) == PM_EOVERFLOW);
}

int main(void)
{
    test_elf_profile();
    puts("PluginManager ELF tests passed");
    return 0;
}
