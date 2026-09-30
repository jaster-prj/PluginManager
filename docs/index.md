# PluginManager Architecture

This documentation follows [arc42](https://arc42.org/). **Current** describes
behavior implemented in PluginManager; **Future** describes target architecture
and outstanding needs, not guarantees of the current implementation.

The EF800 NUCLEO-G0B1RE measurement host is an externally maintained reference
deployment. Its application-specific measurement, UART, storage and UI behavior
does not belong to the PluginManager framework.

## Architecture

1. [Introduction and goals](./01-Introduction_and_Goals/introduction.md)
2. [Constraints](./02-Constraints/constraints.md)
3. [Context and scope](./03-Context_and_Scope/context.md)
4. [Solution strategy](./04-Solution_Strategy/solutions.md)
5. [Building block view](./05-Building_Block_View/blocks.md)
6. [Runtime view](./06-Runtime_View/runtime.md)
7. [Deployment view](./07-Deployment_View/deployment.md)
8. [Crosscutting concepts](./08-Crosscutting_Concepts/crosscutting.md)
9. [Architectural decisions](./09-Architectural_Decisions/index.md)
10. [Quality requirements](./10-Quality_Requirements/quality.md)
11. [Risks and technical debt](./11-Risks_and_Technical_Debt/risks.md)
12. [Glossary](./12-Glossary/glossary.md)

[Appendices](./Appendices/appendices.md) include repository structure and
architecture conformance rules.

Architecture diagrams are stored as [PlantUML sources and rendered SVGs](./diagram_as_code/README.md).

## Other Documentation

- [Implementation history](./implementation.md)
- [SDK builder](../sdk-builder/README.md)
- [Project README](../README.md)
