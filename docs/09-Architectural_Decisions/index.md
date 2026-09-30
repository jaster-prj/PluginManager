# Architectural Decisions

These IDs summarize decisions embodied by the implementation; separate ADR files do not yet exist.

| ID | Decision | Rationale | Consequence |
| --- | --- | --- | --- |
| ADR-001 | Application-neutral core | Reuse without hardware coupling. | Each application publishes its own contract. |
| ADR-002 | C ABI and UUID identities | Embedded/toolchain compatibility. | No C++ or Zephyr-private public contract types. |
| ADR-003 | PMP wrapper around ELF | Separate discovery metadata from executable image. | Package/wire identities are cross-checked. |
| ADR-004 | Narrow ELF/relocation support | Reduce loader complexity. | Specialized build; ordinary objects may fail. |
| ADR-005 | Application service vtables | Decoupling and cooperative capability access. | Service lifetime and version policy matter; no native isolation. |
| ADR-006 | Application-owned storage/memory | Product-specific media and MPU policy. | Integrator owns storage and executable-memory safety. |
| ADR-007 | Shared static/loaded registry | Common lifecycle baseline. | Static plugins consume capacity and cannot unload. |
| ADR-008 | One active instance per plugin | Simplified state/ownership. | Multiple contexts require future runtime work. |
| ADR-009 | Logger-independent core | Host and target portability. | Limited scan diagnostics. |
| ADR-010 | Trusted development packages | Enable proof of concept. | Untrusted field installation is unsupported. |

## 9.1 Decisions Required for the Future

Define service/interface compatibility, canonical signed bytes and algorithms, root-key/revocation/rollback ownership, streaming scratch API, target-profile negotiation, W^X and cache hooks, supervised execution/cancellation, stable handle/threading model, atomic installation and host shared-library support policy. Record future irreversible decisions in `docs/adr/` before implementation.
