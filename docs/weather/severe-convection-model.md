# Severe convection v1: temperature inversions, supercells and tornadoes

This contract diagnoses where tornadoes become possible. Two ingredients are represented: a **capping temperature inversion** that stores convective energy until it is released explosively, and **low-level rotation** (wind shear and storm-relative helicity) that turns released convection into rotating supercells. Implementation: `include/terraflux/weather/severe_convection.hpp`, `src/weather/severe_convection.cpp`, with the inversion in the [column model](atmospheric-column-model.md); schema: [severe-convection-v1.schema.json](severe-convection-v1.schema.json).

## The inversion: a loaded gun

A capping inversion is configured in the column (`column.inversion_base_m`, `inversion_depth_m`, `inversion_strength_k`, `lapse_rate_above_inversion_k_m`): through the layer the temperature *rises* by the strength, and above it the elevated mixed layer keeps the offset with a steep lapse rate (e.g. 8.5 K/km). The surface parcel must become warmer than the cap before it can rise freely, so:

- **ambient CIN** (the cap strength) grows with the inversion — a 2 K cap over a 28 °C, 14 g/kg surface raises CIN from 9 to ~180 J/kg while the steep lapse above stores ~2300 J/kg of CAPE (tested);
- **surface heating** erodes the CIN; when the heated parcel's CIN falls to the release trigger the cap is **broken** and the stored CAPE is released at once (4000 J/kg in the tested case) — the afternoon explosive development behind many severe outbreaks.

Per cell the block reports `cap_strength_j_kg` (ambient CIN), heated CAPE/CIN, and the flags `capped` (energy present but inhibited) and `cap_broken`.

## Rotation: shear and helicity from the single layer

- **0–1 km shear** `S1 = V_g − V_s`: the frictional surface wind turns toward low pressure while the geostrophic wind aloft blows along the isobars (the Ekman turning already in the coupled model). Strong low-level jets with strong friction give strong veering shear (a 31 m/s jet with `k = 6 × 10⁻⁵` gives ~17 m/s).
- **0–6 km shear** adds the thermal wind of the horizontal temperature gradient, `(g Δz/(f T)) k̂ × ∇T` (tested against the formula).
- **Storm motion** (Bunkers et al. 2000): mean wind plus 7.5 m/s to the right of the deep shear vector in the NH (left in the SH).
- **Storm-relative helicity** of the straight 0–1 km hodograph: `SRH = [S1 × (V_s − c)]_z`, positive for veering in the NH; the SH mirror gives negative values of equal magnitude (tested).

## Significant Tornado Parameter

Thompson et al. (2003), fixed-layer form, with our positive-valued CIN:

`STP = (CAPE/1500) · (2000 − LCL)/1000 · (|SRH|/150) · (S6/20) · (200 − CIN)/150`

with the published clamps: LCL term 1 below 1 km and 0 above 2 km; CIN term 1 below 50 J/kg and 0 above 200 J/kg; shear term 0 below 12.5 m/s and capped at 1.5 above 30 m/s; no STP without a condensation level. Every term at its reference value gives 1 (tested). Classes: none < 0.5, low < 1, moderate < 3, significant ≥ 3 (EF2+ environments). **Tornado possible** is flagged only where the class is at least moderate *and* a cumulonimbus exists, i.e. the convection has actually been released.

Two real-world limits fall out of the formula and are visible in the examples: hot afternoons lift the cloud base above 2 km and kill the potential (the "high LCL" limit), and weak flow without a low-level jet gives too little helicity even with a broken cap.

## Configuration

`severe` (requires `cloud_formation`): `deep_shear_depth_m` (6000), `bunkers_deviation_m_s` (7.5), `geostrophic_air_density_kg_m3` (1.2) and the class thresholds. An empty object selects the defaults.

## Output format

`coupled_atmosphere.severe`: config echo and per frame the cell arrays `low_level_shear_m_s`, `deep_shear_m_s`, `storm_motion_east/north_m_s`, `helicity_m2_s2`, `significant_tornado_parameter`, `heated_cape_j_kg`, `heated_cin_j_kg`, `cap_strength_j_kg`, `lcl_height_m` (null when none), `capped`, `cap_broken`, `tornado_risk`, `tornado_possible`, and the totals `capped_cells`, `cap_broken_cells`, `tornado_possible_cells`, `max_stp`. The viewer shades the STP class on the weather map, marks 🌪 where tornadoes are possible, and offers STP and helicity layers in the field explorer.

## Example and tests

`examples/tornado-outbreak-request.json` (checked by CTest, not committed as a fixture): 12 × 10 cells at 40° N, a 30 m/s low-level jet from a broad pressure gradient with strong friction, a warm moist sector (+4 K, +7 g/kg) under a 1 K cap with an 8.5 K/km elevated mixed layer, 4 K diurnal heating peaking at the 6 h frame. At night the domain is capped and quiet; in the afternoon the cap breaks in the warm sector, cumulonimbus form and the STP reaches the moderate class with tornadoes possible in several cells.

`weather.severe_convection` (C++): STP reference values and clamps; the inversion raising CIN and storing CAPE, heating eroding and breaking the cap; helicity sign and Bunkers deviation mirrored between hemispheres; deep shear equal to low-level shear plus the thermal wind; tornado potential requiring both strong flow and released convection; rejections. `weather.storm_systems_cli` (Python) runs the tornado example and checks the night/afternoon contrast.

## Scope limits

Single-layer proxies: the 0–1 km hodograph is the Ekman turning, the deep shear the thermal wind, storm motion Bunkers' rule; no radar-scale dynamics, no mesocyclone or tornadogenesis modelling, no outflow boundaries, no nocturnal low-level jet. The STP is a climatological discriminator of environments, not a tornado forecast. Keep each new process behind an explicit model-version change.
