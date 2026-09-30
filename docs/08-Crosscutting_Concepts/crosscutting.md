# Crosscutting Concepts

## 8.1 Identity and Compatibility

The current identity tuple is **application UUID + interface UUID/version + plugin ID**. Filenames and `.pmp` suffixes filter candidates; they are neither identity nor trust metadata. Load cross-checks package and wire-descriptor identity plus ordered required-service UUIDs. Exact interface validation happens after image loading and adaptation but before plugin creation.

**Future:** define application-contract compatibility and versioned service requirements; reject unsupported requests before loading native code.

## 8.2 Error Handling and Diagnostics

The C API uses negative errno-style `PM_*` statuses. The Zephyr adapter translates filesystem errors. Discovery skips invalid candidates rather than exposing a per-file diagnostic.

**Future:** return stable structured diagnostics including stage, identity, source and compatibility reason.

## 8.3 Ownership and Lifetime

| Object | Owner/lifetime |
| --- | --- |
| Contract and static descriptor | Application-owned; must outlive manager. |
| Storage adapter | Value copied by manager; context application-owned. |
| Zephyr mount/configuration | Application-owned through scan/load and detach. Detach currently requires **zero** registered plugins, including static ones. |
| Availability metadata | Manager-owned until rescan/destruction. |
| Loaded descriptor metadata | Manager-owned until unload. |
| Fixed image buffers | Application-owned for entire loaded lifetime. |
| Allocator-provided image | Released through paired callback. |
| Filtered service table | Manager-owned for active instance lifetime. |
| Instance | Plugin-owned; explicit destroy preserves failure, manager destruction ignores destroy failures. |

## 8.4 Memory and Resources

Declared package limits are checked before loading and each section's buffer capacity is checked before its copy/clear. Aggregate actual ELF image/RAM profile limits are checked **after** section writes. Scan and load each allocate an entire package temporarily. Allocator callbacks receive the manifest image bound for **each** text and rodata buffer, and the RAM bound for **each** data and BSS buffer. Actual data+BSS consumption is not checked against package-declared RAM.

**Future:** streaming validation, caller-owned scratch, overflow-safe range arithmetic, aggregate limits before writes, enforced stacks and high-water metrics.

## 8.5 Synchronization

Mutating operations use an operation state and optional lock callbacks. Storage, allocation/free, import resolution, adaptation, interface validation and plugin create/destroy callbacks currently run **under the configured manager lock**. Callbacks must not re-enter the manager; a blocking non-recursive lock can deadlock before state-based rejection. Getters, manager destruction and raw instance calls are not comprehensively synchronized. Applications must serialize operations that can race with scan, destroy or unload.

**Future:** choose fully synchronized stable handles or a documented single-owner model, and move re-entrant callbacks outside internal locks with safe state transitions.

## 8.6 Security and Trust

**Current:** bounds-oriented parsing, ELF-image CRC, identity checks, recognized section mapping, relocation whitelist, import resolver and Thumb entry-range validation. CRC does **not** cover manifest metadata. `const` service pointers and expected service calls are cooperative conventions, not memory isolation. There is no authenticity, fault containment, deadline or stack isolation.

**Future trust chain:**

```text
trusted firmware -> protected keys and rollback state -> canonical signed package
  -> approved application/interface/services -> W^X image -> supervised invocation
```

## 8.7 Capability Model

The manager passes only records requested by UUID via a `const` service-table API. Native code can nevertheless address application memory. V1 does not enforce interface-supported services, version compatibility, dynamic approval or quotas. Future policy must perform these checks before load and provide safe revocation and metering.

## 8.8 Versioning and Evolution

Framework ABI and package format evolve independently; interfaces and services belong to each application contract. Public application types should use fixed-width fields, ABI versions, `struct_size` and reserved bytes. Current public C APIs are pre-release: no semantic-versioned source/binary compatibility policy exists. The descriptor has version/size guards but manager and application-contract structures do not. A release policy must identify supported toolchains, SDK/runtime combinations and deprecation rules.

## 8.9 Callback Contract

Manager callbacks run synchronously in the caller's context and, during mutating operations with configured locking, under the manager lock. They must not re-enter it; applications bound their latency. If `alloc_image` fails after returning partial pointers, PluginManager calls paired `free_image` with those values. Allocators must initialize outputs and safely free partial results. Future APIs should specify context, locking, blocking, re-entry, ownership and cancellation for each callback.

## 8.10 Testing and Verification

- **Repository CI:** portable host tests and Python SDK-builder tests.
- **Defined but not run in repository CI:** optional ARM fixture and Zephyr native-sim tests.
- **External evidence:** application-owned protocol replay and physical NUCLEO tests, requiring an externally versioned application/hardware record.

**Future:** sanitizer/fuzz builds, malformed corpus, generated-SDK compilation, concurrency stress, signature vectors, fault/MPU and power-loss tests, and hardware-in-loop execution.

## 8.11 Logging and Observability

The core has no generic logger dependency and reports status codes; this makes skipped scan candidates hard to diagnose. A future optional structured event callback should be application-owned, portable, safe before storage/logger initialization and avoid secret leakage.
