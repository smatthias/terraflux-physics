# Ocean carbon v1 — CO₂ in the air and the sea, acidification, oxygen and dead zones

This extension of the ocean model answers four questions:

- **How much CO₂ is in the air** over land and sea, and where do cities and industry raise it?
- **How much CO₂ does the ocean take up** (or give back), and how much more acid does it become?
- **Where does the sea run out of oxygen?** Where are the dead zones, and how do they change through the year?
- **Where can sea life live?** Which water is habitable, stressed, hypoxic or anoxic?

It runs inside the ocean CLI (`terraflux-run-ocean-circulation`) on top of the layered ocean ([ocean-circulation-model.md](ocean-circulation-model.md)): the currents, temperatures, salinities, mixed layer, wind and sunshine of that model drive the chemistry.

**Code and files**
- Kernels:
  - `include/terraflux/weather/ocean_carbon.hpp`: carbonate chemistry, gas exchange, metabolic index.
  - `ocean_biogeochemistry.hpp`: the sea's carbon, alkalinity, oxygen and nitrate on depth levels, habitat classes.
  - `atmospheric_co2.hpp`: the CO₂ of the boundary-layer air.
- CLI: `terraflux-run-ocean-circulation` with the request sections `biogeochemistry` and `atmosphere_co2` (always together).
- Request: `terraflux.ocean.circulation-run/v1` ([schema](ocean-run-v1.schema.json)). Output: the ocean timeline with a `carbon` block per frame and at the top level ([schema](ocean-circulation-v1.schema.json)).
- Examples:
  - [examples/ocean-carbon-request.json](../../examples/ocean-carbon-request.json) (today) → `viewer/public/ocean-carbon/engine-timeline.json`.
  - [examples/ocean-carbon-2100-request.json](../../examples/ocean-carbon-2100-request.json) (2100, high emissions) → `viewer/public/ocean-carbon-2100/engine-timeline.json`.
  - Both are written by [tools/make_ocean_carbon_examples.py](../../tools/make_ocean_carbon_examples.py).
- Viewer: **🫧 Load ocean carbon & dead zones (today)** or **(2100)** and the view **Carbon & ocean life** ([viewer/src/oceanCarbon.js](../../viewer/src/oceanCarbon.js)).

## 1. The air: CO₂ of the boundary layer

The CO₂ mixing ratio X (ppm) of the air near the ground, over land and sea, on the ocean grid:

  dX/dt = −u·∇X + K ∇²X + 10⁶ (E − U_land − F_ocean) / n − (X − X_bg) / τ

- **Background X_bg** — the global concentration of a scenario, `background_ppm` as `[year, ppm]` points, interpolated in time. It enters at the inflow boundaries, and the boundary layer relaxes to it by exchange with the free troposphere (`ventilation_hours`, default 48 h).
- **Air column** — n = ρ_air H / M_air moles per m², for a boundary layer H (`boundary_layer_height_m`, default 1000 m).
- **Wind and diffusion** — the ocean run's 10 m wind (upwind advection) and a horizontal diffusivity (`horizontal_diffusivity_m2_s`, default 5000 m²/s).
- **Sources E** — cities, industry and ports (`sources`: position, Gaussian radius, Mt CO₂/yr). Each source is spread over the cells and normalised, so the map adds up exactly to the source.
- **Vegetation U_land** — net ecosystem exchange on land: photosynthesis in the light minus respiration, U = α · 0.43 · SW − R.
  - α is `photosynthesis_umol_per_j` (default 0.02 µmol CO₂ per J of photosynthetically active radiation).
  - R is `respiration_umol_m2_s` (default 1 µmol m⁻² s⁻¹).
  - Plants draw the air down in daylight and release CO₂ at night.
- **Ocean F_ocean** — the sea's uptake from the ocean model (below). It is negative when the sea outgasses.

