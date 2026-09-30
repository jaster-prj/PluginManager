# Quality Requirements

## 10.1 Quality Tree

```text
Quality
  Safety: reject malformed images before execution; clean up failed operations
  Security [future]: authenticate packages; W^X and supervised execution
  Reliability: defined manager state transitions; rollback [future]
  Performance: bounded configured memory and storage access
  Portability: portable core and application adapters
  Maintainability: converge on authoritative ABI/package definitions
  Testability: host fakes, generated fixtures and hardware reference
```

## 10.2 Current Scenarios

| ID | Stimulus | Expected response |
| --- | --- | --- |
| Q-01 | Random, truncated or image-CRC-corrupt package. | Scan does not register/execute it; manager remains usable. |
| Q-02 | Wrong application or interface UUID/version. | Reject before plugin create (interface rejection happens after image loading). |
| Q-03 | Invalid validated ELF header, section, symbol bound or relocation. | Reject before plugin lifecycle execution. |
| Q-04 | Missing requested service. | Instance creation returns `PM_ENOENT` without an active instance. |
| Q-05 | Unload while an instance exists. | `PM_EBUSY`; image and instance remain. |
| Q-06 | Allocator or adaptation fails. | Release manager copies and allocator-owned regions. |
| Q-07 | File content changes after discovery. | Load rereads/reparses the **recorded byte count**; no fresh size query. |
| Q-08 | Mutating calls overlap with a suitable lock. | Serialize; callback re-entry is prohibited because callbacks run under the lock. |
| Q-09 | Instance destroyed and recreated. | Service table remains valid until successful destroy, then is released. |
| Q-10 | Section exceeds destination capacity. | Reject before copying that section; aggregate limits are checked after section writes. |

## 10.3 Future Scenarios

| ID | Stimulus | Target response |
| --- | --- | --- |
| FQ-01 | Image changed and CRC recomputed. | Signature rejection before execution. |
| FQ-02 | Valid signature but version below rollback floor. | Reject; keep known-good version. |
| FQ-03 | Power fails during installation. | Active package remains usable. |
| FQ-04 | Plugin writes outside data or executes writable memory. | Contained MPU fault; host remains operational. |
| FQ-05 | Plugin hangs. | Supervisor enforces deadline and records failure. |
| FQ-06 | Plugin exhausts stack. | Guard detects overflow without host corruption. |
| FQ-07 | Plugin requests disallowed service. | Reject before loading with structured reason. |
| FQ-08 | Package exceeds scratch budget. | Deterministic streaming rejection. |
| FQ-09 | Update races with invocation. | Stable handles prevent use-after-unload. |
| FQ-10 | SDK target profile differs from runtime. | Reject artifact before publication. |
