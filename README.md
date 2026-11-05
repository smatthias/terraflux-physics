# TerraFlux Physics

A C++ home for deterministic, testable physical models used by TerraFlux. The first planned area is weather physics, with a future interface for coupling weather to terrain and wildfire spread.

The engine is intended to read structured weather datasets and rendered weather maps, interpret them into georeferenced observations, and keep those observations distinct from simulated state. The current implementation decodes sidecar-backed radar and temperature PNG/PPM maps, tracks timestamped temperature frames through a 2D dry transport model, and writes versioned forecast JSON. NOAA MRMS is the temporary live radar data source; its GRIB2 acquisition helper is in place, while GRIB2 decoding into the model's canonical observation contract remains the next step. The planned Victoria source remains BOM radar, pending a suitable commercial licence and sample.

## Reviewer and handover guide

See [docs/HANDOVER.md](docs/HANDOVER.md) for setup, inspection controls, reviewer commands, implemented boundaries and remaining checks. Examples remain synthetic; AI and prediction work are excluded.


## Layout

- `docs/weather/architecture.md` — model scope, boundaries, and implementation sequence
- `docs/weather/dry-transport-model.md` — first solver assumptions, discretization, stability bound, and analytic cases
- `docs/weather/observation-grid.md` — georeferenced observation projection, cell assignment, and quality preservation
- `docs/weather/dry-transport-config-v1.schema.json` — dry transport configuration schema
- `docs/weather/dry-transport-state-v1.schema.json` — dry transport state schema
- `docs/weather/pressure-wind-model.md` — pressure-gradient wind with Coriolis deflection and surface friction: equations, balances, time stepping, configuration
- `docs/weather/pressure-wind-run-v1.schema.json` and `pressure-wind-forecast-v1.schema.json` — pressure-wind run request and output schemas
- `docs/weather/moist-air-relations.md` — estimated dependence between temperature, air density and humidity (ideal gas, Magnus form, sensitivities)
- `docs/weather/coupled-wind-model.md` — coupled wind / moist-layer model: density feedback, scalar transport, configurable diffusion
- `docs/weather/coupled-wind-run-v1.schema.json` and `coupled-atmosphere-v1.schema.json` — coupled run request and viewer timeline extension schemas
- `docs/weather/atmospheric-column-model.md` — vertical layers: hydrostatic thinning, convective adjustment, surface-parcel ascent (LCL, LFC, EL, CAPE, CIN)
- `docs/weather/column-run-v1.schema.json` and `column-profile-v1.schema.json` — column run request and profile output schemas
- `docs/weather/coriolis-verification.md` — review of Coriolis direction and pressure-gradient acceleration conventions with the verification cases
- `docs/weather/cloud-formation-model.md` and `cloud-formation-v1.schema.json` — fronts (cold/warm/stationary), frontal and convective cloud, thunderstorms; the documented output format accepted from the engine or from mocks
- `docs/weather/storm-systems-model.md`, `storm-systems-v1.schema.json` and `surface-v1.schema.json` — tropical cyclones: potential intensity from humid air over a warm sea, genesis index, Fujiwhara interaction and mergers, landfall with surge and inland decay
- `docs/weather/severe-convection-model.md` and `severe-convection-v1.schema.json` — capping temperature inversions, low-level shear and helicity, the Significant Tornado Parameter
- `docs/weather/orography-model.md` and `orography-v1.schema.json` — mountains and valleys: ridge orientation as a weather border, Froude blocking and barrier jets, foehn (moist ascent ~0.6 K/100 m, dry descent ~1 K/100 m), orographic precipitation, valley cold-air pools
- `docs/weather/catchment-hydrology-model.md`, `catchment-run-v1.schema.json` and `catchment-hydrology-v1.schema.json` — where the rain goes: depression-filled D8 drainage network with lakes, basins and ocean/edge outlets, snow → surface → soil → groundwater stores with Darcy flow and baseflow, kinematic overland/channel routing with bankfull overflow, and the hazard layer (flood depth × velocity classes, stream-power erosion/deposition, infinite-slope landslides with runout, rainfall intensity–duration warnings)
- `docs/weather/ocean-circulation-model.md`, `ocean-run-v1.schema.json` and `ocean-circulation-v1.schema.json` — rain on the sea and the coast, evaporation, the air–sea heat exchange and the layered temperature/salinity column (§7), and the ocean streams: a reduced-gravity upper layer on a β-plane (Sverdrup interior, Stommel western boundary current, separation and extension, subpolar gyre), salinity and temperature with density feedback, river exchange points with surface plumes and mixing fronts, deep-water formation and the overturning that fresh water weakens
- `docs/weather/polar-melt-model.md`, `polar-melt-run-v1.schema.json` and `polar-melt-v1.schema.json`: melting polar ice and the Atlantic overturning. Covers a Greenland-like ice sheet (shallow-ice flow, degree-day melt with the melt–elevation feedback, ocean-driven frontal melt), thermodynamic sea ice, an Arctic column, and a two-box overturning calibrated to 17 Sv with its tipping point, hysteresis and critical slowing down. Warming scenarios from 1850 to 2300 answer when the currents collapse, with an uncertainty ensemble and ocean temperature and oxygen indicators. Sea ice, a coupled ice sheet, warming and meltwater hosing in the 2-D ocean are covered in `ocean-circulation-model.md` §8.
- `docs/weather/coastal-flooding-model.md`, `coastal-flooding-run-v1.schema.json` and `coastal-flooding-v1.schema.json`: sea-level rise and its coastal impacts, the follow-up of the polar melt.
  - A sea-level budget: thermal expansion with the deep-ocean lag, glaciers, Antarctica, land water, Greenland from the polar-melt run, regional fingerprints and subsidence.
  - Land permanently lost below high water (bathtub with connectivity and defences).
  - 100-year storm floods with wind setup over the shelf and attenuation inland.
  - Storms pushing up rivers, salt intrusion, Bruun erosion, drowning islands.
  - How often today's 100-year flood comes, per-region ranking and events to 2300.
- `docs/weather/ocean-carbon-model.md`: CO₂ in the air and the sea, run inside the ocean CLI.
  - Boundary-layer CO₂ over land and sea: scenario background, city and industry sources, wind, vegetation, ocean uptake.
  - The sea's carbonate chemistry (pH, aragonite saturation) and air–sea CO₂/O₂ exchange.
  - Carbon, oxygen and nitrate on depth levels to the seafloor: river nitrate, plankton, sinking and seafloor respiration.
  - Habitat classes (habitable, stressed, hypoxic, anoxic) and seasonal dead zones, today and in 2100.
- `docs/weather/solar-cycle-model.md`, `solar-cycle-run-v1.schema.json` and `solar-cycle-v1.schema.json`: the Sun's 11-year cycle and what it does to the Earth.
  - The sunspot record (SILSO monthly file, or synthesised from a cycle table), Hathaway cycle curves, minima and maxima of the 13-month smoothed series.
  - A forecast of the cycle in progress and the next ones with an ensemble, tested by a backtest on the past cycles.
  - TSI, F10.7, Mg II and UV bands from Lyman-α to UV-A; ozone, the stratosphere and the surface UV index.
  - Flare rates and sizes, NOAA radio-blackout levels and flare energy; the global temperature response; the solar disk and the butterfly diagram.
  - A forcing export that the ocean shortwave and the polar-melt scenarios can take.
