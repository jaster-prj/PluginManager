#include "plugin_manager/plugin_package.h"

#include <string.h>

enum {
    FIELD_MAGIC = 0u,
    FIELD_FORMAT_VERSION = 4u,
    FIELD_HEADER_SIZE = 6u,
    FIELD_TOTAL_SIZE = 8u,
    FIELD_APPLICATION_ID = 12u,
    FIELD_INTERFACE_ID = 28u,
    FIELD_INTERFACE_VERSION = 44u,
    FIELD_PLUGIN_ID = 48u,
    FIELD_TARGET_ARCHITECTURE = 52u,
    FIELD_FLAGS = 54u,
    FIELD_IMAGE_OFFSET = 56u,
    FIELD_IMAGE_SIZE = 60u,
    FIELD_RAM_SIZE = 64u,
    FIELD_STACK_SIZE = 68u,
    FIELD_IMAGE_CRC32 = 72u,
    FIELD_NAME_OFFSET = 76u,
    FIELD_NAME_SIZE = 80u,
    FIELD_VERSION_OFFSET = 84u,
    FIELD_VERSION_SIZE = 88u,
    FIELD_REQUIRED_SERVICE_OFFSET = 92u,
    FIELD_REQUIRED_SERVICE_COUNT = 96u,
    FIELD_SIGNATURE_OFFSET = 100u,
    FIELD_SIGNATURE_SIZE = 104u,
};

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

static size_t align4(size_t value)
{
    return (value + 3u) & ~3u;
}

int pm_package_build(const struct pm_package_build_config *config,
                     uint8_t *buffer, size_t buffer_capacity,
                     size_t *package_size)
{
    size_t name_size;
    size_t version_size;
    size_t service_size;
    size_t name_offset;
    size_t version_offset;
    size_t service_offset;
    size_t image_offset;
    size_t total_size;

    if (config == NULL || buffer == NULL || package_size == NULL ||
        config->image == NULL || config->image_size == 0u ||
        config->name == NULL || config->version == NULL ||
        config->required_service_count > PM_PACKAGE_MAX_REQUIRED_SERVICES ||
        (config->required_service_count > 0u &&
         config->required_service_ids == NULL)) {
        return PM_EINVAL;
    }
    name_size = strlen(config->name) + 1u;
    version_size = strlen(config->version) + 1u;
    service_size = config->required_service_count * PM_UUID_SIZE;
    if (name_size > PM_PACKAGE_MAX_NAME_LENGTH ||
        version_size > PM_PACKAGE_MAX_VERSION_LENGTH ||
        config->image_size > UINT32_MAX) {
        return PM_EOVERFLOW;
    }
    name_offset = PM_PACKAGE_HEADER_SIZE;
    version_offset = name_offset + name_size;
    service_offset = align4(version_offset + version_size);
    image_offset = align4(service_offset + service_size);
    total_size = image_offset + config->image_size;
    if (total_size > buffer_capacity || total_size > UINT32_MAX) {
        return PM_EOVERFLOW;
    }
    memset(buffer, 0, total_size);
    write_u32(buffer, FIELD_MAGIC, PM_PACKAGE_MAGIC);
    write_u16(buffer, FIELD_FORMAT_VERSION, PM_PACKAGE_FORMAT_VERSION);
    write_u16(buffer, FIELD_HEADER_SIZE, PM_PACKAGE_HEADER_SIZE);
    write_u32(buffer, FIELD_TOTAL_SIZE, (uint32_t)total_size);
    memcpy(buffer + FIELD_APPLICATION_ID, &config->application_id, PM_UUID_SIZE);
    memcpy(buffer + FIELD_INTERFACE_ID, &config->interface_id, PM_UUID_SIZE);
    write_u32(buffer, FIELD_INTERFACE_VERSION, config->interface_version);
    write_u32(buffer, FIELD_PLUGIN_ID, config->plugin_id);
    write_u16(buffer, FIELD_TARGET_ARCHITECTURE, config->target_architecture);
    write_u16(buffer, FIELD_FLAGS, config->flags);
    write_u32(buffer, FIELD_IMAGE_OFFSET, (uint32_t)image_offset);
    write_u32(buffer, FIELD_IMAGE_SIZE, (uint32_t)config->image_size);
    write_u32(buffer, FIELD_RAM_SIZE, config->ram_size);
    write_u32(buffer, FIELD_STACK_SIZE, config->stack_size);
    write_u32(buffer, FIELD_IMAGE_CRC32,
              pm_package_crc32(config->image, config->image_size));
    write_u32(buffer, FIELD_NAME_OFFSET, (uint32_t)name_offset);
    write_u32(buffer, FIELD_NAME_SIZE, (uint32_t)name_size);
    write_u32(buffer, FIELD_VERSION_OFFSET, (uint32_t)version_offset);
    write_u32(buffer, FIELD_VERSION_SIZE, (uint32_t)version_size);
    write_u32(buffer, FIELD_REQUIRED_SERVICE_OFFSET, (uint32_t)service_offset);
    write_u32(buffer, FIELD_REQUIRED_SERVICE_COUNT,
              (uint32_t)config->required_service_count);
    memcpy(buffer + name_offset, config->name, name_size);
    memcpy(buffer + version_offset, config->version, version_size);
    memcpy(buffer + service_offset, config->required_service_ids, service_size);
    memcpy(buffer + image_offset, config->image, config->image_size);
    *package_size = total_size;
    return PM_OK;
}

static int range_valid(uint32_t offset, uint32_t length, uint32_t total)
{
    return offset <= total && length <= total - offset;
}

static int string_valid(const uint8_t *buffer, uint32_t offset, uint32_t length,
                        uint32_t total, size_t maximum)
{
    if (!range_valid(offset, length, total) || length == 0u || length > maximum) {
        return 0;
    }
    return buffer[offset + length - 1u] == '\0';
}

uint32_t pm_package_crc32(const uint8_t *buffer, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    unsigned int bit;

    if (buffer == NULL && size != 0u) {
        return 0u;
    }
    for (i = 0u; i < size; ++i) {
        crc ^= buffer[i];
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc >> 1u) ^ (0xEDB88320u & (uint32_t)-(int)(crc & 1u));
        }
    }
    return ~crc;
}

int pm_package_parse(const uint8_t *buffer, size_t buffer_size,
                     struct pm_package_manifest *result)
{
    uint32_t total_size;
    uint32_t header_size;
    uint32_t image_offset;
    uint32_t image_size;
    uint32_t name_offset;
    uint32_t name_size;
    uint32_t version_offset;
    uint32_t version_size;
    uint32_t service_offset;
    uint32_t service_count;
    uint32_t signature_offset;
    uint32_t signature_size;
    uint32_t expected_crc;
    uint64_t service_bytes;

    if (buffer == NULL || result == NULL || buffer_size < PM_PACKAGE_HEADER_SIZE) {
        return PM_EINVAL;
    }
    if (read_u32(buffer, FIELD_MAGIC) != PM_PACKAGE_MAGIC ||
        read_u16(buffer, FIELD_FORMAT_VERSION) != PM_PACKAGE_FORMAT_VERSION) {
        return PM_EPROTO;
    }
    header_size = read_u16(buffer, FIELD_HEADER_SIZE);
    total_size = read_u32(buffer, FIELD_TOTAL_SIZE);
    if (header_size < PM_PACKAGE_HEADER_SIZE || header_size > total_size ||
        total_size > buffer_size) {
        return PM_EOVERFLOW;
    }

    image_offset = read_u32(buffer, FIELD_IMAGE_OFFSET);
    image_size = read_u32(buffer, FIELD_IMAGE_SIZE);
    name_offset = read_u32(buffer, FIELD_NAME_OFFSET);
    name_size = read_u32(buffer, FIELD_NAME_SIZE);
    version_offset = read_u32(buffer, FIELD_VERSION_OFFSET);
    version_size = read_u32(buffer, FIELD_VERSION_SIZE);
    service_offset = read_u32(buffer, FIELD_REQUIRED_SERVICE_OFFSET);
    service_count = read_u32(buffer, FIELD_REQUIRED_SERVICE_COUNT);
    signature_offset = read_u32(buffer, FIELD_SIGNATURE_OFFSET);
    signature_size = read_u32(buffer, FIELD_SIGNATURE_SIZE);

    if (read_u16(buffer, FIELD_TARGET_ARCHITECTURE) != PM_PACKAGE_TARGET_ARM_CORTEX_M ||
        !range_valid(image_offset, image_size, total_size) ||
        !string_valid(buffer, name_offset, name_size, total_size,
                      PM_PACKAGE_MAX_NAME_LENGTH) ||
        !string_valid(buffer, version_offset, version_size, total_size,
                      PM_PACKAGE_MAX_VERSION_LENGTH) ||
        service_count > PM_PACKAGE_MAX_REQUIRED_SERVICES ||
        !range_valid(service_offset, 0u, total_size) ||
        !range_valid(signature_offset, signature_size, total_size)) {
        return PM_EPROTO;
    }
    service_bytes = (uint64_t)service_count * PM_UUID_SIZE;
    if (service_bytes > UINT32_MAX ||
        !range_valid(service_offset, (uint32_t)service_bytes, total_size)) {
        return PM_EOVERFLOW;
    }
    expected_crc = read_u32(buffer, FIELD_IMAGE_CRC32);
    if (pm_package_crc32(buffer + image_offset, image_size) != expected_crc) {
        return PM_EPROTO;
    }

