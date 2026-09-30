# Architecture Diagrams

PlantUML sources live in the numbered directories here. Architecture pages
display the corresponding checked-in SVGs under `out/`. Edit the `.puml`
source and regenerate its SVG in the same change.

With the `plantuml/plantuml:latest` Docker image (or a local PlantUML CLI),
render each directory using the source directory as input and the matching
`out/` directory as output. For example, from the repository root:

```sh
docker run --rm -v "${PWD}/docs/diagram_as_code:/data" \
  plantuml/plantuml:latest -tsvg \
  -o /data/out/06_runtime_view /data/06_runtime_view/*.puml
```

Repeat for `03_context_and_scope`, `04_solution_strategy`,
`05_building_block_view`, and `07_deployment_view`. All diagrams are maintained
as text; SVG files are generated artifacts for Markdown viewers.
