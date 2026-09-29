# PluginManager

PluginManager is a portable C framework and Zephyr module for application-defined
plugins. Applications own their plugin contract, services, storage, memory, and
lifecycle policy. PluginManager provides the generic ABI, package validation,
constrained ARM ELF loading, discovery, registration, and SDK tooling.

The framework supports statically linked plugins and target packages. It does
not provide unrestricted dynamic linking or direct access to application
hardware and storage.

## Features

- Versioned C ABI with stable UUID-based application, interface, and service IDs.
- Static and package-loaded plugin registration.
- Fixed little-endian `.pmp` package format with bounded metadata and CRC32.
- Constrained ELF32 ARM `ET_REL` loader.
- ARM `R_ARM_ABS32` relocations and application-controlled import resolution.
- Serialized plugin descriptors with ARM Thumb entry validation.
- Application-provided service tables and lifecycle callbacks.
- Generic storage discovery and a Zephyr filesystem adapter.
- Deterministic Python SDK and package generation.
- Host, generated-SDK, ARM cross-build, and Zephyr native-sim tests.

## Repository Layout

```text
include/plugin_manager/   Public framework API
src/                      Portable runtime and Zephyr adapter
cmake/                    Plugin linker layout and Zephyr library definition
zephyr/                   Zephyr module metadata
sdk-builder/              Python SDK generator and package writer
tests/                    Host, ARM, and Zephyr tests
docs/                     Architecture and implementation status
```

## Host Build

Requirements:

- CMake 3.16 or newer.
- A C11 compiler.
- Ninja or another CMake-supported build tool.

```sh
cmake -S . -B build/host -DPM_BUILD_TESTS=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

## Development Container

The supplied container mounts this repository at `/workspace` and includes the
host tools and Zephyr SDK used by the project.

```sh
docker compose build
docker compose up -d
docker exec -w /workspace PluginManager_Devcontainer sh scripts/container-build.sh
```

Stop it with:

```sh
docker compose down
```

## Zephyr Integration

Add this repository as a west project or pass its path through
`ZEPHYR_EXTRA_MODULES` before `find_package(Zephyr)`:

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES "/path/to/PluginManager")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

Enable the module in the application configuration:

```conf
CONFIG_PLUGIN_MANAGER=y
CONFIG_PLUGIN_MANAGER_ZEPHYR_FS=y
```

`CONFIG_PLUGIN_MANAGER_ZEPHYR_FS` requires Zephyr filesystem support. The
application must mount and own the filesystem. PluginManager never formats,
mounts, unmounts, or accesses a flash device directly.

## Runtime Flow

A typical package-backed lifecycle is:

1. Define an application contract and its interfaces and services.
2. Create a manager with `pm_manager_create()`.
3. Mount application storage.
4. Attach it with `pm_manager_zephyr_attach_filesystem()`.
5. Discover `.pmp` packages with `pm_manager_zephyr_scan()`.
6. Load a selected package with `pm_manager_load_available()`.
7. Create and use one plugin instance.
8. Destroy the instance with `pm_manager_destroy_instance()`.
9. Unload the plugin with `pm_manager_unload()`.
10. Detach storage and destroy the manager.

The filesystem configuration and storage context must remain valid throughout
discovery and loading. Loaded executable and data memory may be supplied as
fixed buffers or through the bounded allocator callbacks.

## Package Format

Package format version 1 uses a fixed 108-byte little-endian header containing:

- Application and interface identity.
- Interface version and plugin ID.
- Target architecture and resource limits.
- Image location, size, and CRC32.
- Bounded plugin name and version records.
- Required-service UUID records.
- Reserved signature fields.

`pm_package_parse()` validates complete in-memory packages.
`pm_package_parse_reader()` validates a streamed image and CRC without requiring
a mounted filesystem.

## Supported ELF Profile

The target loader deliberately supports a narrow profile:

- ELF32, little-endian ARM.
- Relocatable `ET_REL` images.
- One executable `.text` region and one read-only `.rodata` region.
- Optional bounded `.data` and `.bss` regions.
- The `pm_plugin_get_descriptor` symbol in read-only data.
- ARM `R_ARM_ABS32` relocations only.
- Explicit application-controlled resolution of undefined imports.

The linker template is `cmake/plugin_manager_plugin.ld`. Packages outside this
profile are rejected before execution.

## ARM Fixture

Build and test the real Cortex-M package fixture with a Zephyr SDK toolchain:

```sh
cmake -S . -B build/arm \
  -DPM_BUILD_TESTS=ON \
  -DPM_BUILD_ARM_FIXTURE=ON \
  -DPM_ARM_TOOLCHAIN_PREFIX=/opt/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi
cmake --build build/arm
ctest --test-dir build/arm --output-on-failure
```

## SDK Builder

The dependency-free Python builder requires Python 3.10 or newer.

```sh
cd sdk-builder
python -m unittest discover -s tests -v
python -m plugin_sdk_builder validate \
  --descriptor examples/example-plugin-type.json
python -m plugin_sdk_builder generate \
  --descriptor examples/example-plugin-type.json \
  --output build/example-sdk
```

The generated SDK includes public contract headers, UUID constants, runtime and
descriptor sources, a constrained target build, package metadata, and a host
runtime harness.

## Zephyr Tests

From an initialized Zephyr workspace:

```sh
west twister -T /path/to/PluginManager/tests/zephyr \
  -p native_sim --inline-logs
```

## Current Limitations

- One active instance per registered plugin.
- No host shared-library loader.
- No package signature verification or key management.
- No fault isolation, execution timeout, or stack enforcement.
- No hot replacement or automatic rollback.
- No persistent plugin configuration.
- Production targets must define and validate an executable-memory policy.

See `docs/architecture.md` and `docs/implementation.md` for design details and
the current implementation status.

## License

Licensed under the Apache License, Version 2.0. See `LICENCE`.
