#include "plugin_manager/plugin_descriptor.h"

#include <string.h>

static uint16_t read_u16(const uint8_t *buffer, size_t offset)
{
    return (uint16_t)buffer[offset] |
           (uint16_t)((uint16_t)buffer[offset + 1u] << 8u);
}

static uint32_t read_u32(const uint8_t *buffer, size_t offset)
{
    return (uint32_t)buffer[offset] |
           ((uint32_t)buffer[offset + 1u] << 8u) |
           ((uint32_t)buffer[offset + 2u] << 16u) |
           ((uint32_t)buffer[offset + 3u] << 24u);
}

static int range(size_t offset, size_t length, size_t total)
{
    return offset <= total && length <= total - offset;
}

static int string_range(const uint8_t *buffer, size_t offset, size_t length,
                        size_t total, size_t maximum)
{
    return length > 0u && length <= maximum && range(offset, length, total) &&
           buffer[offset + length - 1u] == '\0';
}

int pm_wire_descriptor_decode(const struct pm_elf_image *image,
                              size_t descriptor_offset,
                              struct pm_wire_plugin_descriptor *result)
{
    const uint8_t *buffer;
    size_t required_bytes;
    uint16_t descriptor_version;
    uint16_t descriptor_size;
    uint32_t required_offset;
    uint32_t required_count;
    uint32_t name_offset;
    uint32_t name_size;
    uint32_t version_offset;
    uint32_t version_size;
    uint32_t extension_offset = 0u;
    uint32_t extension_size = 0u;

    if (image == NULL || result == NULL || image->rodata == NULL ||
        !range(descriptor_offset, 80u,
               image->rodata_size)) {
        return PM_EINVAL;
    }
    buffer = image->rodata + descriptor_offset;
    if (read_u32(buffer, 0u) != PM_WIRE_DESCRIPTOR_MAGIC) {
        return PM_EPROTO;
    }
    descriptor_version = read_u16(buffer, 4u);
    descriptor_size = read_u16(buffer, 6u);
    if ((descriptor_version != 1u && descriptor_version != PM_WIRE_DESCRIPTOR_VERSION) ||
        descriptor_size < (descriptor_version == 1u ? 80u :
                           PM_WIRE_DESCRIPTOR_HEADER_SIZE) ||
        descriptor_size > image->rodata_size - descriptor_offset) {
        return PM_EPROTO;
    }
    required_count = read_u32(buffer, 48u);
    required_offset = read_u32(buffer, 52u);
    name_offset = read_u32(buffer, 56u);
    name_size = read_u32(buffer, 60u);
    version_offset = read_u32(buffer, 64u);
    version_size = read_u32(buffer, 68u);
    if (descriptor_version >= 2u) {
        extension_offset = read_u32(buffer, 80u);
        extension_size = read_u32(buffer, 84u);
    }
    if (required_count > PM_WIRE_DESCRIPTOR_MAX_REQUIRED_SERVICES) {
        return PM_EOVERFLOW;
    }
    required_bytes = (size_t)required_count * PM_UUID_SIZE;
    if (!range(required_offset, required_bytes, image->rodata_size) ||
        !string_range(image->rodata, name_offset, name_size,
                      image->rodata_size, PM_MAX_NAME_LENGTH + 1u) ||
        !string_range(image->rodata, version_offset, version_size,
                      image->rodata_size, PM_MAX_VERSION_LENGTH + 1u) ||
        !range(extension_offset, extension_size, image->rodata_size)) {
        return PM_EPROTO;
    }
    memset(result, 0, sizeof(*result));
    memcpy(&result->application_id, buffer + 8u, PM_UUID_SIZE);
    memcpy(&result->interface_id, buffer + 24u, PM_UUID_SIZE);
    result->interface_version = read_u32(buffer, 40u);
    result->plugin_id = read_u32(buffer, 44u);
    result->required_service_count = required_count;
    result->required_service_ids = (const pm_uuid_t *)(image->rodata + required_offset);
    result->name = (const char *)(image->rodata + name_offset);
    result->name_size = name_size - 1u;
    result->version = (const char *)(image->rodata + version_offset);
    result->version_size = version_size - 1u;
    result->create_address = read_u32(buffer, 72u);
    result->destroy_address = read_u32(buffer, 76u);
    result->extension = image->rodata + extension_offset;
    result->extension_size = extension_size;
    if (result->create_address == 0u || result->destroy_address == 0u) {
        return PM_EPROTO;
    }
    return PM_OK;
}
