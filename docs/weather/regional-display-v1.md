# Synthetic regional weather display

**Regional weather demo** creates a separate analytic 1,200 × 900 km scene (48 × 36 cells, 25 km spacing) with 14 ten-minute snapshots. It contains moving low/high-pressure centers, a cold-front illustration, cloud bands, sea-level pressure, relative humidity and a synthetic circulation field. No local engine output is rescaled to claim regional weather dynamics.

The atmosphere-display extension on a timeline is `atmosphere`, with schema `terraflux.weather.atmosphere_display/v1`, source_kind `synthetic`, and arrays `cloud_fraction_by_frame`, `sea_level_pressure_hpa_by_frame`, `relative_humidity_pct_by_frame`, `systems_by_frame`. Numeric arrays match each forecast's grid and row-major order; cloud fraction is [0,1], pressure positive hPa, humidity [0,100]. System entries are low/high markers with normalized u/v coordinates or cold_front polylines containing normalized [u,v] points. u increases east and v north. All frame counts and positions are validated.

Three.js displays translucent schematic cloud puffs, pressure-center labels and a front line. The 2D fallback displays a cloud overlay, center labels and the front over the field grid. Cloud height is a visual convention, not a simulated cloud base. Overlay toggles preserve the underlying numerical field and cell inspection.

The regional fields are generated analytically in `viewer/src/weatherSystems.js`, not by atmospheric equations. Pressure, humidity and cloud fraction do not affect local transport, rainfall, infiltration or evaporation. These are demonstration fields, not observations or predictions.

Metrics include area, temperature min/mean/max, wind mean/max, mean cloud fraction, pressure range and mean humidity. Local engine data additionally show rainfall coverage, mean rainfall over covered cells only, wet area above 0.1 mm surface depth, maximum water depth and mean soil capacity usage over known cells. Every metric is recomputed for the selected snapshot. Missing values are excluded rather than interpreted as zero rainfall. The model's full water budget remains visible for water and soil layers.
