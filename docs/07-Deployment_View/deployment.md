# Deployment View

## 7.1 Development Deployment

![Development deployment](../diagram_as_code/out/07_deployment_view/development.svg)

Source: [`development.puml`](../diagram_as_code/07_deployment_view/development.puml).

The host static library and target plugin are distinct artifacts. A target plugin is not a Zephyr application: it has no vector table, reset handler or independent kernel runtime.

## 7.2 Future Production Deployment

Production needs separate staging/active locations, protected key and rollback state, atomic activation and known-good fallback, MPU-capable executable regions, optional secure-boot linkage, persistent audit records and a recovery mode that does not execute plugins.
