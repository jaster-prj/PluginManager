#include "plugin_manager/plugin_elf.h"
#include "plugin_manager/plugin_descriptor.h"

#include <string.h>

#define ELF32_HDR_SIZE 52u
#define ELF32_SHDR_SIZE 40u
#define ELF32_SYM_SIZE 16u
#define ELF32_REL_SIZE 8u
#define SHT_PROGBITS 1u
#define SHT_SYMTAB 2u
#define SHT_REL 9u
#define SHT_NOBITS 8u
#define SHF_WRITE 0x1u
#define SHF_ALLOC 0x2u
#define SHF_EXECINSTR 0x4u
#define SHN_UNDEF 0u

static uint16_t u16(const uint8_t *p, size_t o)
{
    return (uint16_t)p[o] | (uint16_t)((uint16_t)p[o + 1u] << 8u);
}

static uint32_t u32(const uint8_t *p, size_t o)
{
    return (uint32_t)p[o] | ((uint32_t)p[o + 1u] << 8u) |
           ((uint32_t)p[o + 2u] << 16u) | ((uint32_t)p[o + 3u] << 24u);
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8u);
    p[2] = (uint8_t)(value >> 16u);
    p[3] = (uint8_t)(value >> 24u);
}

static int range(size_t offset, size_t length, size_t total)
{
    return offset <= total && length <= total - offset;
}

static int bounded_string(const uint8_t *buffer, size_t offset, size_t length)
{
    return range(offset, length, SIZE_MAX) &&
           memchr(buffer + offset, '\0', length) != NULL;
}

int pm_elf_resolve_entry(const struct pm_elf_image *image, uint32_t address,
                         enum pm_elf_entry_kind kind,
                         pm_elf_entry_resolve_fn resolve, void *context,
                         void *function_out)
{
    uint32_t aligned;

    if (image == NULL || resolve == NULL || function_out == NULL) {
        return PM_EINVAL;
    }
    aligned = address & ~1u;
    if ((address & 1u) == 0u || aligned < image->text_address ||
        (uint64_t)(aligned - image->text_address) >= image->text_size) {
        return PM_EPROTO;
    }
    if (kind != PM_ELF_ENTRY_CREATE && kind != PM_ELF_ENTRY_DESTROY) {
        return PM_EINVAL;
    }
    return resolve(context, address, kind, function_out);
}

static int section(const uint8_t *buffer, size_t size, uint16_t index,
                   uint32_t section_offset, uint32_t *name, uint32_t *type,
                   uint32_t *flags, uint32_t *offset, uint32_t *length,
                   uint32_t *link, uint32_t *info, uint32_t *entsize)
{
    size_t at = (size_t)section_offset + (size_t)index * ELF32_SHDR_SIZE;

    if (!range(at, ELF32_SHDR_SIZE, size)) {
        return 0;
    }
    *name = u32(buffer, at);
    *type = u32(buffer, at + 4u);
    *flags = u32(buffer, at + 8u);
    *offset = u32(buffer, at + 16u);
    *length = u32(buffer, at + 20u);
    *link = u32(buffer, at + 24u);
    *info = u32(buffer, at + 28u);
    *entsize = u32(buffer, at + 36u);
    return 1;
}

static int profile_valid(const struct pm_elf_profile *profile)
{
    return profile != NULL && profile->max_sections > 0u &&
           profile->max_sections <= PM_ELF_MAX_SECTIONS &&
           profile->max_image_size > 0u && profile->max_ram_size > 0u;
}

static int elf_header_valid(const uint8_t *buffer, size_t size,
                            const struct pm_elf_profile *profile,
                            uint16_t *section_count, uint32_t *section_offset,
                            uint16_t *string_index)
{
    uint32_t section_bytes;

    if (buffer == NULL || !profile_valid(profile) || size < ELF32_HDR_SIZE ||
        buffer[0] != 0x7Fu || buffer[1] != 'E' || buffer[2] != 'L' ||
        buffer[3] != 'F' || buffer[4] != PM_ELF_CLASS_32 ||
        buffer[5] != PM_ELF_DATA_LSB || buffer[6] != 1u ||
        u16(buffer, 16u) != PM_ELF_TYPE_REL ||
         u16(buffer, 18u) != (profile->machine == 0u ? PM_ELF_MACHINE_ARM :
                              profile->machine) || u32(buffer, 20u) != 1u ||
        u16(buffer, 40u) != ELF32_HDR_SIZE || u16(buffer, 46u) != ELF32_SHDR_SIZE) {
        return PM_EPROTO;
    }
    *section_count = u16(buffer, 48u);
    *section_offset = u32(buffer, 32u);
    *string_index = u16(buffer, 50u);
    if (*section_count == 0u || *section_count > profile->max_sections ||
        *string_index >= *section_count) {
        return PM_EPROTO;
    }
    section_bytes = (uint32_t)*section_count * ELF32_SHDR_SIZE;
    return range(*section_offset, section_bytes, size) ? PM_OK : PM_EOVERFLOW;
}

