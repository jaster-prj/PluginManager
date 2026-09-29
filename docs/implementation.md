# PluginManager Implementation Steps

This plan implements the reusable framework before adding EF800-specific
behavior. Each phase should leave the framework buildable and testable.

## Current Implementation Status

Status date: 2026-09-29

The project has reached a working physical-board proof of concept on a
NUCLEO-G0B1RE using Zephyr 4.4.1 and the Zephyr SDK. The implementation is no
longer only a plan. The host, native-sim, ARM cross-build, package pipeline,
and physical execution path are all present.

### Completed

- **Repository and module skeleton**: CMake, Kconfig, Zephyr module metadata,
  public headers, host tests, SDK-builder package placeholder, and project
  development container.
- **Generic ABI and IDs**: versioned C ABI, UUIDs, status codes, bounded
  strings, descriptors, services, contracts, and descriptor validation.
- **Application contract registry**: interface/service lookup, duplicate ID
  rejection, required-service filtering, and static-plugin lifecycle.
- **Package manifest**: fixed 108-byte little-endian header, bounded parser,
  memory-buffer and reader APIs, CRC32 validation, and deterministic package
  builder/tooling.
- **Constrained ELF loader**: ELF32 little-endian ARM `ET_REL` validation,
  `.text`, `.rodata`, `.data`, `.bss` loading, quotas, symbol validation,
  local and imported `R_ARM_ABS32` relocation handling, and controlled cleanup.
- **Serialized descriptor ABI**: wire descriptor decoding, bounded metadata,
  required-service records, lifecycle addresses, and ARM Thumb validation.
- **ARM target adapter**: executable-region checks, Thumb address preservation,
  application-owned function-pointer conversion, and constrained linker
  template.
- **Runtime manager**: package-to-ELF loading, descriptor adaptation, static
  and loaded plugin lifecycle, discovery/storage callbacks, available-plugin
  records, unload protection, operation states, and portable lock callbacks.
- **Zephyr tests**: native-sim ZTEST coverage for discovery, state transitions,
  mutex integration, wire descriptors, and loaded lifecycle.
- **Physical board test**: package and ELF loading, relocation, descriptor
  binding, plugin creation, plugin execution, and custom-information output on
  NUCLEO-G0B1RE.
- **External SPI NOR detection**: W25Q64-class device detected over Arduino
  SPI using Zephyr's `jedec,spi-nor` driver.

### Verification Results

- Host container tests: all host suites pass.
- ARM artifact integration: ARM ELF fixture builds, is packaged, parsed, and
  validated successfully.
- Zephyr native-sim tests: all PluginManager ZTEST cases pass.
- NUCLEO-G0B1RE physical test: successful output was observed:

```text
PluginManager physical test starting
PluginManager: W25Q128 flash ready write-block=1 erase-value=255
wire descriptor bind status: 0
example plugin information: PluginManager ARM example v1 (status 0)
```

The diagnostic label still says `W25Q128`; the detected JEDEC ID is `ef 40 16`,
which identifies the connected part as W25Q64-class, 64 Mbit / 8 MiB.

### Verified 2026-09-29

- Clean host build: all four suites pass when the ARM fixture is enabled.
- SDK builder: all 19 Python tests pass and a generated host runtime harness
  compiles and executes successfully.
- Zephyr native-sim: all five PluginManager test cases pass.
- Runtime ownership fixes retain service tables for the instance lifetime,
  release allocated images on unload and all load failures, and copy discovery
  adapters instead of retaining stack objects.
- ELF validation now initializes output state, validates section storage,
  bounds symbol strings, distinguishes executable and read-only sections, and
  computes the aligned BSS base after data.
- The Zephyr module entry point uses `zephyr/CMakeLists.txt`; host tests are no
  longer added to application firmware builds.

### Not Complete Yet

- SDK generation exists, but release/version policy and broader target profiles
  are not implemented.
- Host shared-library runtime is not implemented.
- Filesystem discovery is implemented and the NUCLEO test writes its embedded
  fixture to W25Q64-backed FAT before scanning and loading it. A clean firmware
  build passes; the updated FAT path still requires physical-board execution.