**Budget.** The moles above the background in the region change by:

  emitted − land uptake − ocean uptake − ventilation + transport (through the boundary) + background shift

The background shift is the change of the reference itself when the background rises. The CLI reports every term in Mt CO₂, cumulative since the end of the spin-up.

## 2. The sea's carbonate chemistry

From dissolved inorganic carbon (DIC) and total alkalinity (TA), on the total pH scale:

  TA = [HCO₃⁻] + 2[CO₃²⁻] + [B(OH)₄⁻] + [OH⁻] − [H⁺]

- **Solution** — solved for [H⁺] by Newton iterations with a bisection safeguard.
- **Constants** — the usual CO2SYS choices:
  - K₀: Weiss (1974).
  - K₁, K₂: Lueker et al. (2000).
  - K_B: Dickson (1990); total boron: Uppström (1974).
  - K_W: Millero (1995).
  - Aragonite and calcite solubility: Mucci (1983); calcium: Riley & Tongudai (1967).
- **Omitted** — phosphate, silicate, sulfate and fluoride. So is the fugacity correction (about 0.3 %).
- **Outputs** — pH, the species, pCO₂ = [CO₂*]/K₀ and the saturation states Ω = [Ca²⁺][CO₃²⁻]/K_sp. Below Ω_aragonite = 1 the water is **corrosive**: shells and corals of aragonite dissolve.

The unit test checks this chemistry against published values and known behaviour:

| Check | Result |
|---|---|
| pK₁, pK₂, ln K₀, ln K_B, ln K_W, K_sp at 25 °C, S 35 | Within 0.002–0.005 of the published values |
| Solution | Reproduces the alkalinity to 10⁻⁶ µmol/kg |
| Doubling CO₂ from 280 to 560 ppm | pH falls by 0.25–0.35; aragonite saturation by about a third |
| Revelle factor of surface water | Near 10, and it rises as the sea takes up CO₂ (the buffer weakens) |
| Warming at constant DIC and TA | pCO₂ rises by about 4 %/K (Takahashi et al. 1993) |

## 3. Air–sea exchange

Gas transfer (Wanninkhof 2014): k = 0.251 U₁₀² (Sc/660)^−½ cm/h, with the seawater Schmidt numbers of CO₂ and O₂.

- **CO₂** — F = k K₀ (pCO₂_air − pCO₂_sea), into the sea when positive. The air's pCO₂ is X × surface pressure × 0.98 (the water vapour of moist air).
- **O₂** — relaxes the surface level to its saturation (Garcia & Gordon 1992) at the rate k/Δz.
- **Sea ice** — only the open-water fraction exchanges gas.
- **Time step** — the uptake is limited to 90 % of the way to equilibrium in one step, so a shallow surface level cannot overshoot.

## 4. The water column and its transport

Four tracers in µmol/kg — DIC, TA, O₂ and NO₃ — are kept on fixed depth levels (`levels_m`, the level bottoms; the example uses 12 levels down to 600 m).

**Columns**
- **Shelf columns** end at the seafloor. If the seafloor leaves a level less than a quarter of its thickness (or less than 1 m), that sliver joins the level above. This keeps a thin bottom level from collecting the whole seafloor respiration.
- **Open columns** stop at the deepest level. Below it lies the unresolved deep ocean.

**Initial state**
- **Profiles** — DIC, TA, O₂ and NO₃ follow `profiles`: value(z) = deep + (surface − deep) e^(−z/scale).
- **Surface DIC** — shifted into equilibrium with the air (`surface_dic_from_atmosphere`).
- **Surface O₂** — shifted to saturation. Both offsets fade with depth.

### Transport (flux form, conservative)

**Horizontal fluxes.** Each face between two sea cells exchanges, level by level:
- an **upwind volume flux** with the face current. The current is that of the ocean's active layer: it moves only the part of a level that lies inside the active layer and above both seafloors. Below the active layer, the 1.5-layer ocean's deep water is at rest.
- a **diffusive flux** over the thickness the two levels share.

