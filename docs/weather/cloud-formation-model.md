# Cloud formation v1: fronts, convection and their combination

This contract diagnoses cloud formation on top of the coupled horizontal layer and the vertical column model. Two lifting mechanisms are represented, separately and combined:

1. **Frontal lifting** — warm, humid air meeting colder air horizontally. The collision zone is a *front*; lifting along it produces layered cloud, and the direction in which the air masses move decides whether it is a cold or a warm front.
2. **Convective lifting** — air heated at the ground rises, cools dry-adiabatically, condenses at its lifting condensation level and, if it reaches its level of free convection, keeps rising: cumulus → towering cumulus → cumulonimbus ("over-development", summer thunderstorms).
3. **Combined** — where frontal lifting carries a parcel with convective energy above its level of free convection, convection is released inside the frontal band (frontal / squall-line thunderstorms), or free convection occurs inside the band.

Implementation: `include/terraflux/weather/cloud_formation.hpp`, `src/weather/cloud_formation.cpp`. It reads the coupled state (pressure, temperature, humidity, wind) and uses the [atmospheric column model](atmospheric-column-model.md) for the parcel ascent. Everything is a rule-of-thumb diagnostic: there is no microphysics, precipitation, radiation or feedback on the flow.

## Frontal analysis (`analyse_fronts`)

Per cell, with central differences (one-sided at the edges):

- `|∇T|` — a cell is a **frontal zone** when `|∇T| ≥ frontal_gradient_threshold_k_m` (default 2 K per 100 km).
- temperature advection `A = −V·∇T` — in a frontal zone, `A ≤ −advection_threshold` (cold air arriving) marks a **cold front**, `A ≥ +threshold` (warm air arriving) a **warm front**, otherwise **stationary** (default threshold 0.1 K/h).
- divergence `∇·V` and **lifting** `w = max(0, −H ∇·V)` with `H = boundary_layer_depth_m`: boundary-layer convergence pushes air upward (mass continuity).
- for every cell, the distance to the nearest frontal cell and its **side** (+1 warm, −1 cold, 0 on the front), by comparing the cell temperature with the nearest frontal cell.

The example flow is a Southern Hemisphere low: clockwise circulation carries the cold pool northward on its western flank (cold front) and the warm sector southward on its eastern flank (warm front).

## Cloud diagnosis (`diagnose_cloud_formation`)

Column diagnostics are computed twice per cell: for the ambient surface parcel (frontal lifting of the air that is there) and for the **heated** parcel (`T_surface + surface_heating_k`, environment aloft unchanged — the convective-temperature idea). Then, in order of precedence:

1. **Convection released** when the heated parcel has `CAPE ≥ cape_threshold_j_kg` and either `CIN ≤ cin_trigger_j_kg` (trigger `free`) or the lifting depth `w × lifting_time_scale_s` carries it to its LFC (trigger `frontal` inside a frontal band, `convergence` elsewhere). Base = heated LCL, top = equilibrium level. Mechanism `convective`, or `frontal_convective` on/near a front.
2. **Frontal layered cloud** otherwise: on the front or on the warm side within `frontal_band_width_m`, lifted air clouds from the ambient LCL up to the lifted height (trigger `frontal`). On the **cold side**, warm air **overruns** the frontal wedge: base = max(wedge height, frontal LCL) with wedge height = distance × slope (`warm_front_slope` 1:200, `cold_front_slope` 1:75), top = wedge + lifting of the nearest frontal cell, within `warm_front_reach_m` / `cold_front_reach_m` (trigger `overrunning`). With distance ahead of a warm front the base rises: nimbostratus → altostratus → cirrostratus.
3. **Fair-weather cumulus** when nothing else applies and the heated mixed layer (`H + mixed_layer_growth_m_per_k × heating`) reaches the heated LCL (trigger `thermals`).