- The physical smoke test executes plugin text from RAM with MPU disabled.
  Production deployment needs an executable RAM region with an explicit MPU
  policy, or a memory-mapped executable flash controller.
- Plugin fault isolation, execution timeouts, stack measurement, signatures,
  sandboxing, and rollback protection remain deferred.

### Recommended Next Milestone

Implement an application-provided Zephyr flash storage adapter for the W25Q64,
write/read a package at a fixed test offset, and load it through
`pm_package_parse_reader` rather than using the compiled-in package. Keep plugin
execution in the proven RAM path until storage is validated independently.

## Phase 1: Repository and Module Skeleton

1. Create the Zephyr module metadata in `zephyr/module.yml`.
2. Add a minimal `CMakeLists.txt` and `Kconfig`.
3. Add public headers under `include/plugin_manager/`.
4. Add a host SDK-builder package with independent Python packaging.
5. Add framework unit-test and host-test targets.
6. Document the supported compiler, ARM architecture, and Zephyr revision
   assumptions.

Exit criteria:

- The module can be added to a Zephyr application without EF800 sources.
- Public headers compile as C11 and C++17-compatible headers.
- The host SDK-builder runs without Zephyr installed.

## Phase 2: Generic ABI and IDs

1. Define framework ABI version constants.
2. Define UUID representation and comparison helpers.
3. Define plugin, interface, service, and package descriptors.
4. Define descriptor-size and version compatibility rules.
5. Define bounded string and buffer conventions.
6. Define the generic plugin status/error contract.
7. Add compile-time layout assertions for package structures.

Tests must cover:

- Current and future descriptor sizes.
- Truncated descriptors.
- Unknown ABI versions.
- UUID mismatches.
- Integer overflow in size calculations.

## Phase 3: Application Contract Registry

1. Implement contract registration.
2. Implement interface lookup by UUID and version.
3. Implement service lookup by UUID and version.
4. Reject duplicate interface and service IDs.
5. Filter the service table by plugin-required capabilities.
6. Define whether compatible newer service versions are accepted.
7. Add registry tests with two fake application contracts.

The tests must demonstrate that the framework can load two different
application contracts without knowing their method semantics.

## Phase 4: Static Plugin Path

Implement a statically linked path before ELF loading:

1. Define a static descriptor registration mechanism.
2. Register a fake plugin in a test application.
3. Validate its descriptor through the same contract registry used by loaded
   plugins.
4. Create and destroy an instance.
5. Dispatch one interface operation through an application adapter.
6. Test service filtering and missing-service failures.

This path becomes the reference behavior for the later ELF loader.

## Phase 5: Package Manifest

1. Define the package header and manifest layout.
2. Implement bounded manifest parsing.
3. Validate magic, version, header size, offsets, lengths, and CRC.
4. Validate architecture and interface identity.
5. Reject duplicate plugin IDs.
6. Add a host package tool that combines manifest and image.
7. Add malformed-package fixtures.

The package parser must be usable without mounting a filesystem. It should
consume a read callback or memory buffer supplied by the application.

## Phase 6: ELF Profile and Loader

1. Specify the exact ELF class, machine, type, sections, and relocations.
2. Create a minimal linker script for a test plugin.
3. Build a plugin that exports only the descriptor symbol.
4. Implement ELF header and section validation.
5. Implement bounded image allocation/copy.
6. Implement supported relocation processing.
7. Implement service/import resolution.
8. Validate the descriptor after loading.
9. Add unload and cleanup behavior.
10. Reject unsupported ELF files with diagnostic errors.

Start with a single active plugin and a fixed maximum image/RAM/stack budget.
Do not add arbitrary symbol lookup or general dynamic linking.

Required tests:

- Valid generated image.
- Wrong architecture.
- Wrong ELF type.
- Unsupported relocation.
- Image outside package bounds.
- Image CRC mismatch.
- Descriptor outside loaded image.
- RAM and stack quota violations.
- Duplicate or missing imports.

## Phase 7: PluginManager Runtime

1. Implement directory enumeration through an application-provided storage
   adapter.
2. Filter candidate package names without relying on the suffix for type.
3. Read and validate manifests before loading images.
4. Maintain available-plugin records.
5. Add explicit load, create, execute, destroy, and unload operations.
6. Add operation-state and error reporting APIs.
7. Add a rescan operation with controlled synchronization.
8. Prevent unload while an operation is active.

The manager should not mount, unmount, or mutate application storage itself.

## Phase 8: SDK Builder

1. Define a versioned application-contract input format.
2. Validate the contract schema.
3. Copy or generate public interface headers.
4. Generate plugin skeletons and descriptor boilerplate.
5. Generate target CMake and linker configuration.
6. Generate host fake services.
7. Generate package manifest templates.
8. Generate examples and README content.
9. Embed contract and framework version metadata.
10. Add deterministic generation tests.

The first SDK-builder implementation uses the dependency-free JSON descriptor
format `plugin-manager/plugin-type-1`. It provides `validate`, `inspect`, and
`generate` commands, normalizes the descriptor, emits ABI headers, and creates
a minimal CMake plugin scaffold. The default package suffix is `.pmp`.

The next milestone adds generated wire-descriptor source and the `package`
command, which combines a target ELF with descriptor metadata into a `.pmp`
package. The package command reuses the version-1 package layout and CRC rules
implemented by PluginManager.

The generated descriptor now also emits required-service UUID records. The
descriptor validator requires every requested service to be declared by the
plugin-type descriptor, and package generation copies the same records into the
package manifest so the wire descriptor and package metadata cannot diverge.

The generated runtime API now emits stable UUID constants, typed service
accessors, and a typed interface accessor. Generated interface methods have
callable default implementations, allowing a generated plugin instance to be
created and invoked before application-specific method behavior is added.

Interface methods may declare a service/method delegation in the descriptor.
The generated method then resolves the typed service from the PluginManager
service table and invokes it, providing the first complete generated
interface-to-service execution path.

Generated SDKs now include a host runtime harness. It supplies a concrete
service vtable, creates the generated plugin instance, obtains the generated
interface, invokes the delegated interface method, and verifies the result.

Example:

```text
python -m plugin_sdk_builder validate --descriptor plugin-type.json
python -m plugin_sdk_builder generate \
  --descriptor plugin-type.json --output generated-sdk
```

The builder must fail when an interface contains unsupported ABI constructs,
such as unbounded strings, C++ types, or unversioned structure fields.

Recommended command shape:

```text
plugin-sdk-builder generate \
  --contract path/to/application-contract \
  --output path/to/generated-sdk
```

The generated SDK should support:

```text
cmake --preset host
cmake --preset target
cmake --build build/host
cmake --build build/target
```

## Phase 9: Host Runtime

1. Implement a host PluginManager using the same descriptor and contract ABI.
2. Load host shared libraries with platform adapters.
3. Provide fake port, storage, logging, clock, and UI services.
4. Make host loading reject target-only ELF artifacts.
5. Run generated plugin examples in CI.
6. Add protocol replay support for device plugins.

Host dynamic libraries are a development and test artifact. They are not
interchangeable with target `.pmp` packages.

## Phase 10: Documentation and Release Gates

1. Document ABI compatibility policy.
2. Document package format and target toolchain requirements.
3. Document how applications define contracts.
4. Document how applications implement services.
5. Document how generated SDKs are versioned.
6. Add a compatibility test between the current manager and generated SDK.
7. Add a package corpus containing valid and invalid fixtures.
8. Publish a framework release only when static, target-loader, host, and
   builder tests pass.

## Deferred Work

- Package signatures and key management.
- Sandboxing or WASM execution.
- Multiple simultaneously loaded plugin instances.
- Plugin hot replacement.
- Plugin-created threads.
- Persistent plugin configuration.
- Automatic update and rollback of plugins.
- Arbitrary filesystem capabilities.
