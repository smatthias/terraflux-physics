# Public weather interfaces

Place stable public C++ types and model interfaces here. Keep units, coordinate conventions, valid ranges, ownership, provenance, and uncertainty clear in each interface.

Use a distinct observation type for imported or interpreted data, separate from the model's evolving state. Map-source metadata must not leak into the core physical state.

Do not expose renderer, file-format, network, or user-interface types in the weather API. Add public declarations only after the corresponding physical or observation contract is agreed in `docs/weather/architecture.md`.
