# Glossary

| Term | Meaning |
| --- | --- |
| Application contract | Application UUID plus interfaces and supplied services. |
| Available plugin | Discovered valid-package metadata; not necessarily loaded or interface-compatible. |
| Capability | Application service offered to a plugin through a vtable. |
| Candidate suffix | Filename filter, normally `.pmp`; not identity or trust metadata. |
| Contract version | Application field not currently enforced for compatibility. |
| Entry adapter | Application-specific target Thumb address to callable function conversion. |
| ELF load profile | Runtime section-count/size limits and virtual addresses; not CPU/toolchain identity. |
| Instance | Active opaque object returned by plugin `create`. |
| Interface | Application-defined typed API identified by UUID and exact version. |
| Loaded plugin | Descriptor/image from PMP; unloadable after instance destruction. |
| Plugin ID | Application-scoped numeric identity. |
| PMP | PluginManager package: manifest, ELF image, image CRC and reserved signature range. |
| Registered plugin | Static or loaded descriptor accepted into the registry. |
| Runtime descriptor | Native descriptor containing resolved lifecycle callbacks. |
| Service | Application-owned UUID/version/vtable record. |
| Static plugin | Linked into application; registered but not unloadable. |
| Target profile | Future explicit CPU, ABI, linker, section and relocation compatibility identity. |
| W^X | Memory writable or executable, never both simultaneously. |
| Wire descriptor | Pointer-free serialized metadata in loaded rodata. |
