#ifndef PLUGIN_MANAGER_PLUGIN_MANAGER_ZEPHYR_H
#define PLUGIN_MANAGER_PLUGIN_MANAGER_ZEPHYR_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/fs/fs.h>

#include "plugin_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The application owns and mounts the filesystem before attaching it. */
struct pm_zephyr_filesystem {
    const struct fs_mount_t *mount;
    /* Directory relative to mount->mnt_point, without leading slash. */
    const char *plugin_directory;
    const char *candidate_suffix;
    size_t max_entries;
    size_t max_name_length;
    size_t max_package_size;
};

int pm_manager_zephyr_attach_filesystem(
    struct pm_manager *manager,
    const struct pm_zephyr_filesystem *filesystem);

int pm_manager_zephyr_scan(struct pm_manager *manager,
                            const struct pm_zephyr_filesystem *filesystem);

int pm_manager_zephyr_detach_filesystem(struct pm_manager *manager);

#ifdef __cplusplus
}
#endif

#endif