    memset(result, 0, sizeof(*result));
    memcpy(&result->application_id.bytes[0], buffer + FIELD_APPLICATION_ID,
           PM_UUID_SIZE);
    memcpy(&result->interface_id.bytes[0], buffer + FIELD_INTERFACE_ID,
           PM_UUID_SIZE);
    result->interface_version = read_u32(buffer, FIELD_INTERFACE_VERSION);
    result->plugin_id = read_u32(buffer, FIELD_PLUGIN_ID);
    result->target_architecture = read_u16(buffer, FIELD_TARGET_ARCHITECTURE);
    result->flags = read_u16(buffer, FIELD_FLAGS);
    result->image_offset = image_offset;
    result->image_size = image_size;
    result->ram_size = read_u32(buffer, FIELD_RAM_SIZE);
    result->stack_size = read_u32(buffer, FIELD_STACK_SIZE);
    result->image_crc32 = expected_crc;
    result->name = (const char *)(buffer + name_offset);
    result->name_size = name_size - 1u;
    result->version = (const char *)(buffer + version_offset);
    result->version_size = version_size - 1u;
    result->required_service_ids = (const pm_uuid_t *)(buffer + service_offset);
    result->required_service_count = service_count;
    result->signature_offset = signature_offset;
    result->signature_size = signature_size;
    result->image = buffer + image_offset;
    return PM_OK;
}

int pm_package_parse_reader(pm_package_read_fn read, void *context,
                            uint32_t package_size,
                            struct pm_package_manifest *result)
{
    uint8_t header[PM_PACKAGE_HEADER_SIZE];
    uint8_t chunk[64];
    uint32_t image_offset;
    uint32_t image_size;
    uint32_t expected_crc;
    uint32_t offset;
    uint32_t remaining;
    uint32_t crc = 0xFFFFFFFFu;
    size_t chunk_size;
    size_t i;
    unsigned int bit;
    int status;

    if (read == NULL || result == NULL || package_size < PM_PACKAGE_HEADER_SIZE) {
        return PM_EINVAL;
    }
    status = read(context, 0u, header, sizeof(header));
    if (status != PM_OK) {
        return status;
    }
    if (read_u32(header, FIELD_MAGIC) != PM_PACKAGE_MAGIC ||
        read_u16(header, FIELD_FORMAT_VERSION) != PM_PACKAGE_FORMAT_VERSION ||
        read_u16(header, FIELD_HEADER_SIZE) < PM_PACKAGE_HEADER_SIZE ||
        read_u32(header, FIELD_TOTAL_SIZE) != package_size ||
        read_u16(header, FIELD_TARGET_ARCHITECTURE) !=
            PM_PACKAGE_TARGET_ARM_CORTEX_M) {
        return PM_EPROTO;
    }
    image_offset = read_u32(header, FIELD_IMAGE_OFFSET);
    image_size = read_u32(header, FIELD_IMAGE_SIZE);
    expected_crc = read_u32(header, FIELD_IMAGE_CRC32);
    if (!range_valid(image_offset, image_size, package_size)) {
        return PM_EOVERFLOW;
    }
    offset = image_offset;
    remaining = image_size;
    while (remaining > 0u) {
        chunk_size = remaining < sizeof(chunk) ? remaining : sizeof(chunk);
        status = read(context, offset, chunk, chunk_size);
        if (status != PM_OK) {
            return status;
        }
        for (i = 0u; i < chunk_size; ++i) {
            crc ^= chunk[i];
            for (bit = 0u; bit < 8u; ++bit) {
                crc = (crc >> 1u) ^
                      (0xEDB88320u & (uint32_t)-(int)(crc & 1u));
            }
        }
        offset += (uint32_t)chunk_size;
        remaining -= (uint32_t)chunk_size;
    }
    if (~crc != expected_crc) {
        return PM_EPROTO;
    }
    /* Reader mode returns validated scalar metadata; pointer views are unavailable. */
    memset(result, 0, sizeof(*result));
    result->image_offset = image_offset;
    result->image_size = image_size;
    result->image_crc32 = expected_crc;
    return PM_OK;
}
