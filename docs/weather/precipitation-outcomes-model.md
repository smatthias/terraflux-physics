# Precipitation outcomes v1: what each cloud formation delivers

This contract maps the diagnosed cloud genus of every cell to its weather at the ground: precipitation type, phase, intensity, duration, accumulation, lightning and hail. It closes the chain pressure → wind → temperature/humidity → column → clouds → outcomes. Implementation: `include/terraflux/weather/precipitation_outcomes.hpp`, `src/weather/precipitation_outcomes.cpp`; schema: [precipitation-outcomes-v1.schema.json](precipitation-outcomes-v1.schema.json).

## Genus → outcome mapping

| Genus | Character | Typical outcome | Lightning | Hail | Duration fraction |
| --- | --- | --- | --- | --- | --- |
| cirrus (Ci) | none | harmless ice crystals aloft; fair-weather indicator | no | no | 0 |
| cirrostratus (Cs) | none | halo; heralds a warm front, precipitation later | no | no | 0 |
| altocumulus (Ac) | none | none (virga at most) | no | no | 0 |
| cumulus (Cu) | none | fair weather | no | no | 0 |
| stratocumulus (Sc) | intermittent | at most light drizzle (rate × 0.3, capped at the drizzle cap) | no | no | 0.5 |
| stratus (St) | drizzle | continuous drizzle or snow grains, capped at `drizzle_max_rate_mm_h` (0.5 mm/h) | no | no | 1.0 |
| altostratus (As) | intermittent | light rain or snow; often virga over dry air | no | no | 0.5 |
| nimbostratus (Ns) | continuous | **prolonged** moderate rain or snow, hours long | no | no | 1.0 |
| towering cumulus (TCu) | showers | rain or snow showers, light to moderate | no | no | 0.2 |
| cumulonimbus (Cb) | showers | heavy to violent showers, **thunderstorm**, **hail** when the updraft allows | yes, if top ≤ −20 °C and the cloud spans 0 °C | sized by updraft, reduced by melting | 0.2 |

The genus and its base/top come from the [cloud formation model](cloud-formation-model.md).

## Rate

Cell-mean precipitation rate from condensation in lifted saturated air:

`R = σ · ε · ρ_base · w · (q_s(T_base, p_base) − q_s(T_top, p_top)) · 3600  [mm/h]`

- `w` — lifting through the cloud: layered cloud `w = depth / lifting_time_scale`; convection `w = updraft_parcel_factor · √(2 CAPE)` (parcel theory halved, because real updrafts are about half the parcel value).
- `σ` — updraft area fraction: 1 for layered cloud, `convective_updraft_fraction` (0.08) for convection, so the cell-mean rate of a storm is a fraction of its core rate.
- `ε` — precipitation efficiency: 0.7 stratiform, 0.4 convective.
- `T_base` — for layered cloud the environment temperature at the base; for convection the heated surface parcel lifted dry-adiabatically to the base. `T_top` follows the pseudo-adiabat from the base (same integration as the column model).
- `q_s` — saturation specific humidity (Magnus form). `ρ_base` from the moist ideal-gas law.

Reference magnitudes (tested): a 6 km deep nimbostratus over 6 h gives 2–8 mm/h (moderate, 30–50 mm in 8 h — a heavy frontal rain day); a 2500 J/kg cumulonimbus gives 35 m/s updraft, 30–80 mm/h cell-mean, 4–6 cm hail; towering cumulus 2–4 mm/h showers; stratus 0.5 mm/h drizzle.

Intensity classes follow WMO: light < 2.5, moderate < 10, heavy < 50, violent ≥ 50 mm/h. `accumulation = R × duration fraction × interval`, the interval defaulting to the snapshot spacing.

## Phase at the ground

The surface **wet-bulb temperature** `T_w` (psychrometric relation `e = e_s(T_w) − (c_p p / εL)(T − T_w)`, Newton-solved; 20 °C / 50 % → 13.75 °C) decides the phase because falling precipitation cools the air to `T_w`:

- `T_w ≤ 0.5 °C` → **snow** (solid) — dry 3 °C air can snow;
- `0.5 °C < T_w ≤ 2 °C` → **sleet** (mixed);
- else **rain**; a cloud entirely warmer than 0 °C (no ice) gives liquid in any case.

Labels: drizzle, rain, sleet, snow, rain/sleet/snow showers. The environment profile is monotonic, so freezing rain (warm layer aloft over sub-zero ground) cannot occur in v1.

## Lightning and hail

