# Architecture Constraints

## 2.1 Current Technical Constraints

| Area | Constraint |
| --- | --- |
| Language | C11 core and target ABI; no C++ objects in public contracts. |
| ABI | Framework ABI version 1; 16-byte UUIDs. |
| Package | `.pmp` v1; 108-byte little-endian fixed header. |
| Image | ELF32, little-endian ARM `ET_REL`; one recognized executable non-writable and one recognized read-only region, optional data and BSS. Runtime permissions are application-owned. |
| Relocations | `R_ARM_ABS32` only; imported symbols require an application resolver. |
| Entries | Thumb addresses inside loaded executable text. |
| Lifecycle | One active instance per plugin; static plugins cannot be unloaded. |
| Storage and memory | Application-owned mounts, fixed image buffers or paired allocation/free callbacks. |
| Generated SDK target | Cortex-M0+, AAPCS, soft-float; runtime does **not** verify CPU subtype, AAPCS or float ABI. |

## 2.2 Fields Without Complete Runtime Semantics

| Field/API | Current behavior |
| --- | --- |
| `pm_application_contract.version` | Stored but not used for compatibility decisions. |
| `pm_plugin_interface.supported_service_ids` | Declared but not enforced. |
| `pm_plugin_interface.create` | Bypassed: manager calls the descriptor's `create`. |
| Service version | Manager matches UUID only; plugins may check the version themselves. |
| Package `stack_size` | Parsed but no stack is allocated or measured by the framework. |
| Package flags | Parsed but no flags have defined behavior. |
| Signature offset/size | Bounds-checked reserved data; no signature verification. |
| `CONFIG_PLUGIN_MANAGER_MAX_PLUGINS` | Defined but runtime capacity comes from `pm_manager_config.max_plugins`. |

## 2.3 Organizational and Deployment Constraints

- Host CMake requires 3.16+; SDK builder requires Python 3.10+ and does not run in firmware.
- Zephyr integration uses `zephyr/module.yml` and Kconfig.
- Production integrations should pin an immutable PluginManager revision.
- Packages depend on the consuming application's contract; cross-application compatibility is not implicit.

## 2.4 Current Assumptions

Current integrations assume trusted, cooperative native plugins; correct non-overlapping application memory and executable permissions; valid contract, service, storage and callback lifetimes; externally serialized raw instance access; stable storage bytes for the recorded package size; intentionally constrained import resolvers; and callbacks that do not re-enter the manager. See [crosscutting concepts](../08-Crosscutting_Concepts/crosscutting.md) for the callback and synchronization contract.