`classify_cloud(base, top, convective)` assigns the genus: convective — cumulonimbus (depth ≥ 6 km or top ≥ 8 km), towering cumulus (depth ≥ 2 km), cumulus; layered — base < 2 km: nimbostratus (depth ≥ 3 km), stratus (≥ 0.5 km), stratocumulus; base 2–6 km: altostratus (depth ≥ 2 km) or altocumulus; base ≥ 6 km: cirrostratus (depth ≥ 1.5 km) or cirrus. Cloud fraction is a fixed value per genus (Ns 1.0, St/As 0.9, Cb 0.9, Cs 0.7, Sc 0.6, TCu/Ac 0.5, Cu/Ci 0.3). `thunderstorm` is true exactly for cumulonimbus.

**Diurnal heating**: `heating(t) = amplitude × max(0, sin(2π (t − phase) / 86400))`; zero at night. The CLI evaluates it per frame; the kernel takes the value directly.

## Configuration

Request section `cloud_formation` (requires `column`); all values optional with the defaults below.

| Key | Default | Meaning |
| --- | --- | --- |
| `frontal_gradient_threshold_k_m` | 2 × 10⁻⁵ | frontal zone threshold (2 K / 100 km) |
| `advection_threshold_k_s` | 2.78 × 10⁻⁵ | cold/warm classification (0.1 K/h) |
| `boundary_layer_depth_m` | 1500 | `H` in `w = −H ∇·V`; base of the mixed layer |
| `lifting_time_scale_s` | 21 600 | lifting depth = `w` × time scale |
| `mixed_layer_growth_m_per_k` | 300 | thermal mixed-layer deepening per kelvin of heating |
| `warm_front_slope` / `cold_front_slope` | 1/200, 1/75 | frontal surface slopes |
| `warm_front_reach_m` / `cold_front_reach_m` | 800 km, 200 km | overrunning cloud reach on the cold side |
| `frontal_band_width_m` | 150 km | warm-side band where lifting counts as frontal |
| `cin_trigger_j_kg` / `cape_threshold_j_kg` | 20, 100 | free-convection release and minimum CAPE |
| `max_cloud_top_m` | 12 000 | cloud-top cap |
| `surface_heating.amplitude_k`, `.phase_seconds` | — | diurnal heating of the surface parcel |

Validation rejects non-positive thresholds, depths, scales and reaches, a warm-front slope steeper than the cold-front slope, negative heating, and everything the column configuration rejects.

## Output format (documented; engine or mock)

The CLI writes the `clouds` block into `coupled_atmosphere` of the timeline; the schema is [cloud-formation-v1.schema.json](cloud-formation-v1.schema.json). The same format is accepted from hand-built mocks when `source_kind` is `synthetic`; the viewer labels them as such and never mixes them with engine output.

```json
{
  "schema_version": "terraflux.weather.cloud_formation/v1",
  "method": "frontal_convergence_overrunning_heated_parcel_convection",
  "source_kind": "engine_output | synthetic",
  "config": { "...all keys above...", "surface_heating": { "amplitude_k": 6, "phase_seconds": 7200 } },
  "frames": [ { "elapsed_seconds": 0, "surface_heating_k": 0, "...cell arrays...", "cloudy_cells": 0, "thunderstorm_cells": 0, "front_cells": 0 } ]
}
```