static int section_table_valid(const uint8_t *buffer, size_t size,
                               uint16_t count, uint32_t section_offset,
                               uint16_t string_index, uint16_t *symtab_index)
{
    uint32_t name, type, flags, offset, length, link, info, entsize;
    uint16_t i;

    *symtab_index = UINT16_MAX;
    for (i = 0u; i < count; ++i) {
        if (!section(buffer, size, i, section_offset, &name, &type, &flags,
                     &offset, &length, &link, &info, &entsize)) {
            return PM_EOVERFLOW;
        }
        if (type == SHT_SYMTAB) {
            if (*symtab_index != UINT16_MAX || entsize != ELF32_SYM_SIZE ||
                link >= count || !range(offset, length, size)) {
                return PM_EPROTO;
            }
            *symtab_index = i;
        } else if (type == SHT_REL) {
            if (entsize != ELF32_REL_SIZE || link >= count || info >= count ||
                !range(offset, length, size) || length % ELF32_REL_SIZE != 0u) {
                return PM_EPROTO;
            }
        } else if (type == SHT_PROGBITS && !range(offset, length, size) &&
                   (flags & SHF_ALLOC) != 0u) {
            return PM_EOVERFLOW;
        } else if (type == SHT_NOBITS && (flags & SHF_ALLOC) == 0u) {
            return PM_EPROTO;
        }
        (void)name;
        (void)string_index;
    }
    return *symtab_index == UINT16_MAX ? PM_EPROTO : PM_OK;
}

static int symbol_tables(const uint8_t *buffer, size_t size, uint32_t shoff,
                         uint16_t symtab, uint32_t *symoff, uint32_t *symlen,
                         uint32_t *stroff, uint32_t *strlen_value)
{
    uint32_t name, type, flags, offset, length, link, info, entsize;

    if (!section(buffer, size, symtab, shoff, &name, &type, &flags, &offset,
                 &length, &link, &info, &entsize) ||
        !section(buffer, size, (uint16_t)link, shoff, &name, &type, &flags,
                 stroff, strlen_value, &link, &info, &entsize) ||
        !range(offset, length, size) || !range(*stroff, *strlen_value, size) ||
        length % ELF32_SYM_SIZE != 0u || type != 3u) {
        return PM_EPROTO;
    }
    *symoff = offset;
    *symlen = length;
    return PM_OK;
}

static int find_descriptor(const uint8_t *buffer, size_t size, uint32_t shoff,
                           uint16_t symtab, uint16_t *descriptor_section,
                           uint32_t *descriptor_value)
{
    uint32_t symoff, symlen, stroff, strlen_value;
    uint32_t i;

    if (symbol_tables(buffer, size, shoff, symtab, &symoff, &symlen, &stroff,
                      &strlen_value) != PM_OK) {
        return PM_EPROTO;
    }
    for (i = 0u; i < symlen / ELF32_SYM_SIZE; ++i) {
        size_t at = (size_t)symoff + (size_t)i * ELF32_SYM_SIZE;
        uint32_t name = u32(buffer, at);
        size_t name_at;
        if (name >= strlen_value) {
            return PM_EPROTO;
        }
        name_at = (size_t)stroff + name;
        if (!range(name_at, strlen_value - name, size)) {
            return PM_EPROTO;
        }
        if (!bounded_string(buffer, name_at, strlen_value - name)) {
            return PM_EPROTO;
        }
        if (strcmp((const char *)(buffer + name_at), PM_ELF_DESCRIPTOR_SYMBOL) == 0) {
            *descriptor_section = u16(buffer, at + 14u);
            *descriptor_value = u32(buffer, at + 4u);
            return PM_OK;
        }
    }
    return PM_ENOENT;
}

