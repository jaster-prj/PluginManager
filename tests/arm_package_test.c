#include "plugin_manager/plugin_elf.h"
#include "plugin_manager/plugin_package.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    FILE *file;
    long length;
    uint8_t *package;
    struct pm_package_manifest manifest;
    struct pm_elf_profile profile = {
        .max_sections = 32u, .max_image_size = 8192u, .max_ram_size = 8192u,
        .text_address = 0u, .rodata_address = 0x1000u,
    };

    assert(argc == 2);
    file = fopen(argv[1], "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    length = ftell(file);
    assert(length > 0 && fseek(file, 0, SEEK_SET) == 0);
    package = malloc((size_t)length);
    assert(package != NULL);
    assert(fread(package, 1u, (size_t)length, file) == (size_t)length);
    fclose(file);
    assert(pm_package_parse(package, (size_t)length, &manifest) == PM_OK);
    assert(manifest.target_architecture == PM_PACKAGE_TARGET_ARM_CORTEX_M);
    assert(pm_elf_validate(manifest.image, manifest.image_size, &profile) == PM_OK);
    free(package);
    return 0;
}
