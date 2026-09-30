# Solution Strategy

## 4.1 Current Strategy

1. **Separate mechanism from semantics.** Framework code handles identity, package, ELF, services and lifecycle. Applications define typed APIs.
2. **Share the registry.** Static and package-loaded descriptors follow the same registration and instance lifecycle.
3. **Cross-check two descriptors.** The package manifest supports discovery and pre-load checks; the pointer-free wire descriptor in ELF rodata contains runtime lifecycle addresses. Identities and ordered required-service UUIDs must agree.
4. **Constrain native loading.** Recognize a deliberately small section/relocation model rather than implementing a full dynamic linker. Validation is not yet a canonical CPU/ABI/section check.
5. **Inject cooperative capabilities.** The framework passes requested service records instead of application-specific hardware headers; native code is not isolated.
6. **Delegate ownership.** Applications own storage, memory, address conversion, locks, invocation and product policy.
7. **Generate reproducibly.** Python tooling normalizes versioned JSON, generates deterministic files and checks basic ELF32/little-endian/ARM/`ET_REL` headers before packaging.

## 4.2 Future Strategy

![Future solution strategy](../diagram_as_code/out/04_solution_strategy/future_strategy.svg)

Source: [`future_strategy.puml`](../diagram_as_code/04_solution_strategy/future_strategy.puml).

The future design retains the application boundary while introducing canonical signed manifests, trusted keys, anti-rollback counters, atomic activation, explicit CPU/ABI/relocation profiles, streaming validation, W^X memory, dedicated plugin stacks and deadlines, fault containment, versioned service requirements and optional host-only shared-library adapters. None of these are guarantees of PMP v1.
