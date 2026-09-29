# PluginManager Architecture

## Purpose

PluginManager is a reusable Zephyr module and host-side SDK framework for
loading application-defined plugins. It must not contain EF800-specific
concepts such as measurements, ports, workspaces, LCDs, or keyboard events.

The framework provides:

- A versioned C ABI for plugins.
- Package discovery and manifest validation.
- Target-compatible ELF loading.
- Application contract registration.
- Capability-based service lookup.
- Plugin lifecycle and resource ownership.
- SDK generation from an application contract.
- Host-side build and test support.

The application provides the meaning of each plugin interface and service.
For EF800, those interfaces are Device and Calculation plugins.

## Boundary

```text
PluginManager framework
  plugin package format
  ELF validation/loading
  ABI and lifecycle
  generic interface/service IDs
  application-contract registration
  SDK generation

Application integration
  interface definitions
  service vtables
  service implementations
  plugin IDs
  application behavior
  menu and persistence integration
```

The framework must never include an application contract as a built-in
dependency. An application registers its contract during initialization.

## Repository Layout

```text
PluginManager/
  docs/
    architecture.md
    implementation.md
  include/plugin_manager/
    plugin_abi.h
    plugin_package.h
    plugin_manager.h
    plugin_contract.h
  src/
    plugin_manager.c
    plugin_registry.c
    plugin_package.c
    plugin_loader.c
    plugin_elf.c
  sdk-builder/
    README.md
    pyproject.toml
    plugin_sdk_builder/
    templates/
    schemas/
    tests/
  cmake/
    PluginManagerConfig.cmake
  zephyr/module.yml
  CMakeLists.txt
  Kconfig
```

The target module and the SDK builder are separate deliverables. The SDK
builder is a host tool and must not be compiled into MainApplication.

## Plugin Identity

A plugin is identified by an application contract and interface, not by its
filename or directory:

```text
application contract UUID
interface UUID and version
plugin ID
```

The `.pmp` suffix is the default PluginManager package convention. Applications
may configure another candidate suffix, but filenames never define plugin
identity or interface type.

Subdirectories may organize files, but must not define plugin semantics.

## ABI

The ABI is C-only and uses fixed-width types, caller-owned buffers, explicit
sizes, and negative errno-style status codes.

The generic descriptor has this shape conceptually:

```c
struct pm_plugin_descriptor {
    uint32_t framework_abi_version;
    uint32_t descriptor_size;
    uint8_t application_id[16];
    uint8_t interface_id[16];
    uint32_t interface_version;
    uint32_t plugin_id;
    const uint8_t *required_service_ids;
    size_t required_service_count;
    const char *name;
    const char *version;
    int (*create)(const struct pm_service_table *, void **instance);
    int (*destroy)(void *instance);
};
```

The exported symbol is:

```c
const struct pm_plugin_descriptor *pm_plugin_get_descriptor(void);
```

ABI rules:

- `descriptor_size` is checked before reading optional fields.
- Plugins must not expose C++ objects or Zephyr types.
- Pointers are valid only for the documented call duration.
- Services are immutable after plugin creation.
- A plugin must release all instance resources in `destroy`.
- ABI and interface versions are checked independently.
- Unknown interface versions are rejected unless compatibility is explicitly
  registered by the application.

## Application Contracts

An application registers a contract containing interface adapters and services:

```c
struct pm_application_contract {
    uint8_t application_id[16];
    uint32_t version;
    const struct pm_plugin_interface *interfaces;
    size_t interface_count;
    const struct pm_service *services;
    size_t service_count;
};
```

The generic manager performs identity, version, and capability checks. The
interface adapter performs application-specific descriptor validation and
instance creation.

```c
struct pm_plugin_interface {
    uint8_t interface_id[16];
    uint32_t version;
    const uint8_t *supported_service_ids;
    size_t supported_service_count;
    int (*validate)(const struct pm_plugin_descriptor *descriptor);
    int (*create)(const struct pm_plugin_descriptor *descriptor,
                  const struct pm_service_table *services,
                  void **instance);
};
```

This allows another application to register Audio, Camera, or Automation
interfaces without changing PluginManager.

## Services

Services are identified independently from interfaces:

```c
struct pm_service {
    uint8_t service_id[16];
    uint32_t version;
    const void *vtable;
};

struct pm_service_table {
    size_t count;
    const struct pm_service *items;
};
```

The manager passes only services requested by the plugin and available in the
current application state. Service vtables belong to the application contract.

The framework should provide generic services only where they are genuinely
generic, such as logging and monotonic time. Port, workspace, display, and
keyboard services belong to the consuming application.

## Package Format

The package is a manifest followed by a target plugin image and optional
metadata:

```text
package header
manifest
plugin image
optional signature
```

The manifest must contain at least:

```text
magic and package format version
header size
application UUID
interface UUID and version
plugin ID
target architecture
image offset and size
RAM and stack requirements
image CRC
name and version
required services
```

The loader must validate all offsets and lengths before reading or executing
the image. A package filename must never be trusted as metadata.

The initial trust model permits unsigned packages, but CRC and compatibility
validation are still mandatory. Signature fields should be reserved in the
format so signing can be added without changing the package layout.

## ELF Profile

The SDK must produce a constrained plugin ELF, not a normal Zephyr application
ELF. The profile must define:

- ARM Cortex-M architecture and ABI.
- Entry/descriptor symbol.
- Allowed ELF type.
- Supported relocation types.
- Import table and service resolution.
- Code, read-only data, data, BSS, and stack limits.
- No vector table or independent reset handler.
- No direct dependency on Zephyr kernel globals.

The first loader should support the smallest ELF profile required by the SDK.
It should reject arbitrary ELF files rather than attempting to be a general
dynamic linker.

## Lifecycle

```text
discover file
  -> read and validate manifest
  -> validate architecture, ABI, contract, and bounds
  -> load image and resolve imports
  -> validate exported descriptor
  -> register plugin
  -> create instance with selected services
  -> execute through interface adapter
  -> destroy instance
  -> unload image
```

The manager owns loading, memory, timeout, and unload policy. The application
owns when a plugin is invoked and whether its output changes application data.

The initial implementation should support one active instance per plugin and
should unload plugins only at controlled lifecycle points. Hot replacement is
deferred.

## Storage and Trust

The manager may scan a configured directory, but the application controls when
that directory is available. It must not assume that a filesystem is mounted
or writable.

The framework must expose storage hooks rather than directly depending on FAT.
The application decides whether plugins are scanned, installed, or removed.

Future security work may add signatures, allowlists, capability approval,
rollback protection, and execution quotas. The service-only ABI is the first
step toward those controls even though initial plugins are trusted.

## Host SDK

The SDK builder consumes an application contract and generates:

- Public ABI headers.
- Plugin skeletons.
- Target linker configuration.
- CMake helpers.
- Package-generation tool configuration.
- Host fake services.
- Native test scaffolding.
- Documentation and examples.

Host builds may produce `.dll` or `.so` for simulation, but those artifacts
are not target packages. The target build produces the constrained ELF and the
application-specific package suffix.

## Non-Goals

The first framework release does not provide:

- Arbitrary filesystem access to plugins.
- General-purpose dynamic linking.
- Plugin-created Zephyr threads.
- Plugin-owned interrupt handlers.
- Unbounded allocation.
- Automatic workspace mutation.
- Cross-application binary compatibility without a declared contract.