**Projection.** A depth-uniform current is not free of divergence over a seafloor whose depth changes along the flow. It would pile up or empty columns. So before the tracers move, the depth-integrated face transports are made consistent with the column geometry by two potential-flow corrections, each a graph-Laplacian equation solved by conjugate gradients:

1. The current is corrected so that no interior column gains or loses water.
2. The river water flows from each mouth towards the open boundary, in the surface mixed layer (the plume).

**Open boundary.** The ocean's sponge cells are the open boundary:
- they take up whatever the basin exports, at the water's own concentration;
- they supply inflow with the reference (open-ocean) water;
- below their mixed layer, they relax to the reference profiles on the sponge time scale.

In a closed basin, the river water displaces the same volume everywhere.

**Inside a column.** What remains level by level is closed by vertical fluxes between the levels: flow over a sloping seafloor moves water up and down.

**Properties.** The scheme conserves every tracer. Each budget is change = rivers + open boundary + sources and sinks. The remaining transport residual is the solver tolerance: below 10⁻⁹ of the inventory (about 10⁻¹⁰ Mt CO₂ in the example). Uniform water stays uniform.

### Mixing and ventilation
- **Mixed layer** — levels above the ocean model's mixed-layer depth are homogenised.
- **Below it** — implicit vertical diffusion (`vertical_diffusivity_m2_s`, default 10⁻⁵ m²/s).
- **Ventilation** — below `ventilation_depth_m` (default 150 m) the water relaxes to the reference profiles over `ventilation_time_years` (default 10). This is the renewal by the large-scale circulation that the regional column does not resolve.

### Rivers
River water enters the surface level of its mouth with its own chemistry (`forcing.rivers[].chemistry`: nitrate, DIC, TA, O₂ saturation; default `biogeochemistry.river_chemistry`). Its volume flows on in the plume layer.

## 5. Life in the water: production, sinking and respiration

**Production.** Net community production in the sunlit levels:

  P = e^(0.0633 (T − 20 °C)) · I/(I + I_k) · min(μ_max NO₃, P_max NO₃/(NO₃ + K_N))

- I = PAR e^(−k_d z), with PAR = 0.43 SW through the open water.
- Production is first order in scarce nitrate (μ_max, `max_growth_per_day`, 0.6/day) and capped in eutrophic water (P_max, `max_production_umol_n_kg_day`).
- The temperature factor is Eppley's (1972).
- It takes up nitrate and carbon at the Redfield ratio (C:N 117:16), plus CaCO₃ at the rain ratio (0.07).
- It releases O₂ (O₂:N 170:16) and raises TA by 1 per nitrate, lowered by 2 per CaCO₃.

**Export and respiration.** The organic matter sinks down the Martin curve F(z) = F(z₀)(z/z₀)^−0.858 and is respired on the way, consuming O₂ and releasing carbon and nitrate.
- **On the shelf**, whatever reaches the seafloor is respired in the bottom water (benthic demand), and the CaCO₃ dissolves there. With summer stratification this drains the bottom water of oxygen, as off river deltas.
- **In deep water**, the rest leaves the open column (export).
- **When the oxygen runs out**, nitrate is used (denitrification, 0.8 NO₃ per O₂). Beyond that, the demand is recorded as sulfidic (hydrogen sulfide) and O₂ stays at zero.

## 6. Habitat and dead zones

Each level gets a habitat class. Each column takes the class of its worst level.

| Class | Criterion | Meaning |
|---|---|---|
| 0 habitable | none of the below | Fish and shellfish can live here |
| 1 stressed | Ω_aragonite < 1 (`corrosive_omega`), or metabolic index Φ < 3 | Shells dissolve, or the warm water's oxygen demand outruns the supply |
| 2 hypoxic | O₂ < 60 µmol/kg (`hypoxia_umol_kg`, about 2 mg/L) | Fish and shrimp leave or die |
| 3 anoxic | O₂ < 5 µmol/kg (`anoxia_umol_kg`) | Only microbes; nitrate and then sulfate are respired |

