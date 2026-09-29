#include "plugin_manager/plugin_package.h"

#include <stdio.h>
#include <stdlib.h>

static int read_file(const char *path, uint8_t **data, size_t *size)
{
    FILE *file = fopen(path, "rb");
    long length;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0) return PM_EINVAL;
    length = ftell(file);
    if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) return PM_EINVAL;
    *data = malloc((size_t)length);
    if (*data == NULL || fread(*data, 1u, (size_t)length, file) != (size_t)length) {
        fclose(file);
        return PM_ENOMEM;
    }
    fclose(file);
    *size = (size_t)length;
    return PM_OK;
}

int main(int argc, char **argv)
{
    uint8_t *image;
    uint8_t *package;
    size_t image_size;
    size_t package_size;
    struct pm_package_build_config config = {
        .application_id = {{1u}}, .interface_id = {{2u}},
        .interface_version = 1u, .plugin_id = 42u,
        .target_architecture = PM_PACKAGE_TARGET_ARM_CORTEX_M,
        .ram_size = 4096u, .stack_size = 1024u,
        .name = "arm-plugin", .version = "1.0",
    };
    FILE *output;

    if (argc != 3 || read_file(argv[1], &image, &image_size) != PM_OK) return 2;
    config.image = image;
    config.image_size = image_size;
    package = malloc(image_size + 512u);
    if (package == NULL || pm_package_build(&config, package, image_size + 512u,
                                            &package_size) != PM_OK) return 3;
    output = fopen(argv[2], "wb");
    if (output == NULL || fwrite(package, 1u, package_size, output) != package_size) return 4;
    fclose(output);
    free(package);
    free(image);
    return 0;
}
