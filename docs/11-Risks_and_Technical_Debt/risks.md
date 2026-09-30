# Risks and Technical Debt

| Priority | Risk/debt | Consequence | Direction |
| --- | --- | --- | --- |
| Critical | No authenticity | Recomputed CRC allows modified native code. | Signed canonical package, keys and rollback. |
| Critical | Executable memory/isolation application-owned | Plugin can corrupt host; reference MPU disabled. | W^X provider, MPU/cache hooks, fault supervisor. |
| High | Stack and deadline unenforced | Overflow or indefinite blocking. | Dedicated supervised execution context. |
| High | Service versions/policy incomplete | ABI mismatch or excessive access. | Versioned requirements and enforcement. |
| High | Partial synchronization and raw pointers | Race with destroy/unload. | Single-owner model or synchronized stable handles. |
| High | Callbacks under manager lock | Re-entry/blocking can deadlock. | Explicit contract and safe external calls. |
| High | Non-canonical package format acceptance | Bounds-valid overlapping regions, enlarged headers, embedded NULs and trailing bytes complicate signing. | Canonical layout and strict size/string rules. |
| High | Incomplete target compatibility | No runtime Cortex-M subtype, ELF attribute, AAPCS or float-ABI check. | Shared builder/runtime target profiles. |
| Medium | Whole-package scan/load allocations | Heap pressure on small MCUs. | Streaming and caller-owned scratch. |
| Medium | Declared RAM not compared with data+BSS | Misleading limits and allocation. | Define and enforce semantics. |
| Medium | ABI/package definitions duplicated by integrations | Drift between package, descriptor and runtime. | Canonical generator and compatibility tests. |
| Medium | Public interface `create`/service policy unused | API promises differ from behavior. | Implement or remove in next ABI. |
| Medium | Kconfig maximum unused; discovery hard-coded | Misleading capacity settings. | Wire configuration or remove option. |
| Medium | Zephyr adapter re-enumerates directories | Quadratic scan cost. | Iterator/session or adapter cache. |
| Medium | Load trusts scanned file size | Changes are not freshly restated. | Fresh size/content identity on load. |
| Medium | Manager teardown ignores destroy failures | Services/image freed after failed cleanup. | Fallible close API. |
| Medium | Detach requires zero plugins including static | Static plugins block unrelated storage detach. | Track storage-dependent records. |
| Medium | No structured scan diagnostics | Corrupt/incompatible/unreadable indistinguishable. | Optional event callback. |
| Low | No exported installed CMake package | Awkward host consumption. | Export namespaced target and install test. |

## 11.1 Evolution Roadmap

1. **Contract correctness:** decide versions, enforce supported services, settle interface creation and threading.
2. **Parser/resource hardening:** canonical layout, overflow-safe ranges, pre-write aggregate limits, streaming and scratch.
3. **Artifacts:** canonical package/descriptor generation, target-profile metadata, broader CI and malformed corpus.
4. **Trust:** signatures, keys, anti-rollback, atomic activation.
5. **Execution safety:** W^X, MPU/cache hooks, dedicated stack, deadlines and fault containment.
6. **Operations:** structured diagnostics, audit, known-good rollback and recovery.
7. **Extensions:** other target profiles and optional host runtime after compatibility and trust policies stabilize.