- **Metabolic index** (Deutsch et al. 2015): Φ = pO₂ / pO₂,crit(T), with pO₂,crit(T) = 2 kPa × exp(−E₀/k_B (1/T − 1/288 K)) and E₀ = 0.4 eV. Warm water holds less oxygen and raises the oxygen an animal needs.
- **Dead zone** — a cell whose bottom water is hypoxic, or whose column is hypoxic over at least 20 % of its depth (`dead_zone_column_fraction`).
- **Reported per cell:**
  - habitable share of the column, hypoxic thickness;
  - bottom and lowest O₂ (with the depth of the minimum);
  - surface and bottom Ω, surface pH and pCO₂.
- **Reported in total:**
  - sea, dead-zone, anoxic and corrosive areas;
  - the hypoxic and habitable volume;
  - the mean surface pH and Ω.

## 7. In the ocean CLI

**Requirements.** `biogeochemistry` and `atmosphere_co2` go together. They need `physics.thermal_layers` and `forcing.air_sea`.

**Coupling.** Every `step_every` ocean steps (also during the spin-up):
- the air advances with the 10 m wind, the sources, the land exchange from the shortwave, and the ocean's uptake of the previous step;
- then the sea advances with:
  - the layer temperatures and salinities interpolated onto its levels (the deep water below the active layer);
  - the mixed-layer depth, the current and the active-layer thickness;
  - the wind, the shortwave and the sea ice;
  - the air's pCO₂;
  - the rivers with their chemistry.

The budgets count from the end of the spin-up.

**Output per frame** (`carbon`):
- **`atmosphere`** — `co2_ppm` (land and sea), background, maximum, means over sea and land, and the cumulative budget terms in Mt CO₂.
- **`ocean`** — maps:
  - CO₂ flux;
  - surface pCO₂, pH and Ω; bottom Ω;
  - bottom O₂, lowest O₂ and its depth;
  - habitable share;
  - production;
  - surface nitrate;
  - column class;
  - the dead-zone cells.

  Also per level: `level_oxygen_umol_kg`, `level_ph`, `level_omega_aragonite` and `level_class`; the `totals`; and the cumulative `budget` in mol.
- **Flux and production** are means over the interval since the previous frame (`averaging_hours`). Both follow the sun.

**Budget identities** (checked by the CLI test and the viewer):

- carbon change = air–sea + rivers − export + ventilation + open boundary + transport residual
- nitrogen change = rivers − export + ventilation + open boundary − denitrified + transport residual

**Top-level `carbon`** echoes the configuration, with:
- levels, profiles, biology, transport and habitat thresholds;
- the background scenario, the sources with their peak cells, and the emission map;
- the seafloor depths;
- the open boundary and the river chemistry;
- the initial inventories.

## 8. The example: a Gulf-like sea today and in 2100

**The sea**
- A semi-enclosed sea of 30 × 22 cells of 36 km.
- A northern coast with a river delta; the Great Delta River carries 16,000 m³/s with 110 µmol/kg nitrate, about 2,000 t N/day.
- A western coast with a smaller river.
- An eastern peninsula.
- The open ocean to the south (a sponge).
- The shelf deepens from 6 m at the coast to 100 m over 160 km, then down the slope to 3000 m.

**The air**
- Four coastal sources emit 180 Mt CO₂/yr: a refinery belt of 120 Mt, two cities and a port.

**The run**
- One year after a one-year spin-up, with monthly frames.
- Today: about 427 ppm.
- The 2100 slice: about 1,010 ppm (high emissions), the sea 3 K warmer, and the deep DIC 40 µmol/kg higher (anthropogenic carbon in the thermocline).