- **Lightning** needs a cumulonimbus whose top is at or below `lightning_top_temperature_k` (−20 °C) and whose depth spans the freezing level: the mixed-phase region where graupel and ice crystals separate charge.
- **Hail**: the largest stone the updraft can suspend has terminal velocity equal to the updraft, `v_t = √(4 g ρ_ice D / (3 C_d ρ_air))` with `ρ_ice = 900 kg/m³`, `C_d = 0.6`, `ρ_air = 1 kg/m³` (2 cm ↔ 19.8 m/s, 4 cm ↔ 28 m/s). It loses `hail_melting_cm_per_km` (0.3 cm/km) falling from the freezing level; stones still at least `hail_min_diameter_cm` (0.5 cm) are reported, capped at `hail_max_diameter_cm`.
- **Virga**: rates ≤ `virga_max_rate_mm_h` from a base ≥ `virga_min_base_m` into surface air drier than `virga_surface_rh` evaporate before reaching the ground (`virga` flag, rate 0).

Hazards per cell: `thunderstorm`, `hail`, `heavy_rain` (≥ 10 mm/h), `violent_rain` (≥ 50 mm/h), `prolonged_rain` (continuous with ≥ 10 mm in the interval), `snow`, `sleet`, `drizzle`, `virga`.

Also reported per cell: freezing level, surface wet-bulb temperature, precipitable water (column-integrated vapour, mm), cloud-base and cloud-top temperatures and the lifting used.

## Configuration (`precipitation`)

Requires `cloud_formation`. All keys optional: efficiencies, updraft fraction and parcel factor (each in (0, 1]), `interval_seconds` (default: snapshot interval), wet-bulb thresholds, lightning top temperature (< 0 °C), hail minimum/melting/maximum, drizzle cap, virga thresholds, minimum reported rate. Validation rejects values outside these ranges and a snow threshold above the sleet threshold.

## Output format

`coupled_atmosphere.precipitation` (`terraflux.weather.precipitation_outcomes/v1`): `config` echo and one frame per timeline frame with the cell arrays `precipitation_type`, `phase`, `character`, `intensity`, `rate_mm_h`, `duration_fraction`, `accumulation_mm`, `hail_max_diameter_cm`, `lightning`, `virga`, `updraft_m_s`, `freezing_level_m` (null when absent), `surface_wet_bulb_k`, `precipitable_water_mm`, `cloud_base_temperature_k`/`cloud_top_temperature_k` (null when clear), `hazards` (list per cell) and the totals `precipitating_cells`, `lightning_cells`, `hail_cells`, `snow_cells`, `max_rate_mm_h`, `total_accumulation_mm`. Consistency rules enforced by the viewer and the CTest check: intensity follows the rate; dry cells carry nothing; accumulation = rate × duration × interval; hail requires lightning; hazards match lightning and hail; totals match. `source_kind` is `engine_output` or `synthetic` (mock in the same format, labelled).

## Weather map view

The viewer's **Weather map** composes switchable layers over the loaded frames: base field (temperature, surface pressure or none), **isobars** contoured from the pressure field by marching squares at 1/2/4 hPa with labels on the major lines, fronts, cloud cover by genus, optional 3D cloud volumes, precipitation rate shading (light blue → blue → purple → red), precipitation symbols (● rain, ▽ showers, ✱ snow, ▲ sleet, ’ drizzle; white for snow), ⚡ thunderstorm and ◆ hail markers, and wind vectors. The legend follows the active layers; the inspector lists type, intensity, rate, duration, accumulation, phase, hazards, hail size, wet-bulb, freezing level, precipitable water and cloud temperatures; the metrics strip summarises precipitating area, rates, accumulation, thunderstorm/hail/snow counts and hazards. The field explorer also gets **Precipitation rate** and **Interval accumulation** layers. External map tiles are not rendered yet; the map is designed so that a tile layer can be added underneath.

## Tests

`weather.precipitation_outcomes` (C++): helper references (wet-bulb table value, hail terminal velocities, WMO classes, genus table); harmless genera deliver nothing; nimbostratus prolonged moderate rain with the rate formula re-evaluated independently; cumulonimbus heavy/violent showers with lightning, updraft-sized hail and melting, a weak storm with lightning but no hail, towering-cumulus showers; phase by wet-bulb (snow, sleet, rain, dry cold air snowing, warm stratus drizzling); virga over dry air; a pinned regression on the 12 × 10 baroclinic scenario at 6 K and 0 K heating (type/intensity counts, totals, selected cell values); configuration rejections. `weather.cloud_formation_cli` (Python) checks the block's format, recomputes wet-bulb and freezing level independently, and verifies afternoon-only thunderstorms with hail and prolonged nocturnal frontal rain in the fixture. Viewer tests validate the fixture and the mock, the styling helpers and the isobar contouring (straight lines for a linear field, closed rings around a low).

## Scope limits

Cell-mean rule-of-thumb outcomes: no microphysics, no precipitation drift or evaporation below cloud beyond the virga rule, no accumulation transport, no snowpack, no freezing rain (monotonic profile), fixed efficiencies and duration fractions, hail from parcel theory without supercell dynamics. Nothing feeds back on the air, and the horizontal model still has no surface fluxes, so storms are not resupplied. Keep each new process behind an explicit model-version change.