Per frame (one entry per timeline frame, `elapsed_seconds` equal to the frame's), every cell array is row-major `nx × ny`:

| Field | Type | Meaning |
| --- | --- | --- |
| `cloud_type` | enum | `none`, `cumulus`, `towering_cumulus`, `cumulonimbus`, `stratocumulus`, `stratus`, `nimbostratus`, `altocumulus`, `altostratus`, `cirrus`, `cirrostratus` |
| `mechanism` | enum | `none`, `convective`, `frontal`, `frontal_convective` |
| `trigger` | enum | `none`, `free`, `thermals`, `frontal`, `convergence`, `overrunning` |
| `base_m`, `top_m` | number or null | cloud base/top above the surface; null when clear; `base < top` otherwise |
| `cloud_fraction` | 0–1 | fixed value per genus |
| `thunderstorm` | boolean | true exactly for cumulonimbus |
| `lifting_depth_m` | ≥ 0 | `max(0, −H ∇·V) × lifting_time_scale` |
| `heated_cape_j_kg`, `heated_cin_j_kg` | ≥ 0 | heated surface parcel |
| `front_type` | enum | `none`, `cold`, `warm`, `stationary` |
| `temperature_gradient_k_m` | ≥ 0 | `|∇T|` |
| `temperature_advection_k_s` | number | `−V·∇T` (negative = cold advection) |
| `divergence_s` | number | `∂u/∂x + ∂v/∂y` (negative = convergence) |
| `distance_to_front_m` | number or null | nearest frontal cell; null when no front exists |
| `side` | −1, 0, 1 | cold side, on front / none, warm side |
| `cloudy_cells`, `thunderstorm_cells`, `front_cells` | integers | totals, which must match the arrays |

Consistency rules enforced by the viewer validator and the CTest check: clear cells have `none` mechanism/trigger, null levels and zero fraction; cloudy cells have `base < top ≤ max_cloud_top_m`; `thunderstorm ⇔ cumulonimbus`; frontal cells have `side = 0`; totals match; the frame count and times match the timeline.

## Viewer

**Cloud formation** is a categorical layer: cell colour is the genus (legend with WMO abbreviations), ground height is the cloud fraction, translucent volumes span base → top (deep convection darker and narrower, a yellow marker on thunderstorm tops), and a ground overlay colours frontal cells (blue cold, red warm, purple stationary). The 2D fallback draws the same overlay and ⚡ markers. The inspector lists genus, base/top, mechanism, trigger, front analysis (`|∇T|`, advection, divergence, distance and side) and heated CAPE/CIN; the metrics strip shows cover, thunderstorm and front counts, dominant genus, mean base/top and the current heating. **Load cloud formation example** opens the engine fixture; **Cloud formation mock (synthetic)** builds a labelled mock (moving warm and cold fronts, diurnal convection) in the same format. The request editor has the full cloud configuration, the diurnal heating, and free *air masses* (warm sector / cold pool anomalies not tied to pressure centres).

## Example and tests

`examples/cloud-formation-request.json`: 20 × 15 cells at 50 km, 48 h in 300 s steps with 8-hourly snapshots, a 1021 hPa high west of a 1003 hPa low at 37.8° S, a warm moist sector (+5 K, +4 g/kg) north-east of the low and a cold dry pool (−8 K, −4 g/kg) south-west, 6 K diurnal heating peaking at 8 h and 32 h. Fixture: `viewer/public/clouds/engine-timeline.json`. At t = 0 the domain is clear (no circulation, no heating); in the first afternoon widespread convection with frontal thunderstorms; at night frontal stratus, stratocumulus, nimbostratus and altocumulus along the cold and warm fronts; a second afternoon of over-developed towering cumulus without storms, because nothing resupplies surface heat and moisture (documented limit).

`weather.cloud_formation` (C++): classification rules; diurnal heating; cold/warm/stationary fronts from gradient and advection with sides and distances; lifting from convergence; heating driving none → cumulus → towering cumulus → cumulonimbus with CAPE rising and CIN falling; frontal lifting releasing inhibited convection (combined case); the warm-front overrunning sequence rising with distance; a pinned **regression scenario** (12 × 10 baroclinic case through the coupled model, diagnosed at 6 K and 0 K heating: front counts, cloud/mechanism/trigger counts and selected cell values); and configuration rejections. `weather.cloud_formation_cli` (Python): CLI output equals the committed fixture to 10⁻⁹, format rules above, night-time cloud only frontal, daytime thunderstorms, both fronts present, and the combined mechanism occurring. Viewer tests validate the fixture and the mock under the same validator and reject malformed blocks.

## Scope limits

Cloud genus, fraction and fronts are rule-of-thumb diagnostics on a coarse single-layer state: no cloud microphysics, precipitation, latent-heat feedback, radiation, vertical wind shear, frontogenesis dynamics, occlusions or front propagation beyond what the coupled transport produces. Frontal surfaces are straight wedges; lifting is a boundary-layer convergence estimate over a fixed time scale. Surface heating is a prescribed diurnal amplitude, not a surface energy balance, and nothing resupplies the warm sector, so convective potential decays with time. Keep each new process behind an explicit model-version change.
