#ifndef PLUGIN_MANAGER_PLUGIN_PACKAGE_H
#define PLUGIN_MANAGER_PLUGIN_PACKAGE_H

#include <stddef.h>
#include <stdint.h>

#include "plugin_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_PACKAGE_MAGIC 0x504D504Bu /* "PMPK" */
#define PM_PACKAGE_FORMAT_VERSION 1u
#define PM_PACKAGE_HEADER_SIZE 108u
#define PM_PACKAGE_TARGET_ARM_CORTEX_M 1u
#define PM_PACKAGE_DEFAULT_SUFFIX ".pmp"

/*
 * Package fields are encoded little-endian and must not be read as a C struct.
 * The fixed header is 108 bytes. It is followed by referenced strings,
 * required-service UUIDs, the image, and optional reserved signature bytes.
 */
#define PM_PACKAGE_MAX_REQUIRED_SERVICES 64u
#define PM_PACKAGE_MAX_NAME_LENGTH PM_MAX_NAME_LENGTH
#define PM_PACKAGE_MAX_VERSION_LENGTH PM_MAX_VERSION_LENGTH

typedef int (*pm_package_read_fn)(void *context, uint32_t offset,
                                  void *destination, size_t size);

struct pm_package_manifest {
    pm_uuid_t application_id;
    pm_uuid_t interface_id;
    uint32_t interface_version;
    uint32_t plugin_id;
    uint16_t target_architecture;
    uint16_t flags;
    uint32_t image_offset;
    uint32_t image_size;
    uint32_t ram_size;
    uint32_t stack_size;
    uint32_t image_crc32;
    const char *name;
    size_t name_size;
    const char *version;
    size_t version_size;
    const pm_uuid_t *required_service_ids;
    size_t required_service_count;
    uint32_t signature_offset;
    uint32_t signature_size;
    const uint8_t *image;
};

struct pm_package_build_config {
    pm_uuid_t application_id;
    pm_uuid_t interface_id;
    uint32_t interface_version;
    uint32_t plugin_id;
    uint16_t target_architecture;
    uint16_t flags;
    uint32_t ram_size;
    uint32_t stack_size;
    const char *name;
    const char *version;
    const pm_uuid_t *required_service_ids;
    size_t required_service_count;
    const uint8_t *image;
    size_t image_size;
};

int pm_package_build(const struct pm_package_build_config *config,
                     uint8_t *buffer, size_t buffer_capacity,
                     size_t *package_size);

int pm_package_parse(const uint8_t *buffer, size_t buffer_size,
                     struct pm_package_manifest *result);

/* Reader mode validates the fixed header and image CRC without retaining data. */
int pm_package_parse_reader(pm_package_read_fn read, void *context,
                            uint32_t package_size,
                            struct pm_package_manifest *result);

uint32_t pm_package_crc32(const uint8_t *buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif
