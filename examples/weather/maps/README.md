# Weather map examples

The first fixture is a small synthetic radar reflectivity map that mirrors the input structure of the BOM Melbourne (Broadmeadows) 256 km product candidate (IDR012), without containing BOM imagery:

- `victoria-radar-synthetic.ppm`: 4 × 4 RGB palette raster.
- `victoria-radar-synthetic.json`: v1 input sidecar with UTC time, raster metadata, affine georeferencing, units, and palette statuses.
- [Map sidecar schema](../../../docs/weather/observation-schema.json) and [canonical observation schema](../../../docs/weather/observation-record-v1.schema.json).

The C++ reader supports PPM P3 and PNG. The current fixture is PPM so palette pixels are easy to inspect in a text diff; additional PNG fixtures can use the same sidecar palette contract and optional alpha channel.

The sidecar maps colors to dBZ. Expected row-by-row interpretation:

| Row | Cells |
|---|---|
| 1 | 10, 20, 30, 40 dBZ |
| 2 | 20, 30, 40, 50 dBZ |
| 3 | 30, 40 dBZ, ambiguous, 50 dBZ |
| 4 | no-data, 10, 20, 30 dBZ |

Ambiguous and no-data pixels both have a null value in canonical observations, with distinct quality statuses and an explanatory reason for the ambiguous pixel. Do not infer rainfall accumulation from reflectivity or commit live BOM imagery until applicable source-use terms are cleared.
