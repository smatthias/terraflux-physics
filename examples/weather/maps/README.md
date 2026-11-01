# Weather map examples

The radar fixtures mirror the input structure of the BOM Melbourne (Broadmeadows) 256 km product candidate (IDR012), without containing BOM imagery:

- `victoria-radar-synthetic.ppm` with `victoria-radar-synthetic.json`: 4 × 4 RGB PPM palette fixture and v1 sidecar.
- `victoria-radar-synthetic-indexed.png` with `victoria-radar-synthetic-indexed.json`: 4 × 4 indexed PNG fixture and v1 sidecar. It includes an explicitly transparent no-data pixel.
- [Map sidecar schema](../../../docs/weather/observation-schema.json) and [canonical observation schema](../../../docs/weather/observation-record-v1.schema.json).

The sidecars map raster colors to dBZ. Both fixtures use these row-by-row expected values:

| Row | Cells |
|---|---|
| 1 | 10, 20, 30, 40 dBZ |
| 2 | 20, 30, 40, 50 dBZ |
| 3 | 30, 40 dBZ, ambiguous, 50 dBZ |
| 4 | no-data, 10, 20, 30 dBZ |

In the PNG fixture, the row 4 first pixel has alpha 0 and maps to no-data through an explicit palette entry. The ambiguous pixel and no-data pixels have null values but distinct quality statuses. Do not infer rainfall accumulation from reflectivity or commit live BOM imagery until applicable source-use terms are cleared.