| Whole year | Today (2025) | 2100 |
|---|---|---|
| Background CO₂ | 427 ppm | 1,012 ppm |
| Mean surface pH (lowest month) | 8.03 (8.00) | 7.74 (7.72) |
| Mean surface aragonite Ω | 3.7 | 2.4 |
| Ocean uptake (mean) | +1 Mt CO₂/yr (a sink in winter and spring, a source in summer) | +24 Mt CO₂/yr (a sink all year) |
| Largest dead zone (month) | 50,500 km² (August) | 57,000 km² (June) |
| Largest anoxic area | 5,200 km² | 13,000 km² |
| Largest corrosive area (Ω < 1) | 0 | 6,500 km² (bottom water under the dead zones) |
| Nitrogen lost to denitrification | 61 kt | 165 kt |

**What happens**

- **The dead zones are seasonal.**
  - None in winter, when the mixed layer reaches the seafloor.
  - From April they grow as the shelf warms and stratifies.
  - In summer the plankton fed by the river nitrate sinks and is respired at the seafloor, where no new oxygen arrives.
  - In autumn cooling and storms mix the water again.
- **Where they form.**
  - The largest lies downstream of the delta: the coastal current carries the plume east into the bight between the coast and the peninsula.
  - A second lies on the western shelf, where the boundary current brings nutrient-rich water from the open boundary.
- **Acidification.** Warm water holds less CO₂, so today's sea takes it up in winter and gives some back in summer; the pH swings by about 0.04 over the year. In 2100 the rising atmosphere pushes CO₂ in all year, and the pH is about 0.29 lower.
- **Warming.** In 2100, warm water holds less oxygen and the metabolic demand rises. The dead zones are larger and more often anoxic. Their bottom water, loaded with respiration CO₂ on top of the higher background, turns corrosive.
- **The air.** Plumes of a few ppm stand above the sources and drift with the wind. Vegetation draws the air down by day and raises it by night. The sea's uptake is a small term in the regional air budget: the sources emit in two days what today's sea takes up in a year.

## 9. In the viewer

**🫧 Load ocean carbon & dead zones (today)** and **🌡 … (2100)** open the **Carbon & ocean life** view at the month with the largest dead zone. The viewer validates the carbon block on load ([viewer/src/oceanCarbon.js](../../viewer/src/oceanCarbon.js)): shapes, land without sea chemistry, columns without gaps, classes against the oxygen thresholds, dead zones against the bottom oxygen, areas and uptake against the maps, and the sea and air budgets.