- `docs/weather/solar-grand-minima-model.md`: grand solar minima and maxima since 1000 AD (the `long_term` section of the solar request).
  - A catalogue of epochs (Oort, Wolf, Spörer, Maunder, Dalton, Medieval and Modern Maximum), episodes detected in the series, a case study of the Maunder Minimum.
  - The slow brightening of the quiet Sun (low / central / high TSI), UV and ozone, cosmic rays and ¹⁴C/¹⁰Be production.
  - The climate with volcanic and CO₂ forcing for comparison, an illustrative regional winter response, and a future grand minimum.
  - A stochastic Babcock–Leighton dynamo map in which grand minima arise by chance, calibrated to the Holocene statistics, with the risk of a new one.
- `docs/weather/solar-wind-model.md`, `solar-wind-run-v1.schema.json` and `solar-wind-v1.schema.json`: the solar wind at the magnetosphere's polar caps and what it means globally.
  - A tilted dipole with magnetic coordinates and the cosmic-ray cutoff; the solar-wind coupling (Newell Kp, O'Brien–McPherron Dst, Shue magnetopause); the expanding and contracting polar cap (Milan) with the auroral oval, aurora and Joule heating; the Jacchia thermosphere.
  - Escape to space: diffusion-limited hydrogen by Jeans escape, charge exchange and the polar wind; O⁺ outflow from the cusp and the oval; helium.
  - Galactic cosmic rays (force field, effective yields for the ground, 12 km and the Pfotzer maximum, cosmogenic production) and solar proton events with their NOx and polar ozone loss.
  - A severe storm hour by hour, the solar cycle, and scenarios of storms, proton events, a weaker dipole, methane and places.
- `docs/weather/cosmic-dust-model.md`, `cosmic-dust-run-v1.schema.json` and `cosmic-dust-v1.schema.json`: the cosmic dust and extraterrestrial water that enter the atmosphere every day (30–100 t/day).
  - The Grün size distribution, the entry of every size, speed and angle (heating, melting, evaporation, dehydration), calibrated to the Antarctic micrometeorite collections.
  - Where the water is released, and the budgets of mass, water, energy, iron and meteoric smoke against their terrestrial counterparts; what it would take for the dust to matter for the climate.
  - Hourly global maps of the influx from the sporadic meteoroid sources (apex, helion, antihelion, toroidal).
- `docs/weather/precipitation-outcomes-model.md` and `precipitation-outcomes-v1.schema.json` — what each cloud genus delivers: rain, drizzle, snow, sleet, showers, prolonged rain, lightning and hail, with the rate, phase and hazard rules
- `docs/weather/map-interpretation.md` — map input and interpretation pipeline
- `docs/weather/map-source-status.md` — NOAA prototype and BOM source status
- `docs/weather/noaa-mrms.md` — NOAA MRMS acquisition, bounded GRIB2 decoding, and attribution
- `docs/weather/precipitation-estimation.md` — configurable empirical conversion from radar reflectivity to rain-rate estimates
- `docs/weather/precipitation-accumulation.md` — timestamped rain-rate integration and interval quality rules
- `docs/weather/mobile-weather-response-v1.schema.json` and `mobile-weather-response-v1.md` — versioned mobile envelope combining forecast and aligned precipitation accumulation
- `docs/weather/map-decoder.md` — decoder API, CLI, formats, dependencies, and limitations
- `docs/weather/observation-schema.json` — v1 raster-sidecar schema
- `docs/weather/observation-record-v1.schema.json` — canonical observation schema
- `include/terraflux/weather/` — public observation, gridding, and transport model types
- `src/weather/` — sidecar parsing, raster and GRIB2 observation import, serialization, precipitation estimator and accumulator, CLI, observation mapping, and transport solver
- `tests/weather/` — analytic transport cases and planned map/reference coverage
- `examples/weather/maps/` — synthetic palette rasters and metadata
- `tools/fetch_noaa_mrms.py` — download one current NOAA MRMS composite and provenance
- `tools/decode_noaa_mrms.py` — decode a bounded MRMS grid crop into canonical reflectivity observations
- `tools/silso_to_request.py` — turn a downloaded SILSO monthly sunspot file into the record of a solar-cycle request
- `tools/make_land_mask.py` — sample the `global-land-mask` package at the cell centres of a global grid (the land masks of the cosmic-dust and solar-wind examples)
- `terraflux-estimate-rain-rate` — estimate a precipitation rate from reflectivity observations with configurable Z-R coefficients
- `terraflux-accumulate-rainfall` — integrate timestamped rate observations into precipitation depth in millimetres
- `terraflux-run-pressure-wind` — integrate the wind between configured high- and low-pressure systems with Coriolis deflection and friction
- `terraflux-run-coupled-wind` — the same wind coupled with temperature, humidity and diagnosed density, with configurable diffusion; writes a viewer-ready timeline, optionally with per-cell vertical column diagnostics
- `terraflux-run-coastal-flooding`: a rising sea on a coast (land lost, storm floods, defences overtopped, rivers pushed back, salt intrusion, islands), year by year for the polar-melt scenarios; writes a viewer timeline with a `coast` block
- `terraflux-run-polar-melt`: Greenland, the Arctic sea ice and the Atlantic overturning under warming scenarios over centuries; writes a viewer timeline with a `polar` block
- `terraflux-run-solar-cycle`: the sunspot cycle with its minima, maxima and forecast, and the UV, ozone, flares and climate response it brings; writes a viewer timeline with solar-disk frames and a `solar` block
- `terraflux-run-solar-wind`: the solar wind at the polar caps (aurora, escaping ions, cosmic radiation, solar protons) in a storm, over the solar cycle and in scenarios; writes a viewer timeline with global storm maps and a `solar_wind` block
- `terraflux-run-cosmic-dust`: cosmic dust and extraterrestrial water entering the atmosphere (what burns, what reaches the ground, where the water goes, the budgets); writes a viewer timeline with hourly global influx maps and a `cosmic_dust` block
- `terraflux-run-atmospheric-column` — one vertical column: hydrostatic layers, convective adjustment, lifted surface parcel, cloud base and CAPE
- `CMakeLists.txt` — C++17 weather map and transport targets with CTest integration

## Working principles

- Use SI units and document coordinate and direction conventions.
- Preserve source, valid time, georeferencing, quality, and uncertainty for interpreted data.
- Keep map parsing and physical calculations independent of rendering, networking, and user interface code.
- State model assumptions, spatial and temporal scales, and boundary conditions.
- Validate simple cases before combining processes.

### Wind between high- and low-pressure systems

The pressure-gradient wind module (`include/terraflux/weather/pressure_wind.hpp`) accelerates a single dry layer down the gradient of a prescribed pressure field, deflects it with the Coriolis force, and slows it with linear surface friction. Air therefore does not flow straight from high to low pressure: without friction it settles into the geostrophic wind along the isobars (low on the left in the Northern Hemisphere, on the right in the Southern Hemisphere), and with friction it crosses the isobars toward low pressure by `atan(k/|f|)`. Configuration is split into `time` (start, timestep, steps), `grid` (the shared transport grid and its anchor latitude), and `atmosphere` (density, friction coefficient, Coriolis mode: `none`, `f_plane`, `beta_plane`, or `prescribed`). Pressure is given per cell or built from Gaussian high/low centers.

```sh
./build/terraflux-run-pressure-wind examples/pressure-wind-request.json pressure-wind.json
```

The output includes the analytic geostrophic and friction-balanced winds as diagnostics. With periodic gradients on an f-plane the geostrophic wind is discretely divergence-free and can be supplied as the prescribed wind of the dry transport model. See [the model contract](docs/weather/pressure-wind-model.md).

### Coupled wind, temperature, density and humidity

`terraflux-run-coupled-wind` extends the pressure-driven wind with the fields that move with it: temperature and specific humidity are transported by the wind, air density is diagnosed from pressure, temperature and humidity (`ρ = p / (R_d T (1 + 0.608 q))`) and feeds back into the pressure-gradient acceleration, and relative humidity is reported as a diagnostic. The `diffusion` section configures separate temperature, humidity and momentum diffusivities that smooth the fields between the high and the low. The estimated dependence between the three values (about −0.35 % density per K, −0.6 % per 10 g/kg, RH falling ~4.9 points per K at fixed moisture) is documented in [moist-air relations](docs/weather/moist-air-relations.md); the model contract is [coupled wind model](docs/weather/coupled-wind-model.md).

```sh
./build/terraflux-run-coupled-wind examples/coupled-wind-request.json coupled-timeline.json
```

The output is a viewer timeline with a `coupled_atmosphere` extension. Pressure is prescribed and static; there is no condensation or latent heating in the horizontal layer.

### Vertical layers, thinning and convection

The atmospheric column module builds hydrostatic layers above each cell from its surface pressure, temperature and humidity: pressure and density thin with height (`dp/dz = −ρg` with moist-air density; the dry ISA profile is reproduced to 10⁻⁹). Two exchange processes connect the ground to higher altitudes: dry convective adjustment with saturation adjustment (mixing unstable layers, condensing supersaturated ones with latent heating, conserving enthalpy and water) and a lifted surface parcel that gives the lifting condensation level (cloud base), level of free convection, equilibrium level (cloud top), CAPE and CIN — the basis for cumulus cloud and thunderstorm potential. Add a `column` section to a coupled request to get these per cell and frame, or run one column in detail:

```sh
./build/terraflux-run-atmospheric-column examples/column-request.json profile.json
```

See [the column model contract](docs/weather/atmospheric-column-model.md). The Coriolis and pressure-gradient conventions were reviewed against the exact solution, a direction matrix over both hemispheres and textbook magnitudes: [coriolis-verification.md](docs/weather/coriolis-verification.md).

### Cloud formation: fronts, convection and thunderstorms

With a `cloud_formation` section the coupled CLI diagnoses cloud per cell and frame from two lifting mechanisms. Where warm, humid air meets colder air horizontally, the temperature gradient marks a frontal zone and the temperature advection decides whether cold air is advancing (cold front) or warm air (warm front); boundary-layer convergence lifts air at the front, warm air overruns the frontal wedge on the cold side, and layered cloud forms (nimbostratus → altostratus → cirrostratus with distance ahead of a warm front). Where the ground heats the surface parcel, it rises, cools and condenses: cumulus, towering cumulus and, with enough heating, cumulonimbus (summer thunderstorms). Both combine where frontal lifting releases convective energy. Output types, mechanisms, triggers, bases, tops and front fields follow the documented [cloud formation format](docs/weather/cloud-formation-model.md#output-format-documented-engine-or-mock); mocks in the same format are accepted when labelled `synthetic`.

```sh
./build/terraflux-run-coupled-wind examples/cloud-formation-request.json clouds.json
```

### Precipitation outcomes and the weather map

A `precipitation` section turns every diagnosed cloud into its weather at the ground. Harmless genera (cirrus, cirrostratus, altocumulus, cumulus) deliver nothing; stratus drizzles; altostratus and stratocumulus give intermittent light precipitation or virga; nimbostratus gives prolonged moderate rain or snow; towering cumulus gives showers; cumulonimbus gives heavy showers, lightning (top colder than −20 °C across the freezing level) and hail sized by the updraft. The rate comes from condensation in the lifted air, the phase from the surface wet-bulb temperature (snow ≤ 0.5 °C, sleet ≤ 2 °C), intensity from WMO classes, and accumulation from genus-typical duration fractions. See [precipitation outcomes](docs/weather/precipitation-outcomes-model.md). The viewer's **Weather map** view composes switchable layers — base field, isobars, fronts, cloud genus, cloud volumes, precipitation shading, symbols, thunderstorm/hail markers, wind — over any loaded coupled timeline (engine fixture or labelled mock).

### Combined weather response

`tools/compose_mobile_weather.py` composes forecast and explicitly gridded rainfall files with spatial, interval, and missing-data checks. See [the input contract and commands](docs/weather/mobile-weather-response-v1.md#file-composer). The `terraflux-grid-rainfall` CLI maps geographic accumulation observations onto the forecast grid before composition.

## Vue + Three.js physics viewer

```sh
cd viewer
npm ci
npm run dev
```

Open the URL printed by Vite. No login, database, or backend is required. Use **Open JSON** for a `terraflux.weather.forecast/v1` file or composed `terraflux.weather.mobile_response/v1` file. Files remain in browser memory. The initial synthetic demo is illustrative, not solver output. Temperature, wind speed, rainfall, wind vectors, orbit/zoom, and cell inspection are supported. Without terrain, height represents the selected field. With optional terrain enabled, height represents relative elevation and color represents the selected field. Single snapshots and validated snapshot timelines are supported; there is no live browser solver. Rainfall is unavailable for bare forecast files. Limit: 65,536 cells / 20 MB input.

Checks: `npm test` and `npm run build` inside `viewer/`.

### Reproducible engine example

Select **Load engine example** in the viewer to open the committed 12 × 10 example. It uses synthetic radar and atmospheric inputs evaluated by the production C++ dry-transport, rain-rate estimator, accumulator, and gridding kernels, then the Python composer. This is engine output from synthetic inputs, not live weather or a coupled rainfall/temperature simulation. It includes missing rainfall coverage.

After a normal CMake build, regenerate or verify the bundled file:

```sh
python3 tools/build_viewer_example.py --build-dir build
python3 tools/build_viewer_example.py --build-dir build --check
```

CTest runs the regeneration check. The comparison allows relative floating-point differences of 1e-9 across platforms. The snapshot file contains the final state after 260 × 30-second dry-transport steps. The companion timeline contains 14 snapshots at ten-minute intervals, with static prescribed wind. Each positive-time frame includes radar-derived rainfall accumulated from the start up to that frame.

### Coupled wind example and request editor

**Load coupled wind example** opens the committed 24 × 18 output of `terraflux-run-coupled-wind` (7 snapshots over 48 h). Pressure, relative humidity, air density and specific humidity become selectable layers, and the cell inspector shows the coupled values. The **Coupled wind request** panel edits time steps, grid, atmosphere, the three diffusivities, boundaries, density mode and the high/low systems with their temperature and humidity anomalies. It reports the solver bounds (Coriolis/friction, momentum diffusion, an estimate of the scalar stability number at the spin-up peak) and the implicit upwind numerical diffusion, then downloads a request JSON to run with the C++ CLI. **Use loaded run's settings** fills the editor from an opened coupled timeline. With column diagnostics present, **Convective energy (CAPE)**, **Cloud base (LCL)** and **Equilibrium level** are additional layers and the inspector lists LCL, LFC, EL, CAPE, CIN and the potential class per cell; the editor's column fields configure layers, lapse rate, tropopause and humidity scale height. No physics is solved in the browser.

### Cloud formation view

**Load cloud formation example** opens the engine fixture (20 × 15 cells, 48 h, 8-hourly frames); **Cloud formation mock (synthetic)** builds a labelled mock in the same format. The **Cloud formation** layer colours cells by cloud genus, raises the ground by cloud fraction, draws translucent cloud volumes from base to top with thunderstorm markers, and overlays cold (blue), warm (red) and stationary (purple) frontal cells; the inspector lists genus, base/top, mechanism, trigger, front analysis and heated CAPE/CIN, and the metrics strip shows cover, thunderstorm and front counts. The request editor carries the full cloud configuration, the diurnal heating, and free air masses (warm sector / cold pool).

### Weather map view

**Weather map** (scene header) switches from the field explorer to a flat map with layers that can be toggled independently in **01b / Weather map layers**: base field (temperature / surface pressure / none), isobars at 1, 2 or 4 hPa with labels, fronts, cloud cover by genus, 3D cloud volumes, precipitation rate shading, precipitation symbols, ⚡/◆ hazard markers and wind vectors. The legend follows the active layers. It works for any coupled timeline; cloud and precipitation layers need the cloud formation example or mock.

### Hurricanes, mergers, landfall and tornadoes

With `surface` (coastline, sea-surface temperature) and `storms` sections the coupled CLI tracks tropical cyclones as objects: their energy is Emanuel's potential intensity from the enthalpy of saturated air at the sea surface versus the boundary layer; genesis follows the Emanuel–Nolan index (vorticity, mid-level humidity, potential intensity, shear); storms move with the steering flow and beta drift, rotate around each other through their induced winds (Fujiwhara) and merge when their cores overlap; at landfall the wind drops, a surge estimate is recorded and the storm decays by Kaplan–DeMaria. A `severe` section with a `column` inversion adds tornado potential: the capping inversion stores CAPE until heating breaks it, the Ekman turning of the surface wind supplies low-level helicity, and the Significant Tornado Parameter classifies the environment. See [storm systems](docs/weather/storm-systems-model.md) and [severe convection](docs/weather/severe-convection-model.md).

```sh
./build/terraflux-run-coupled-wind examples/hurricane-landfall-request.json hurricane.json
./build/terraflux-run-coupled-wind examples/tornado-outbreak-request.json tornado.json
```

**Load hurricane landfall example** opens the committed run on the weather map with cyclone centres, category colours, tracks, landfall/merger/genesis markers, the storm wind field, coastal surge and land/sea as switchable layers; the field explorer adds storm wind, potential intensity, genesis index, surge, tornado parameter and helicity layers, and the request editor has the coastline, sea temperature, storm seeds, severe toggle and inversion fields.

### Mountains: orientation, foehn and cold-air pools

An `orography` section gives the coupled layer terrain (per cell, or synthetic ridges with an explicit axis orientation plus basins). Air moving along slopes changes temperature at the adiabatic rates — saturated ascent at the moist rate (about 0.6 K/100 m for cool air, computed from the thermodynamics), descent at the dry rate (about 1 K/100 m) — so a ridge crossing produces orographic precipitation on the windward side and the **foehn** (warm, dry) on the lee. Flow that cannot climb a range is blocked according to the cross-barrier Froude number and deflected along it, so a north–south range (Rockies) blocks zonal flow while an east–west range (Alps) blocks the north–south exchange; flow parallel to a ridge is untouched. A diurnal surface cycle enhanced in valleys lets cold air pool in basins at night. See [orography](docs/weather/orography-model.md).

```sh
./build/terraflux-run-coupled-wind examples/alps-foehn-request.json alps.json
./build/terraflux-run-coupled-wind examples/rockies-barrier-request.json rockies.json
```

**Load Alps foehn example** opens the weather map on a hillshaded 3D relief with lifting, foehn, cold-pool, orographic-precipitation and blocking layers; the editor builds ridges by orientation.

### Where the rain goes: rivers, lakes, groundwater — and floods, erosion, landslides

`terraflux-run-catchment-hydrology` takes a terrain (per cell or the shared synthetic ridges/basins builder with tilt and roughness), its own time stepping (hydrology step, spin-up, snapshot cadence), the grid, soil / groundwater / channel / snow / hazard parameters and a forcing (rainfall events with optional Gaussian footprint and orographic enhancement, a lapse-rate diurnal air temperature, and/or the precipitation and temperature of a coupled-wind timeline). It fills depressions into lakes, derives the D8 river network, basins and outlets, and routes every drop through snow, surface, soil and aquifer stores to the rivers, the lakes and the sea while closing the water budget in every frame. The hazard layer classifies flooding by depth × velocity, bookkeeps stream-power erosion and deposition with a sediment yield, and fails saturated slopes with the infinite-slope factor of safety, running the debris downslope. See [catchment-hydrology-model.md](docs/weather/catchment-hydrology-model.md).

```sh
./build/terraflux-run-catchment-hydrology examples/alpine-flood-request.json flood.json
```

`terraflux-run-ocean-circulation` takes a basin (continents from the synthetic terrain builder or a mask, optional sponge rows as an open-boundary proxy), the ocean layer and physics parameters, and a forcing (gyre, uniform or coupled-wind wind stress; surface-temperature targets with cold formation patches; net precipitation; rivers with constant, event-driven or catchment-fed discharge). It runs the reduced-gravity layer — the wind-stress curl drives a Sverdrup interior whose return is the western boundary current, density gradients and deep-water formation drive the overturning — and reports the streams (core streamlines, speed, transport in Sverdrups), the salinity at the surface and in the layer, the exchange points with their plumes, the mixing fronts and the formation sites, with closed volume and salt budgets. See [ocean-circulation-model.md](docs/weather/ocean-circulation-model.md).

```sh
./build/terraflux-run-ocean-circulation examples/atlantic-streams-request.json ocean.json
```

**Rain over the sea and the coast, evaporation and a layered ocean.** `forcing.rain` lets rain fall on the sea (fresh water at its own temperature into the surface: a buoyant rain lens that cools the surface and caps convection until the wind erodes it) and on the land, where the coastal band drains directly to the nearest sea cell through a linear reservoir while land farther inland is left to the rivers and the catchment model; rain comes from events, moving storms or the cell-mean precipitation of a coupled-wind run. `forcing.air_sea` computes evaporation and the air–sea heat exchange by bulk formulae (Dalton evaporation and latent heat, sensible heat, Berliand longwave, Zillman/Reed shortwave from the sun's position), solved implicitly at the surface, from an analytic atmosphere with diurnal cycle and moving air masses (e.g. a cold, dry outbreak) or from a coupled run including its storm winds. `physics.thermal_layers` splits the upper ocean into a temperature and salinity column (sigma layers moving with the currents, shortwave penetration, convective adjustment, Kraus–Turner wind mixing, bulk-Richardson shear mixing, diffusion) with mixed-layer, isothermal-layer and barrier-layer diagnostics. Volume, salt, heat and the coastal land water close every step; without these sections the slab model and its output are unchanged. See [ocean-circulation-model.md §7](docs/weather/ocean-circulation-model.md#7-rain-over-the-sea-and-the-coast-evaporation-airsea-heat-and-the-layered-column).

```sh
./build/terraflux-run-ocean-circulation examples/ocean-heat-rain-request.json ocean-heat.json
python3 tools/build_ocean_example.py --build-dir build            # regenerate viewer/public/ocean-heat
python3 tools/build_ocean_example.py --build-dir build --check    # compare with the committed fixture
```

**Load ocean heat & rain example** opens the Ocean streams view on a late-summer subtropical basin: a tropical rain band crosses the sea and makes landfall, a storm floods the coastal plain, a cold dry outbreak draws heat and water out of the sea and the sun drives a daily warm layer. Colour the water by sea-surface temperature, mixed-layer depth, net heat flux or evaporation; overlays show rain on sea and land, the coastal band, runoff arrows to the sea and the surface lenses by source; the inspector decomposes the heat flux of a cell and tells where the rain on a land cell goes; the metrics strip, time series and a water-column panel (profile and time–depth section) show what happens.

**Melting polar ice and the overturning.** `terraflux-run-polar-melt` runs warming scenarios (global-mean warming curves) from 1850 to 2300 through the following:

- a Greenland-like ice sheet: shallow-ice flow, positive-degree-day surface mass balance with the melt–elevation feedback, and Rignot frontal melt from the ocean's thermal forcing;
- a central-Arctic sea-ice column;
- a two-box Atlantic overturning calibrated to today's 17 Sv, whose fresh water comes from Greenland, the Arctic and a stronger water cycle and whose tipping point falls with polar amplification.

It reports when the summer sea ice goes, when Greenland starts losing mass at its surface, when the overturning crosses its tipping point, weakens by 25 % / 50 % and collapses, with a 9-member ensemble over the two least-known quantities. It also reports the sea level, ocean temperature and oxygen solubility. In the ocean CLI, `physics.sea_ice`, an `ice_sheet` section, `forcing.warming` and `forcing.hosing` add drifting sea ice, a coupled ice sheet, climate warming and meltwater hosing to the layered ocean with seasons. Hosing can also be ramped down to test recovery: `examples/polar-ocean-recovery-request.json` runs for about 10 minutes. See [polar-melt-model.md](docs/weather/polar-melt-model.md) and [ocean-circulation-model.md §8](docs/weather/ocean-circulation-model.md#8-polar-processes-sea-ice-a-coupled-ice-sheet-warming-and-meltwater-hosing).

```sh
./build/terraflux-run-polar-melt examples/polar-melt-request.json polar.json            # about 50 s
./build/terraflux-run-ocean-circulation examples/polar-ocean-request.json polar-ocean.json   # about 5 min
python3 tools/build_ocean_example.py --build-dir build --cli terraflux-run-polar-melt \
  --request examples/polar-melt-request.json --output viewer/public/polar/engine-timeline.json --check
```

**🧊 Load polar melt & overturning example** opens the **Polar melt** view. It shows Greenland's ice in relief (thickness, surface mass balance, melt or air temperature), the metrics of the scenario in focus, the **"When do the currents collapse?"** table with the ensemble range, scenario charts and the equilibrium diagram of the overturning. **❄ Load polar ocean (sea ice & hosing) example** opens the Ocean streams view with sea ice, the ice sheet on land and the hosing ring. It includes charts of overturning, sea ice, SMB, northern SST and oxygen, and a year-by-year table of when the overturning weakens.

**Sea-level rise and the coasts.** `terraflux-run-coastal-flooding` takes the warming scenarios and Greenland's melt from the polar-melt run and turns them into a sea-level rise at the coast. The budget covers thermal expansion, glaciers and Antarctica, plus regional fingerprints and land subsidence. From that rise it maps, for every year to 2300:

- which land goes permanently under the high water;
- where the 100-year storm floods, with the wind setup over the shelf;
- when defences are overtopped;
- how far storms push up the rivers and salt intrudes;
- how sandy shores retreat and low islands drown.

It also ranks the regions by where it hits hardest. See [coastal-flooding-model.md](docs/weather/coastal-flooding-model.md).

```sh
./build/terraflux-run-coastal-flooding examples/coastal-flooding-request.json coast.json   # about 4 s; reads viewer/public/polar
```

**🏝 Load sea-level rise & coasts example** opens the **Sea level & coasts** view on a synthetic coast with a subsiding delta, an estuary city, barrier islands, cliffs and low islands. It shows:

- land lost, storm flooding, land behind defences, defences, rivers and salt;
- alternative land colours: the year the land is lost, flood frequency, population or subsidence;
- the sea-level components, a "where does it hit hardest?" ranking and a "when?" event table;
- charts over time and a rivers table.

**Ocean carbon, acidification and dead zones.** With the request sections `biogeochemistry` and `atmosphere_co2`, `terraflux-run-ocean-circulation` also computes:

- the CO₂ of the air over land and sea: the scenario background, plumes from cities and industry carried by the wind, drawdown by vegetation and by the sea;
- the ocean's CO₂ uptake and its acidification: pH and aragonite saturation from the carbonate chemistry;
- carbon, oxygen and nitrate on depth levels down to the seafloor: river nitrate feeds the plankton, whose sinking remains are respired at depth and on the seafloor;
- which water is habitable, stressed (corrosive or too little oxygen for the temperature), hypoxic or anoxic, and where the dead zones are.

See [ocean-carbon-model.md](docs/weather/ocean-carbon-model.md).

```sh
python3 tools/make_ocean_carbon_examples.py   # writes the two example requests
./build/terraflux-run-ocean-circulation examples/ocean-carbon-request.json carbon.json        # today, about a minute
./build/terraflux-run-ocean-circulation examples/ocean-carbon-2100-request.json carbon2100.json   # 2100, high emissions
```

**🫧 Load ocean carbon & dead zones (today)** or **(2100)** opens the **Carbon & ocean life** view on a Gulf-like sea with a delta river. It shows:

- the sea coloured by bottom oxygen, habitat, oxygen at a chosen depth, pH, aragonite saturation, CO₂ flux, production or nitrate; the land by the CO₂ in the air;
- dead zones as red volumes with outlines, anoxic columns ☠, corrosive water in magenta, the CO₂ sources and the river mouths with their nitrate loads;
- metrics, charts of the year (dead zones, uptake, pH, Ω, CO₂ in the air, production), the water column of a cell with its oxygen over the year, a dead-zone table, the carbon budgets of sea and air, and a today ↔ 2100 comparison.

**The Sun: the solar cycle, UV, flares and the climate.** `terraflux-run-solar-cycle` reads a monthly sunspot record (converted from SILSO with `tools/silso_to_request.py`, or synthesised from a table of cycles) and:

- finds the minima and maxima of the 13-month smoothed sunspot number and fits every cycle with a Hathaway curve;
- forecasts the cycle in progress and the next ones: a fit to the cycle so far, weighed against a prior from the earlier cycles by the errors a backtest on the past cycles found at the same age, and a 200-member ensemble for the next minimum and maximum with their ranges;
- turns the cycle into TSI, F10.7, Mg II and UV bands, ozone and stratospheric temperature, the clear-sky noon UV index at sites, flare rates and NOAA radio-blackout probabilities, and the global surface temperature of a two-layer energy balance;
- exports annual TSI and temperature response that the ocean shortwave (`forcing.air_sea.shortwave.solar_cycle_path`) and the polar-melt scenarios (`solar_cycle`) can take.

See [solar-cycle-model.md](docs/weather/solar-cycle-model.md).

```sh
./build/terraflux-run-solar-cycle examples/solar-cycle-request.json solar.json   # about 7 s
# Real data: download SN_m_tot_V2.0.csv from SILSO, then
python3 tools/silso_to_request.py SN_m_tot_V2.0.csv --start 1850-01 --first-cycle 10 --output my-solar-request.json
```

The example's record is synthesised from an approximate, hand-entered table of the SILSO cycles 1–25 (not verified against the SILSO files) with monthly noise; its dates illustrate the method rather than forecast the real Sun.

**Grand minima and maxima.** The request's `long_term` section extends the Sun back to 1000 AD: cycles synthesised around a catalogue of documented epochs before 1755, then the table, the record and the forecast to 2100. It maps the grand minima (Oort, Wolf, Spörer, Maunder) and maxima, the slow brightening of the quiet Sun, the ¹⁴C/¹⁰Be production of cosmic rays, and the global temperature with volcanic and CO₂ forcing for comparison. A stochastic dynamo map shows how grand minima arise (a weak Babcock–Leighton source leaves the field below the spot-emergence threshold; a weak α effect keeps a faint cycle until it recovers) and gives the probability of a new one (about 12 % by 2100 in the example). In the model the Maunder Minimum's dimmer Sun cools the globe by about 0.05 K (0.03–0.16 K across the background scenarios), volcanic eruptions matter more for the Little Ice Age, and a new grand minimum would offset at most about 0.13 K of this century's warming. See [solar-grand-minima-model.md](docs/weather/solar-grand-minima-model.md).

**☀ Load solar cycle & space weather example** opens the **Sun & solar cycle** view. It shows:

- the solar disk as a dome: limb-darkened photosphere, spot groups at the latitudes of their cycle's zone, faculae, and the month's flares coloured by radio-blackout level; the disk can be coloured by brightness, by contrast to the quiet Sun, or by cycle;
- the sunspot chart with record, smoothed series, fitted curves and the forecast's 50 % and 90 % ranges, and a table of minima and maxima with the predicted ones;
- the backtest skill by cycle age, the UV spectrum of the cycle, TSI, UV at the top against the UV index at the ground with a site table, flare counts, the radio-blackout table and an annual flare outlook, the climate response, and the butterfly diagram;
- grand minima and maxima since 1000 AD with the epochs and detected episodes, long-term TSI and cosmic rays, the climate contributions with an epoch table, a Maunder Minimum case study, the dynamo map with a synthetic Holocene, and the outlook for a new grand minimum.

**The solar wind at the polar caps.** `terraflux-run-solar-wind` follows a synthetic severe storm hour by hour: a solar proton event, then a fast coronal mass ejection whose magnetic cloud turns the field southward. The magnetic field is not weaker at the poles (it is twice as strong as at the equator); there the field lines are open to the solar wind. So the model maps what comes in and goes out through them:

- **Magnetosphere.** Kp and Dst, the magnetopause pushed inside geostationary orbit, the open polar cap widening to 65° and the aurora reaching 45° magnetic latitude.
- **Energy and the thermosphere.** Auroral and Joule heating that raise the thermosphere by about 400 K and triple the satellite drag.
- **Escape.** Oxygen ions escaping from the cusp and the night-side oval, and hydrogen escaping everywhere, limited by its supply from below.
- **Radiation.** Galactic cosmic rays behind the geomagnetic cutoff, with a Forbush decrease; solar protons entering only the polar caps, where their NOx thins the ozone above 35 km.

The monthly solar cycle comes from the solar-cycle example. Scenarios cover storms up to the Carrington class, proton events, a weaker dipole (Laschamp), methane, and what 18 places see.

Globally: the Earth loses about 4 kg/s, mostly hydrogen. Oxygen (0.3 kg/s) leaves only through the poles. The energy that enters is 4·10⁻⁷ of the absorbed sunlight, negligible for weather and climate. The effects that matter are on the upper atmosphere (drag, polar ozone), on technology and on aviation dose: polar routes receive 2–3 times the cosmic radiation of equatorial ones. See [solar-wind-model.md](docs/weather/solar-wind-model.md).

```sh
./build/terraflux-run-solar-wind examples/solar-wind-request.json solar-wind.json   # about 2 s
```

**🧲 Load solar wind & polar caps example** (in the *Sun & space weather* group) opens the **Solar wind & poles** view. It shows:

- the Earth as a globe in a Sun-fixed frame with a sketch of the magnetosphere: closed and open field lines, the magnetopause, the solar wind, escaping O⁺ and H⁺, and cosmic rays or solar protons entering the caps;
- a choice of layers: aurora, escaping oxygen, dose at 12 km, solar-proton ionisation, cutoff rigidity, thermosphere temperature, open and closed field (or a flat world map);
- magnetic polar dials and latitude profiles, twelve storm charts, the escape budget, the solar cycle, and tables of proton events, storms, places and a weaker dipole, with a summary of the global effects.

**Cosmic dust and extraterrestrial water.** `terraflux-run-cosmic-dust` takes the total influx as a scenario (the example: 30 / 60 / 100 t/day; published estimates range from about 5 to 300 t/day). It then:

- spreads the influx over sizes with the Grün (1985) distribution;
- follows every size, speed and entry angle through the standard atmosphere (drag, heating, radiation, Langmuir evaporation, dehydration of the bound water);
- sums up what evaporates and where, and what reaches the ground unmelted, partly melted or as cosmic spherules;
- compares the dust's water, mass, energy, iron and meteoric smoke with the hydrogen escaping to space, the oceans, the rain, volcanic degassing, sunlight, geothermal heat, desert dust and volcanic aerosol;
- maps the influx hour by hour from the sporadic meteoroid sources.

The entry model is calibrated so that about a quarter of the mass reaches the ground, as the Antarctic collections suggest. With 60 t/day, about 43 t evaporate at a median height of 88 km and about 17 t reach the ground.

The water is 3 t/day at 5 % water. 99.7 % of it is released above 80 km. That is about 0.1 % of the water lost as escaping hydrogen, and 2·10⁻¹² of the rain, so it is not injected into the water models; per-area fluxes are exported instead. The smoke's forcing is at most ±0.006 W/m². It would take about 16× today's influx to reach 0.1 W/m². See [cosmic-dust-model.md](docs/weather/cosmic-dust-model.md).

```sh
./build/terraflux-run-cosmic-dust examples/cosmic-dust-request.json dust.json   # about 3 s
```

**☄ Load cosmic dust & extraterrestrial water example** (in the *Sun & space weather* group) opens the **Cosmic dust** view. It shows:

- the world map of the influx hour by hour over one day (or the daily mean), higher and brighter where more arrives, the night side darker, land tinted, and the points where the apex, helion, antihelion and toroidal sources and the Sun stand overhead;
- the scenario switch (30 / 60 / 100 t/day), the fate of the mass as a bar, the size distribution with the share that reaches the ground, and the altitude profiles of evaporation, water release and heating with the metal layers and noctilucent clouds;
- the water and budget table with the ratios, the influx over the day at a chosen cell and by latitude, and the "what would it take?" table for 1–10,000× today's influx.

The field-layer list is folded into topic groups showing only layers with data, with a tooltip for every layer and example.

**Load Atlantic streams example** opens the **Ocean streams** view: water coloured by surface salinity (or speed, temperature, thermocline depth, freshwater fraction), streams sized by transport and coloured by speed with labels, current arrows, river mouths ⊕ with discharge and plume salinity, fronts, formation sites ⇩, and time series of the overturning, the main stream and the river input — the example's meltwater pulse collapses the overturning.

**Load alpine flood example** opens the **Catchment** view (rivers sized by upstream area and coloured by discharge, lakes at their level, overland water, snow, rain; click a cell to trace its water to the outlet) and the **Hazards** view (flood classes, reaches above bankfull, erosion/deposition, landslides with runout, rainfall-threshold warnings); the field explorer adds discharge, soil saturation, water-table depth, snowpack, erosion, deposition, factor of safety and flood class, and the metrics panel shows the budget and a hydrograph at the peak or selected reach.

### Timeline playback

**Load engine example** opens the generated 14-frame timeline. Play/pause, previous/next, and the frame slider select discrete solver snapshots. Playback stops at the end; Play from the end restarts. Scrubbing or loading another dataset stops playback. Color ranges are fixed across all frames for each field. No temporal interpolation or browser-side physics is performed.

JSON input also accepts `terraflux.weather.timeline/v1`: an object with a `frames` array of 2–240 forecast or mobile-response objects. All frames must have the same grid and start time, strictly increasing elapsed times, and no more than two million cell snapshots in total. Rainfall is enabled only on frames containing an interval-aligned rainfall layer. See [timeline contract](docs/weather/timeline-v1.md).

### Optional terrain

The bundled timeline includes a deterministic synthetic Gaussian hill. Select **Terrain elevation** to inspect it, or use **Place fields on terrain** and the height-scale slider to display weather colors on its geometry. The 2D fallback shows elevation as color. Original metre values and their explicit datum appear in the cell inspector. An optional lapse-rate diagnostic can adjust displayed temperatures without modifying transport. See [terrain grid contract](docs/weather/terrain-grid-v1.md).

### Terrain temperature diagnostic

The C++ `adjust_temperature_for_terrain` function optionally computes `Tadjusted = Treference - lapse_rate × (elevation - reference_elevation)`. It preserves inputs and validates matching vertical datums, finite values, array lengths, and positive kelvin output. The function is disabled by default; the caller supplies grid-aligned arrays.

The example uses an explicitly chosen 0.0065 K/m lapse rate and 80 m reference elevation in its synthetic datum. These are scenario assumptions, not measured weather. In the viewer, **Terrain-adjusted temperature** switches between stored C++ diagnostic output and the original field, with the correction shown in the inspector. The adjustment is applied once to each snapshot and never feeds back into transport. Wind, pressure, moisture, and surface fluxes are unaffected.

The timeline may carry `terrain_temperature` with schema version `terraflux.weather.terrain_temperature/v1`, units `K`, finite `lapse_rate_k_per_m` and `reference_elevation_m`, a matching `vertical_reference`, and `values_by_frame` arrays matching every forecast. The viewer verifies the declared relation and rejects inconsistent output.

### Terrain slope and aspect

The C++ `calculate_terrain_derivatives` function computes metric terrain gradients, slope angle, and downhill aspect. The example bundles these fields for **Terrain slope** and **Downhill aspect** viewer layers. Aspect is clockwise from north, with undefined flat cells preserved as null. One-sided boundary differences do not wrap across the domain. See [method and contract](docs/weather/terrain-derivatives-v1.md).

### Surface-water storage and runoff prototype

The C++ `advance_surface_water` function adds interval rainfall and routes stored water between four neighboring cells down free-surface head gradients. Donor-limited transfers keep storage nonnegative. Boundaries are closed. Optional capacity-limited infiltration transfers surface water into a separate soil store; prescribed evaporation and soil drainage can remove water with separate accounting; there is no external surface outlet or momentum solver. It is a conservative storage/routing prototype, not shallow-water hydraulics.

The example applies each ten-minute rainfall increment uniformly over twenty 30-second steps, with routing speed parameter 0.002 m/s. Cells lacking rainfall coverage are impermeable masks and retain unknown/null water depth in the output. Water depth is stored in metres and displayed in millimetres. The **Surface water depth** layer includes stored volume, cumulative rainfall input and mass-balance residual for every frame. Terrain and weather remain independent of the routed water state. See [surface-water contract](docs/weather/surface-water-v1.md).

### Soil-water storage

The optional C++ infiltration step transfers available surface water into a capacity-limited soil store. The example runs rain → routing → infiltration → evaporation → drainage each 30 seconds. **Soil water storage** is a separate playback layer; budget readouts show surface and soil volumes against cumulative rainfall. The example rate and capacity are illustrative scenario parameters. See [infiltration contract and limits](docs/weather/infiltration-v1.md).

### Evaporation and soil drainage

`apply_water_losses` optionally removes a shared evaporation demand from surface then soil storage, followed by availability-limited soil drainage. Playback budgets show cumulative evaporation and drainage alongside remaining storage. The synthetic example uses prescribed rates, not a weather-driven evaporation calculation. All removed water remains explicitly represented in the mass budget. See [loss contract](docs/weather/water-losses-v1.md).

### Wet-to-dry regression scenario

The generated example covers 12:00–14:10 UTC: one hour of synthetic rain-rate estimates, a ten-minute linear taper to zero, then one rain-free hour. Explicit zero rates are scenario forcing; missing coverage remains unknown. This controlled edit is identified in observation derivation metadata. Cumulative rainfall stops growing at 13:10, while routing, infiltration, evaporation and drainage continue. The timeline labels intervals from rainfall increments. Regression checks verify constant dry-phase rain input, declining combined storage, nonnegative depths, growing accounted losses and mass closure. Soil and surface stores decrease over the dry period; they need not be completely empty.

### Clouds, regional weather and metrics

Select **Regional weather demo** for a separate 1,200 × 900 km analytic scene with moving low/high-pressure centers, a front, cloud cover, pressure and humidity layers. Cloud and system overlays work in Three.js and the 2D fallback. These are explicitly synthetic illustrations, not output from an atmospheric circulation or cloud model, and they do not drive local hydrology.

Metrics now show domain area, temperature range/mean, wind mean/maximum, cloud percentage, pressure range and humidity. Engine examples additionally show rainfall coverage/mean, wet area, peak surface-water depth, soil capacity usage and water budgets. See [display contract and metric definitions](docs/weather/regional-display-v1.md).

### Configurable hydrology scenario

Edit `examples/hydrology-scenario.json` to set routing speed, infiltration, soil capacity, evaporation, drainage and the water timestep. The C++ example generator validates the complete configuration before creating output. Supported boundary, missing-data and process-order settings are explicit; unsupported modes and unknown keys are rejected. Water timesteps must be integer divisors of the 600-second output interval. Atmospheric transport keeps its independent 30-second timestep.

```sh
python3 tools/build_viewer_example.py --build-dir build --scenario examples/hydrology-scenario.json
# Preserve the bundled fixture when experimenting with another config:
python3 tools/build_viewer_example.py --build-dir build --scenario my-scenario.json --output /tmp/custom-weather/engine-weather.json
```

Open `/tmp/custom-weather/engine-timeline.json` in the viewer. The timeline records the exact applied `water_scenario`, and its water output carries matching parameters. Water/soil budget readouts show timestep and rate settings. The [JSON Schema](docs/weather/hydrology-scenario-v1.schema.json) documents the file contract. Parameters remain illustrative and prescribed; no AI or predictive model is involved.

### Deterministic scenario comparison

Select **Compare hydrology scenarios** to load the baseline and a C++-generated variant with infiltration reduced from 3.60 to 0.36 mm/h. Playback is synchronized, the map can switch between runs, and water/soil layers share a common color range. A table shows selected-time surface storage, soil storage, evaporation, drainage and rainfall, with delta defined as variant minus baseline. The chart compares surface storage across the full timeline and marks the current frame.

**Open comparison JSON** accepts another generated timeline only when grid, times, terrain, rainfall, atmospheric forcing and coverage match the current baseline. Both runs must provide water output at every snapshot. This is a deterministic parameter comparison, not AI prediction or an assessment of real soil behavior.

Regenerate or verify the bundled variant:

```sh
python3 tools/build_viewer_example.py --build-dir build --scenario examples/hydrology-low-infiltration.json --output viewer/public/comparison/engine-weather.json --timeline-only
python3 tools/build_viewer_example.py --build-dir build --scenario examples/hydrology-low-infiltration.json --output viewer/public/comparison/engine-weather.json --timeline-only --check
```

CTest checks both baseline and variant regeneration. The `--timeline-only` flag avoids saving an unused final-snapshot file for comparisons.

### Comparison CSV export

After loading a comparison, select **Export all frames (CSV)**. The download contains one row per snapshot, regardless of the current playback frame or visible scenario. Columns include UTC valid time, elapsed seconds, scenario labels, known/masked cell counts, baseline/variant/delta volumes (surface, soil, evaporation, drainage, rain and balance residual), applied timestep/rate parameters and soil capacities. Volume units are m³, storage capacities metres and rates m/s, identified in column names. Delta means variant minus baseline.

The CSV uses UTF-8 with BOM and CRLF rows, preserves numeric precision, escapes quoted/multiline labels, and neutralizes labels that could be interpreted as spreadsheet formulas. The export contains aggregates for the modeled coverage; masked cells are counted explicitly. Data are downloaded locally without a server or database.

### Water timestep sensitivity

`python3 tools/check_water_timestep.py --build-dir build --check` reruns the synthetic hydrology case at 15, 30 and 60 second water timesteps. It checks storage positivity, capacity, coverage, input consistency and full mass accounting, then verifies the committed report. CTest includes this regression. The report measures spatial surface-water differences from the 15-second run; it is not proof of physical accuracy or convergence. See [method and results](docs/weather/water-timestep-sensitivity.md).

### Basin ponding and overflow regression

`ctest --test-dir build -R surface_water_basin --output-on-failure` checks stable ponding below a spill point, equal-head retention, internal overflow above it, a raised spill barrier and rainfall-driven filling. Each step verifies nonnegative depths and the closed-domain water budget. This is a controlled routing regression, not a hydraulic spillway model. See [geometry and limits](docs/weather/surface-water-basin-test.md).

### Configured surface outlets

Optional boundary outlets remove surface water above a specified terrain-datum crest, bounded by available storage and a prescribed capacity. Discharge is tracked separately in water budgets, comparisons and CSV. Existing closed scenarios are unchanged. The example configuration, validation rules and command are documented in [surface outlets](docs/weather/surface-outlets-v1.md).

### Outlet playback and discharge charts

Select **Load outlet scenario** to open the bundled C++-generated outlet run. Orange markers locate configured outlet cells. The discharge panel follows playback and shows cumulative discharge in m³ and interval-average flow in m³/s. Rate values use actual elapsed times between snapshots; the initial frame has no preceding interval. These are snapshot averages, not instantaneous hydraulic discharge. Both charts mark the selected time.

Regenerate the fixture with:

```sh
python3 tools/build_viewer_example.py --build-dir build --scenario examples/hydrology-outlet-scenario.json --output viewer/public/outlet/engine-weather.json --timeline-only
```

CTest verifies outlet fixture regeneration. The bundled case isolates external discharge by disabling infiltration, evaporation and soil drainage; local rain and terrain remain synthetic.

## One-command verification

```sh
python3 tools/verify_pipeline.py
```

This configures/builds C++, runs CTest and fixture checks, runs Python tests, installs locked viewer dependencies, and runs viewer tests plus its production build. Native dependencies must already be installed. The runner stops at the first failure; `--dry-run` only prints commands. CI uses the same runner for its C++/Python and viewer jobs. See [prerequisites, dependency prefixes and partial modes](docs/VERIFY_PIPELINE.md). All examples remain synthetic; AI and live-provider work are excluded.
