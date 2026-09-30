# Introduction and Goals

## 1.1 Requirements Overview

PluginManager is a portable C framework and Zephyr module for application-defined plugins on resource-constrained microcontrollers. The framework owns generic mechanisms; the consuming application owns plugin semantics and policy.

**Current framework responsibilities:** generic C ABI, UUIDs, descriptors and status codes; static registration; `.pmp` discovery and validation; constrained ARM ELF32 loading into application-provided memory; pointer-free wire descriptor decoding and binding; service filtering; one active instance per registered plugin; Zephyr filesystem adapter and SDK/package tooling.

**Application responsibilities:** stable identities and typed interfaces; service implementations and hardware ownership; storage mounting and scan timing; executable-memory policy; plugin selection, invocation, monitoring and teardown; installation, trust, rollback and persistence policy.

PluginManager is neither a general dynamic linker nor a provider of direct Zephyr device, filesystem or interrupt access to plugins.

## 1.2 Quality Goals

| Priority | Goal | Architectural response |
| --- | --- | --- |
| 1 | Malformed-input safety | Bounds-oriented package/ELF checks, image CRC, limited relocations and descriptor cross-checks. |
| 2 | Application decoupling | Generic framework; cooperative plugins receive application-owned service vtables. |
| 3 | Predictable resource use | Application-defined package bounds and image buffers; one active instance per plugin. |
| 4 | Portability | C11 core, storage/memory/locking callbacks and separate Zephyr adapter. |
| 5 | Testability | Host and SDK tests, optional ARM fixture and native-sim tests, external physical reference host. |
| 6 | Evolvability | UUIDs, versions, size fields and pointer-free target descriptor. |

Security against malicious native code is a **future** goal. CRC32 and format validation do not authenticate code.

## 1.3 Stakeholders

| Stakeholder | Expectation |
| --- | --- |
| Application architect | Stable framework boundary and ownership rules. |
| Firmware integrator | Defined lifecycle, bounded memory and Zephyr integration. |
| Plugin developer | Versioned SDK, reproducible builds and actionable errors. |
| Security engineer | Signed packages, least privilege and W^X execution policy. |
| Test engineer | Host, target, malformed-input, lifecycle and hardware test points. |
| Operator | Diagnosable installation and compatibility without corrupting application state. |
