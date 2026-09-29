# Plugin SDK Builder

The first SDK-builder milestone uses a dependency-free JSON descriptor format.
It validates an application plugin type and generates deterministic C headers,
SDK metadata, and a minimal CMake project.

Run it from this directory:

```text
python -m plugin_sdk_builder validate \
  --descriptor examples/example-plugin-type.json

python -m plugin_sdk_builder inspect \
  --descriptor examples/example-plugin-type.json

python -m plugin_sdk_builder generate \
  --descriptor examples/example-plugin-type.json \
  --output build/example-sdk
```

The generated target project uses the constrained ARM relocatable profile. Build
it with the Zephyr SDK compiler prefix and the PluginManager source directory:

```text
cmake -S build/example-sdk -B build/example-target \
  -DPM_ARM_TOOLCHAIN_PREFIX=/opt/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi \
  -DPLUGIN_MANAGER_DIR=/workspace/PluginManager \
  -DPM_PLUGIN_SOURCES=/workspace/my-plugin/plugin.c \
  -DPM_PLUGIN_ID=42 -DPM_PLUGIN_NAME=example-plugin -DPM_PLUGIN_VERSION=1.0
cmake --build build/example-target
```

The canonical package suffix is `.pmp`. Package an existing target ELF with:

```text
python -m plugin_sdk_builder package \
  --descriptor examples/example-plugin-type.json \
  --elf plugin.elf --output plugin.pmp \
  --plugin-id 42 --name example-plugin --version 1.0
```

The compiler prefix is applied before CMake enables C, so it is sufficient to
select the cross-compiler. The generated project targets Cortex-M0+ and does not
create an editable plugin source file. Pass one or more semicolon-separated
source paths through `PM_PLUGIN_SOURCES`.

The generated descriptor source is emitted in `src/plugin_descriptor.c` and
uses the PluginManager wire descriptor ABI. The package command creates the
fixed little-endian package header and image CRC.

The generated runtime header provides `pm_sdk_service_find()` and exposes the
generated interface API through the plugin instance. Default generated methods
return `PM_ENOTSUP` until the plugin author supplies application behavior.

The SDK also generates `generated_ids.h` with application, interface, and
service UUID constants, plus typed helpers such as
`pm_sdk_transport_get()`. Use `pm_sdk_interface_get(instance)` to obtain the
generated interface vtable from a created plugin instance.

An interface method may delegate its generated default implementation to a
service method by declaring `default_service` and `default_method` in the
descriptor. This provides a complete service-backed example path without
exposing PluginManager internals to plugin code.

Example:

```json
{
  "name": "measure",
  "return": "int32",
  "default_service": "transport",
  "default_method": "measure",
  "parameters": [
    {"name": "instance", "type": "pointer(void)"},
    {"name": "result", "type": "pointer(example_result)"}
  ]
}
```

The generator validates that the referenced service and method exist and emits
the delegation through the typed service accessor.

The generated SDK also includes `tests/runtime_harness.c`. A host-only SDK
build supplies a concrete service vtable, creates the generated plugin,
obtains its generated interface, invokes the delegated method, and verifies
the returned value:

```text
cmake -S generated-sdk -B build/host -DPLUGIN_MANAGER_DIR=/path/to/PluginManager
cmake --build build/host
ctest --test-dir build/host
```

Required service UUIDs are declared in the descriptor and emitted into the
wire descriptor. They must refer to entries in the descriptor's `services`
array.

The package manifest receives the same required-service UUID list. This keeps
the descriptor and package compatibility checks consistent at runtime.
