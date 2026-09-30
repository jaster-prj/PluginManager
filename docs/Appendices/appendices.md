# Appendices

## A. Current Repository Map

```text
PluginManager/
  include/plugin_manager/   Public C API
  src/                      Runtime, registry, package, ELF and descriptor code
  cmake/                    Zephyr library definition and target linker script
  zephyr/                   Zephyr module metadata
  sdk-builder/              Python SDK generator and package writer
  tests/                    Host, ARM fixture and Zephyr tests
  docs/01-*/ to 12-*/       Authoritative arc42 chapters
  docs/implementation.md    Historical implementation plan and status
```

## B. Architecture Conformance Rules

1. PluginManager core does not depend on an application-specific contract.
2. Package and wire descriptor identities agree before registration.
3. Plugin lifecycle callbacks are not invoked before ELF, descriptor and entry validation.
4. PluginManager exposes application resources through declared service APIs; native memory isolation remains future work.
5. Image and service lifetimes cover each invocation.
6. Controlled loaded-plugin lifecycle destroys the instance successfully before unload/detach; unconditional manager teardown is a documented exception.
7. Every section write has a destination-capacity check; aggregate profile checks must move before writes in the future.
8. Future security controls strengthen rather than bypass application-owned policy.
