#include "plugin_manager/plugin_manager_zephyr.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>


static int pm_status(int status)
{
    if (status == 0) {
        return PM_OK;
    }
    switch (-status) {
    case ENOENT:
        return PM_ENOENT;
    case ENOMEM:
        return PM_ENOMEM;
    case EBUSY:
        return PM_EBUSY;
    case EINVAL:
        return PM_EINVAL;
    case EOVERFLOW:
        return PM_EOVERFLOW;
    default:
        return PM_EIO;
    }
}

static int make_path(const struct pm_zephyr_filesystem *filesystem,
                     const char *name, char *path, size_t capacity)
{
    int length;

    if (filesystem == NULL || filesystem->mount == NULL ||
        filesystem->mount->mnt_point == NULL ||
        filesystem->plugin_directory == NULL || name == NULL ||
        name[0] == '/' || strstr(name, "..") != NULL ||
        strchr(name, '/') != NULL) {
        return PM_EINVAL;
    }
    length = snprintk(path, capacity, "%s/%s/%s",
                      filesystem->mount->mnt_point,
                      filesystem->plugin_directory, name);
    return length < 0 || (size_t)length >= capacity ? PM_EOVERFLOW : PM_OK;
}

static int make_directory_path(const struct pm_zephyr_filesystem *filesystem,
                               char *path, size_t capacity)
{
    int length;

    if (filesystem == NULL || filesystem->mount == NULL ||
        filesystem->mount->mnt_point == NULL ||
        filesystem->plugin_directory == NULL) {
        return PM_EINVAL;
    }
    length = snprintk(path, capacity, "%s/%s",
                      filesystem->mount->mnt_point,
                      filesystem->plugin_directory);
    return length < 0 || (size_t)length >= capacity ? PM_EOVERFLOW : PM_OK;
}

static int storage_list(void *context, size_t index, char *name,
                        size_t capacity, int *present)
{
    const struct pm_zephyr_filesystem *filesystem =
        context;
    struct fs_dir_t directory;
    struct fs_dirent entry;
    char path[256];
    size_t current = 0u;
    int status;

    if (filesystem == NULL || name == NULL || capacity == 0u ||
        present == NULL) {
        return PM_EINVAL;
    }
    *present = 0;
    status = make_directory_path(filesystem, path, sizeof(path));
    if (status != PM_OK) {
        return status;
    }
    fs_dir_t_init(&directory);
    status = fs_opendir(&directory, path);
    if (status != 0) {
        return pm_status(status);
    }
    for (;;) {
        status = fs_readdir(&directory, &entry);
        if (status != 0) {
            (void)fs_closedir(&directory);
            return pm_status(status);
        }
        if (entry.name[0] == '\0') {
            break;
        }
        if (entry.type != FS_DIR_ENTRY_FILE) {
            continue;
        }
        if (current++ != index) {
            continue;
        }
        if (strlen(entry.name) + 1u > capacity) {
            (void)fs_closedir(&directory);
            return PM_EOVERFLOW;
        }
        memcpy(name, entry.name, strlen(entry.name) + 1u);
        *present = 1;
        break;
    }
    status = fs_closedir(&directory);
    return status == 0 ? PM_OK : pm_status(status);
}

static int storage_size(void *context, const char *name, uint32_t *size)
{
    const struct pm_zephyr_filesystem *filesystem =
        context;
    struct fs_dirent entry;
    char path[256];
    int status;

    if (size == NULL) {
        return PM_EINVAL;
    }
    status = make_path(filesystem, name, path, sizeof(path));
    if (status != PM_OK) {
        return status;
    }
    status = fs_stat(path, &entry);
    if (status != 0) {
        return pm_status(status);
    }
    if (entry.type != FS_DIR_ENTRY_FILE ||
        (uint64_t)entry.size > UINT32_MAX) {
        return PM_EPROTO;
    }
    *size = (uint32_t)entry.size;
    return PM_OK;
}

static int storage_read(void *context, const char *name, uint32_t offset,
                        void *destination, size_t size)
{
    const struct pm_zephyr_filesystem *filesystem =
        context;
    struct fs_file_t file;
    char path[256];
    ssize_t count;
    int status;

    if (destination == NULL && size != 0u) {
        return PM_EINVAL;
    }
    status = make_path(filesystem, name, path, sizeof(path));
    if (status != PM_OK) {
        return status;
    }
    fs_file_t_init(&file);
    status = fs_open(&file, path, FS_O_READ);
    if (status != 0) {
        return pm_status(status);
    }
    status = fs_seek(&file, (off_t)offset, FS_SEEK_SET);
    if (status == 0 && size > 0u) {
        count = fs_read(&file, destination, size);
        status = count == (ssize_t)size ? 0 :
            (count < 0 ? (int)count : -EIO);
    }
    (void)fs_close(&file);
    return status == 0 ? PM_OK : pm_status(status);
}

static int valid_config(const struct pm_zephyr_filesystem *filesystem)
{
    return filesystem != NULL && filesystem->mount != NULL &&
           filesystem->mount->mnt_point != NULL &&
           filesystem->plugin_directory != NULL &&
           filesystem->max_entries > 0u && filesystem->max_name_length > 0u &&
           filesystem->max_package_size >= PM_PACKAGE_HEADER_SIZE;
}

int pm_manager_zephyr_attach_filesystem(
    struct pm_manager *manager,
    const struct pm_zephyr_filesystem *filesystem)
{
    if (manager == NULL || !valid_config(filesystem)) {
        return PM_EINVAL;
    }
    if (pm_manager_filesystem_is_attached(manager)) {
        return PM_EBUSY;
    }
    /* The application retains the mount object for the whole manager lifetime. */
    return pm_manager_filesystem_attach(manager, filesystem);
}

int pm_manager_zephyr_scan(struct pm_manager *manager,
                            const struct pm_zephyr_filesystem *filesystem)
{
    struct pm_manager_scan_config scan;
    struct pm_storage_adapter storage;

    if (manager == NULL || !valid_config(filesystem)) {
        return PM_EINVAL;
    }
    if (!pm_manager_filesystem_is_same(manager, filesystem)) {
        return PM_EPERM;
    }
    storage = (struct pm_storage_adapter){
        .context = (void *)filesystem,
        .list = storage_list,
        .size = storage_size,
        .read = storage_read,
        .max_entries = filesystem->max_entries,
    };
    scan.storage = &storage;
    scan.candidate_suffix = filesystem->candidate_suffix;
    scan.max_name_length = filesystem->max_name_length;
    scan.max_package_size = filesystem->max_package_size;
    return pm_manager_scan(manager, &scan);
}

int pm_manager_zephyr_detach_filesystem(struct pm_manager *manager)
{
    return pm_manager_filesystem_detach(manager);
}