static int address_for_section(uint16_t index, uint16_t text, uint16_t rodata,
                               uint16_t data, uint16_t bss,
                               const struct pm_elf_profile *profile,
                               size_t data_size,
                               uint32_t *address)
{
    if (index == text) {
        *address = profile->text_address;
    } else if (index == rodata) {
        *address = profile->rodata_address;
    } else if (index == data) {
        *address = profile->data_address;
    } else if (index == bss) {
        if (data_size > UINT32_MAX - 3u ||
            profile->data_address > UINT32_MAX - (((uint32_t)data_size + 3u) & ~3u)) {
            return PM_EOVERFLOW;
        }
        *address = profile->data_address + (((uint32_t)data_size + 3u) & ~3u);
    } else {
        return PM_ENOENT;
    }
    return PM_OK;
}

static int apply_relocations(const uint8_t *buffer, size_t size,
                             uint16_t count, uint32_t shoff, uint16_t symtab,
                             uint16_t text, uint16_t rodata, uint16_t data,
                              uint16_t bss, const struct pm_elf_profile *profile,
                              uint32_t descriptor_value,
                             uint8_t *text_storage, uint8_t *rodata_storage,
                             size_t text_size, size_t rodata_size,
                             uint8_t *data_storage, size_t data_size,
                             uint8_t *bss_storage, size_t bss_size,
                             pm_elf_resolve_fn resolve, void *resolve_context)
{
    uint32_t name, type, flags, offset, length, link, info, entsize;
    uint16_t i;
    uint32_t symoff, symlen, stroff, strlen_value;

    if (symbol_tables(buffer, size, shoff, symtab, &symoff, &symlen, &stroff,
                      &strlen_value) != PM_OK) {
        return PM_EPROTO;
    }
    for (i = 0u; i < count; ++i) {
        uint32_t target_address;
        uint32_t j;
        if (!section(buffer, size, i, shoff, &name, &type, &flags, &offset,
                     &length, &link, &info, &entsize) || type != SHT_REL) {
            continue;
        }
        if (link != symtab || address_for_section((uint16_t)info, text, rodata,
                                                   data, bss, profile, data_size,
                                                   &target_address) != PM_OK) {
            return PM_EPROTO;
        }
        for (j = 0u; j < length / ELF32_REL_SIZE; ++j) {
            size_t rel_at = (size_t)offset + (size_t)j * ELF32_REL_SIZE;
            uint32_t rel_address = u32(buffer, rel_at);
            uint32_t rel_info = u32(buffer, rel_at + 4u);
            uint32_t symbol_index = rel_info >> 8u;
            uint32_t relocation_type = rel_info & 0xFFu;
            uint32_t symbol_value;
            uint8_t symbol_type;
            uint16_t symbol_section;
            uint32_t symbol_base;
            uint32_t resolved;
            const char *symbol_name;
            uint8_t *target;
            size_t target_size;
            if (relocation_type != (profile->relocation_type == 0u ?
                                    PM_ELF_R_ARM_ABS32 : profile->relocation_type) ||
                (size_t)symbol_index * ELF32_SYM_SIZE >= symlen) {
                return PM_ENOTSUP;
            }
            symbol_value = u32(buffer, symoff + symbol_index * ELF32_SYM_SIZE + 4u);
            symbol_type = buffer[symoff + symbol_index * ELF32_SYM_SIZE + 12u] & 0x0Fu;
            symbol_section = u16(buffer, symoff + symbol_index * ELF32_SYM_SIZE + 14u);
            name = u32(buffer, symoff + symbol_index * ELF32_SYM_SIZE);
            if (name >= strlen_value ||
                !range((size_t)stroff + name, strlen_value - name, size)) {
                return PM_EPROTO;
            }
            symbol_name = (const char *)(buffer + stroff + name);
            if (!bounded_string(buffer, (size_t)stroff + name,
                                strlen_value - name)) {
                return PM_EPROTO;
            }
            if (symbol_section == SHN_UNDEF) {
                if (resolve == NULL) {
                    return PM_ENOTSUP;
                }
                if (resolve(resolve_context, symbol_name, &resolved) != PM_OK) {
                    return PM_ENOENT;
                }
            } else {
                if (symbol_section == 0xFFF1u) {
                    resolved = symbol_value;
                } else {
                    if (symbol_section == text) {
                        /* Descriptor function fields retain ELF-relative
                         * Thumb addresses; the target adapter relocates them
                         * after decoding. Other references need RAM pointers.
                         */
                        if (info == rodata &&
                            (rel_address == descriptor_value + 72u ||
                             rel_address == descriptor_value + 76u)) {
                            resolved = symbol_value;
                        } else {
                            resolved = (uint32_t)(uintptr_t)text_storage +
                                       (symbol_value < profile->text_address ?
                                        symbol_value :
                                        symbol_value - profile->text_address);
                        }
                    } else if (symbol_section == rodata) {
                        resolved = (uint32_t)(uintptr_t)rodata_storage +
                                   (symbol_value < profile->rodata_address ?
                                    symbol_value :
                                    symbol_value - profile->rodata_address);
                    } else if (symbol_section == data) {
                        resolved = (uint32_t)(uintptr_t)data_storage +
                                   (symbol_value < profile->data_address ?
                                    symbol_value :
                                    symbol_value - profile->data_address);
                    } else if (symbol_section == bss) {
                        resolved = (uint32_t)(uintptr_t)bss_storage +
                                   (symbol_value < profile->data_address ?
                                    symbol_value :
                                    symbol_value - profile->data_address);
                    } else if (address_for_section(symbol_section, text, rodata,
                                                   data, bss, profile, data_size,
                                                   &symbol_base) != PM_OK) {
                        return PM_EPROTO;
                    } else {
                        resolved = symbol_base + symbol_value;
                    }
                }
            }
            if (symbol_section == text && symbol_type == 2u) {
                resolved |= 1u;
            }
            if (info == text) {
                target = text_storage; target_size = text_size;
            } else if (info == rodata) {
                target = rodata_storage; target_size = rodata_size;
            } else if (info == data) {
                target = data_storage; target_size = data_size;
            } else {
                target = bss_storage; target_size = bss_size;
            }
            if (!range(rel_address, 4u, target_size)) {
                return PM_EOVERFLOW;
            }
            /* R_ARM_ABS32 always applies the existing word as addend. */
            put32(target + rel_address, resolved + u32(target, rel_address));
        }
    }
    return PM_OK;
}

int pm_elf_validate(const uint8_t *buffer, size_t buffer_size,
                    const struct pm_elf_profile *profile)
{
    uint16_t count, string_index, symtab, text = UINT16_MAX;
    uint16_t rodata = UINT16_MAX;
    uint32_t shoff, descriptor_value, name, type, flags, offset, length;
    uint32_t link, info, entsize;
    uint16_t descriptor_section;
    int status;

    status = elf_header_valid(buffer, buffer_size, profile, &count, &shoff,
                              &string_index);
    if (status != PM_OK) {
        return status;
    }
    status = section_table_valid(buffer, buffer_size, count, shoff, string_index,
                                 &symtab);
    if (status != PM_OK) {
        return status;
    }
    status = find_descriptor(buffer, buffer_size, shoff, symtab,
                             &descriptor_section, &descriptor_value);
    if (status != PM_OK || descriptor_section == SHN_UNDEF) {
        return status == PM_OK ? PM_EPROTO : status;
    }
    for (uint16_t index = 0u; index < count; ++index) {
        if (!section(buffer, buffer_size, index, shoff, &name, &type, &flags,
                     &offset, &length, &link, &info, &entsize)) {
            return PM_EOVERFLOW;
        }
        if (type == SHT_PROGBITS && (flags & SHF_ALLOC) != 0u &&
            (flags & SHF_WRITE) == 0u) {
            if ((flags & SHF_EXECINSTR) != 0u) {
                if (text != UINT16_MAX) {
                    return PM_EPROTO;
                }
                text = index;
            } else {
                if (rodata != UINT16_MAX) {
                    return PM_EPROTO;
                }
                rodata = index;
            }
        }
    }
    if (text == UINT16_MAX || rodata == UINT16_MAX || descriptor_section != rodata ||
        !section(buffer, buffer_size, rodata, shoff, &name, &type, &flags,
                 &offset, &length, &link, &info, &entsize) ||
        descriptor_value > length ||
        length - descriptor_value < PM_WIRE_DESCRIPTOR_HEADER_SIZE) {
        return PM_EPROTO;
    }
    return PM_OK;
}

int pm_elf_load(const uint8_t *buffer, size_t buffer_size,
                const struct pm_elf_profile *profile,
                uint8_t *text_storage, size_t text_capacity,
                uint8_t *rodata_storage, size_t rodata_capacity,
                uint8_t *data_storage, size_t data_capacity,
                uint8_t *bss_storage, size_t bss_capacity,
                pm_elf_resolve_fn resolve, void *resolve_context,
                struct pm_elf_image *result)
{
    uint16_t count, string_index, symtab, text = UINT16_MAX;
    uint16_t rodata = UINT16_MAX, data = UINT16_MAX, bss = UINT16_MAX;
    uint16_t descriptor_section;
    uint32_t shoff, descriptor_value, total_image = 0u, total_ram = 0u;
    uint32_t name, type, flags, offset, length, link, info, entsize;
    uint16_t i;
    int status;

    if (result == NULL || buffer == NULL) {
        return PM_EINVAL;
    }
    memset(result, 0, sizeof(*result));
    status = pm_elf_validate(buffer, buffer_size, profile);
    if (status != PM_OK) {
        return status;
    }
    (void)elf_header_valid(buffer, buffer_size, profile, &count, &shoff,
                           &string_index);
    (void)section_table_valid(buffer, buffer_size, count, shoff, string_index,
                              &symtab);
    (void)find_descriptor(buffer, buffer_size, shoff, symtab, &descriptor_section,
                          &descriptor_value);
    for (i = 0u; i < count; ++i) {
        if (!section(buffer, buffer_size, i, shoff, &name, &type, &flags, &offset,
                     &length, &link, &info, &entsize)) {
            return PM_EOVERFLOW;
        }
        if (type == SHT_PROGBITS && (flags & SHF_ALLOC) != 0u &&
            (flags & SHF_WRITE) == 0u) {
            if ((flags & SHF_EXECINSTR) != 0u) {
                if (text != UINT16_MAX) return PM_EPROTO;
                text = i;
            } else {
                if (rodata != UINT16_MAX) return PM_EPROTO;
                rodata = i;
            }
        } else if (type == SHT_PROGBITS &&
                   (flags & (SHF_ALLOC | SHF_WRITE)) == (SHF_ALLOC | SHF_WRITE)) {
            if (data != UINT16_MAX) {
                return PM_EPROTO;
            }
            data = i;
        } else if (type == SHT_NOBITS &&
                   (flags & (SHF_ALLOC | SHF_WRITE)) == (SHF_ALLOC | SHF_WRITE)) {
            if (bss != UINT16_MAX) {
                return PM_EPROTO;
            }
            bss = i;
        }
    }
    if (text == UINT16_MAX || rodata == UINT16_MAX ||
        !section(buffer, buffer_size, text, shoff, &name, &type, &flags, &offset,
                  &length, &link, &info, &entsize) || length > text_capacity ||
        (length > 0u && text_storage == NULL) ||
        !range(offset, length, buffer_size)) return PM_EOVERFLOW;
    memcpy(text_storage, buffer + offset, length);
    result->text = text_storage; result->text_size = length; total_image += length;
    if (!section(buffer, buffer_size, rodata, shoff, &name, &type, &flags, &offset,
                  &length, &link, &info, &entsize) || length > rodata_capacity ||
        (length > 0u && rodata_storage == NULL) ||
        !range(offset, length, buffer_size)) return PM_EOVERFLOW;
    if (descriptor_section != rodata || descriptor_value > length ||
        length - descriptor_value < PM_WIRE_DESCRIPTOR_HEADER_SIZE) {
        return PM_EPROTO;
    }
    memcpy(rodata_storage, buffer + offset, length);
    result->rodata = rodata_storage; result->rodata_size = length; total_image += length;
    if (data != UINT16_MAX) {
        if (!section(buffer, buffer_size, data, shoff, &name, &type, &flags, &offset,
                      &length, &link, &info, &entsize) || length > data_capacity ||
            (length > 0u && data_storage == NULL) ||
            !range(offset, length, buffer_size)) return PM_EOVERFLOW;
        memcpy(data_storage, buffer + offset, length); result->data = data_storage;
        result->data_size = length; total_ram += length;
    }
    if (bss != UINT16_MAX) {
        if (!section(buffer, buffer_size, bss, shoff, &name, &type, &flags, &offset,
                      &length, &link, &info, &entsize) || length > bss_capacity ||
            (length > 0u && bss_storage == NULL)) return PM_EOVERFLOW;
        memset(bss_storage, 0, length);
        result->bss_size = length; total_ram += length;
    }
    if (total_image > profile->max_image_size || total_ram > profile->max_ram_size) {
        return PM_EOVERFLOW;
    }
    result->descriptor_offset = descriptor_value;
    result->descriptor_address = profile->rodata_address + descriptor_value;
    result->text_address = profile->text_address;
    result->rodata_address = profile->rodata_address;
    status = apply_relocations(buffer, buffer_size, count, shoff, symtab, text,
                               rodata, data, bss, profile, descriptor_value,
                               text_storage,
                               rodata_storage, result->text_size,
                               result->rodata_size,
                               data_storage, result->data_size,
                                bss_storage, result->bss_size,
                               resolve, resolve_context);
    return status;
}