- **Map.**
  - The sea is coloured by one quantity: bottom oxygen, the habitat class of the column, oxygen at a chosen depth, surface pH (the run's range), surface or bottom aragonite saturation, the CO₂ flux, the CO₂ of the air over the sea, production or nitrate.
  - The land is tinted by the CO₂ in the air above it.
  - Overlays: dead zones as translucent red volumes with a red outline (taller where less oxygen is left); anoxic columns ☠; corrosive water outlined in magenta; the CO₂ sources 🏭 with their emission and the CO₂ above the background; the river mouths ⊕ with their nitrate load; wind arrows; labels of the largest dead zones.
  - A key explains the colours.
- **Metrics.** The CO₂ in the air (background and highest), the sea's uptake, surface pH and Ω, dead-zone, anoxic and corrosive areas, the habitable volume, bottom oxygen, production and the emissions.
- **The year in the sea.** Charts of the dead-zone, anoxic and corrosive areas, the uptake, pH, Ω, the CO₂ in the air (background, over the sea, over land, highest) and production, with the current frame.
- **Water column.** The habitat class, oxygen, pH and Ω of each level down to the seafloor, with the hypoxia and Ω = 1 thresholds, and the column's oxygen over the year (a time–depth section). It follows the selected sea cell, or else the cell with the lowest bottom oxygen of the run.
- **Dead zones now.** The connected dead zones with area, lowest bottom oxygen, anoxic and corrosive cells, depth and the nearest river mouth; a click shows a zone's worst column.
- **Carbon budgets.** The sea's and the air's budgets since the start, in Mt CO₂, with the river nitrogen and the denitrification.
- **Today ↔ 2100.** Loads the other run on demand and compares the whole-year numbers.
- **Field explorer and inspector.** The layer group **Carbon & ocean life** (CO₂ in the air, CO₂ flux, pH, Ω at the surface and the bottom, bottom oxygen, habitat, habitable share, production, nitrate). The inspector gives a cell's air CO₂ (and emission), and for sea cells the seafloor, habitat class or dead zone, oxygen, pH, Ω, pCO₂, flux, production and nitrate.

## 10. Tests

- **`weather.ocean_carbon`** (kernel):
  - the carbonate constants against published values; TA reconstruction; acidification on doubling CO₂; the Revelle factor; pCO₂ rising with warming;
  - Schmidt numbers and transfer velocity; the metabolic index;
  - a closed shelf column conserving N, C and TA, with O₂ balanced by denitrification and sulfide;
  - a stratified turbid shelf turning hypoxic at the bottom, and winter mixing restoring it;
  - equilibration with a high-CO₂ atmosphere; export;
  - the conservative transport: uniform fields stay uniform, a blob is carried downstream, a converging current over a sloping seafloor with a river and an open boundary closes every budget;
  - river eutrophication; thin bottom remainders merging;
  - the atmosphere's steady state, plume, budget and rising background;
  - request validation.
- **`weather.ocean_carbon_cli`** — short summer runs of the example basin:
  - the contract (shapes, classes against oxygen thresholds, dead zones against bottom oxygen, areas against maps, uptake against the flux map);
  - the carbon, nitrogen and air budgets;
  - the air and sea seeing the same exchange;
  - plumes over the sources; nitrate at the delta mouth;
  - clean rivers giving less production and more bottom oxygen on the shelf;
  - a 900 ppm atmosphere giving more uptake, lower pH and Ω;
  - nine rejected requests.
- **`weather.viewer_ocean_carbon`** and **`weather.viewer_ocean_carbon_2100`** — regenerate the two fixtures and compare them with the committed files (about a minute each).
- **Viewer** (`oceanCarbon.test.js`):
  - the fixtures validate, and broken budgets, misplaced dead zones or land chemistry are rejected;
  - the background rises and the sources stand out;
  - 2100 is more acid with larger dead zones;
  - the dead zones are seasonal and on the shelf;
  - water columns reach the seafloor;
  - the budgets add up;
  - colours.

## 11. Limitations

- **Regional chemistry, not an Earth-system model.**
  - No phosphate, silicate or iron limitation, no zooplankton, and no dissolved organic matter.
  - One production law with fixed Redfield ratios.
  - The sinking matter is respired instantly along the Martin curve.
  - Benthic respiration has no sediment memory.
- **Transport.**
  - The current is the depth-uniform current of the 1.5-layer ocean. No baroclinic shear and no Ekman up- or downwelling is resolved.
  - The coastal upwelling and the plume's spreading come only from the potential-flow corrections.
  - The deep water below the active layer moves only by diffusion and ventilation.
- **Gas exchange.**
  - A quadratic wind law without bubbles or chemical enhancement.
  - No fugacity correction.
  - No nutrient or carbon deposition from the air.
- **The air.**
  - A single well-mixed boundary layer of fixed height, without a diurnal cycle of its depth. Real night-time plumes are stronger.
  - No vertical structure, and no fossil versus biogenic attribution.
  - The land exchange is a light response without soil moisture, season or land cover.
- **The example.**
  - A synthetic sea with prescribed profiles and forcing. Read the numbers as a process demonstration: dead zones of the right kind, place and season, and acidification of the right size. They are not a forecast for the Gulf of Mexico.
  - One year per scenario; no interannual variability.
