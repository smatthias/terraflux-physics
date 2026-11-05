// The field layers of the explorer, grouped by topic for the folding sidebar, each with a short subtitle, a
// tooltip that says what it shows and where it comes from, and the data it needs.
const has = path => data => path.split('.').reduce((o, k) => (o == null ? undefined : o[k]), data) != null

export const LAYER_GROUPS = [
  {
    id: 'air', title: 'Weather & air', tooltip: 'Temperature, wind, pressure and moisture of the air near the ground.',
    layers: [
      { key: 'temperature', title: 'Air temperature', subtitle: 'Dry transport · °C', tooltip: 'Air temperature near the ground, carried by the wind and smoothed by diffusion. With a terrain grid it can be corrected by the lapse rate (colder up high).', needs: d => !d.coast && !d.solar && !d.dust && !d.solarWind },
      { key: 'wind', title: 'Wind speed', subtitle: 'Prescribed field · m/s', tooltip: 'Wind speed near the ground. In coupled runs it follows from the pressure pattern, the Coriolis force and surface friction; arrows show the direction.', needs: d => !d.solar && !d.dust && !d.solarWind },
      { key: 'pressure', title: 'Sea-level pressure', subtitle: 'Synthetic regional field · hPa', tooltip: 'Air pressure reduced to sea level. Lows (blue) bring wind and weather, highs (red) calm and dry conditions.', needs: d => !!(d.atmosphere || d.coupled) },
      { key: 'humidity', title: 'Relative humidity', subtitle: 'Synthetic regional field · %', tooltip: 'How close the air is to saturation. At 100 % water vapour condenses into cloud.', needs: d => !!(d.atmosphere || d.coupled) },
      { key: 'density', title: 'Air density', subtitle: 'Coupled moist layer · kg/m³', tooltip: 'Mass of a cubic metre of air, from pressure, temperature and humidity. Warm, moist air is lighter and accelerates faster under the same pressure gradient.', needs: has('coupled') },
      { key: 'moisture', title: 'Specific humidity', subtitle: 'Coupled moist layer · g/kg', tooltip: 'Grams of water vapour per kilogram of air, carried with the wind.', needs: has('coupled') },
      { key: 'cloud', title: 'Cloud cover', subtitle: 'Synthetic fraction · %', tooltip: 'Share of the sky covered by cloud in the synthetic regional illustration (not engine output).', needs: has('atmosphere') },
    ],
  },
  {
    id: 'rain', title: 'Clouds & rain', tooltip: 'Where clouds form, how unstable the air is, and what falls out of the clouds.',
    layers: [
      { key: 'rainfall', title: 'Rainfall accumulation', subtitle: 'Observation-derived · mm', tooltip: 'Rain summed since the start, estimated from radar reflectivity; missing coverage stays empty rather than dry.', needs: has('rain') },
      { key: 'cape', title: 'Convective energy (CAPE)', subtitle: 'Surface-parcel column · J/kg', tooltip: 'Energy a rising surface parcel gains from buoyancy: the fuel of thunderstorms. Above about 1000 J/kg deep convection is possible.', needs: has('coupled.columns') },
      { key: 'cloudbase', title: 'Cloud base (LCL)', subtitle: 'Surface-parcel column · m', tooltip: 'Height at which rising surface air becomes saturated and clouds start (lifting condensation level).', needs: has('coupled.columns') },
      { key: 'cloudtop', title: 'Equilibrium level', subtitle: 'Surface-parcel column · m', tooltip: 'Height where a rising parcel stops being buoyant: roughly the top of convective clouds.', needs: has('coupled.columns') },
      { key: 'clouds', title: 'Cloud formation', subtitle: 'Fronts + convection · type', tooltip: 'Cloud genus from fronts (warm, cold, stationary) and heated-surface convection: stratus to cumulonimbus.', needs: has('clouds') },
      { key: 'precip', title: 'Precipitation rate', subtitle: 'Cloud outcomes · mm/h', tooltip: 'Rain, drizzle, snow or sleet falling now, from the cloud type, the condensation and the wet-bulb temperature.', needs: has('precipitation') },
      { key: 'accum', title: 'Interval accumulation', subtitle: 'Cloud outcomes · mm', tooltip: 'Precipitation collected over the interval between two snapshots.', needs: has('precipitation') },
    ],
  },
  {
    id: 'storms', title: 'Storms & severe weather', tooltip: 'Tropical cyclones, storm surge and the ingredients for tornadoes.',
    layers: [
      { key: 'stormwind', title: 'Storm wind field', subtitle: 'Tropical cyclones · m/s', tooltip: 'Wind of the tropical cyclones (Holland profile) around their centres.', needs: has('storms') },
      { key: 'mpi', title: 'Potential intensity', subtitle: 'Ocean energy bound · m/s', tooltip: 'The strongest wind a cyclone could reach over this sea, set by the sea-surface temperature and the air above (Emanuel).', needs: has('storms') },
      { key: 'gpi', title: 'Genesis potential', subtitle: 'Emanuel–Nolan index', tooltip: 'How favourable the environment is for a new tropical cyclone to form.', needs: has('storms') },
      { key: 'surge', title: 'Storm surge', subtitle: 'Coastal cells · m', tooltip: 'Water pushed onto the coast by a cyclone\'s wind and low pressure.', needs: has('storms') },
      { key: 'stp', title: 'Tornado parameter', subtitle: 'Significant tornado parameter', tooltip: 'Combination of instability, low-level shear, helicity and cloud base: above 1 strong tornadoes become possible.', needs: has('severe') },
      { key: 'helicity', title: 'Storm-relative helicity', subtitle: '0–1 km · m²/s²', tooltip: 'Spin in the lowest kilometre that a storm can tilt into rotation (supercells).', needs: has('severe') },
    ],
  },
  {
    id: 'terrain', title: 'Terrain & mountains', tooltip: 'The ground and how mountains and valleys shape the weather.',
    layers: [
      { key: 'terrain', title: 'Terrain elevation', subtitle: 'Surface geometry · m', tooltip: 'Height of the ground (negative: sea bed) relative to the grid\'s vertical datum.', needs: has('terrain') },
      { key: 'slope', title: 'Terrain slope', subtitle: 'Inclination · degrees', tooltip: 'Steepness of the ground, from the elevation differences to the neighbouring cells.', needs: has('terrainDerivatives') },
      { key: 'aspect', title: 'Downhill aspect', subtitle: 'Bearing from north · degrees', tooltip: 'Direction the slope faces (downhill), clockwise from north; undefined on flat ground.', needs: has('terrainDerivatives') },
      { key: 'lifting', title: 'Orographic lifting', subtitle: 'V·∇h · m/s (− = descent)', tooltip: 'Vertical motion forced by wind blowing up (positive) or down (negative) the terrain.', needs: has('orography') },
      { key: 'blocked', title: 'Blocked fraction', subtitle: 'Froude blocking · 0–1', tooltip: 'Share of the flow that cannot climb the mountain and is blocked or goes around it (low Froude number).', needs: has('orography') },
      { key: 'foehn', title: 'Foehn warming', subtitle: 'Lee anomaly · K', tooltip: 'Warming on the lee side: air that rained out on the way up descends dry and warms by about 1 K per 100 m.', needs: has('orography') },
      { key: 'coldpool', title: 'Cold-air pool', subtitle: 'Valley deficit · K', tooltip: 'Cold air collected in valleys and basins on calm, clear nights.', needs: has('orography') },
      { key: 'oroprecip', title: 'Orographic precipitation', subtitle: 'Windward condensation · mm/h', tooltip: 'Rain and snow squeezed out of moist air forced up the windward slopes.', needs: has('orography') },
      { key: 'valley', title: 'Valley depth', subtitle: 'Below surroundings · m', tooltip: 'How far a cell lies below its surroundings: where cold air and fog collect.', needs: has('orography') },
    ],
  },
  {
    id: 'land', title: 'Water on land', tooltip: 'Where rain goes on land: surface water, soils, groundwater, snow and rivers.',
    layers: [
      { key: 'water', title: 'Surface water depth', subtitle: 'Closed-domain routing · mm', tooltip: 'Water standing or flowing on the ground in the surface-water prototype.', needs: has('surfaceWater') },
      { key: 'soil', title: 'Soil water storage', subtitle: 'Infiltrated water · mm', tooltip: 'Water soaked into the soil, up to the storage capacity.', needs: has('surfaceWater.soil_values') },
      { key: 'discharge', title: 'River discharge', subtitle: 'Catchment routing · m³/s', tooltip: 'Water flowing through the river network, routed from cell to cell down the drainage directions.', needs: has('hydrology') },
      { key: 'surfacewater', title: 'Surface / flood water', subtitle: 'Overland + floodplain · mm', tooltip: 'Overland flow and water spilled onto the floodplain from rivers above bankfull.', needs: has('hydrology') },
      { key: 'saturation', title: 'Soil saturation', subtitle: 'Regolith store · 0–1', tooltip: 'How full the soil is: saturated soils cannot take more rain, so it runs off.', needs: has('hydrology') },
      { key: 'watertable', title: 'Water-table depth', subtitle: 'Unconfined aquifer · m', tooltip: 'Depth to the groundwater below the surface; shallow water tables feed springs and baseflow.', needs: has('hydrology') },
      { key: 'snowpack', title: 'Snowpack', subtitle: 'Water equivalent · mm', tooltip: 'Water stored as snow, released when it melts.', needs: has('hydrology') },
    ],
  },
  {
    id: 'hazards', title: 'Land hazards', tooltip: 'What heavy rain does to the land: floods, erosion and landslides.',
    layers: [
      { key: 'erosion', title: 'Erosion', subtitle: 'Cumulative stream power · mm', tooltip: 'Ground removed by fast-flowing water (stream power).', needs: has('hydrology') },
      { key: 'deposition', title: 'Deposition', subtitle: 'Cumulative sediment · mm', tooltip: 'Sediment dropped where the flowing water slows down.', needs: has('hydrology') },
      { key: 'stability', title: 'Slope factor of safety', subtitle: 'Infinite slope · FS', tooltip: 'Resistance of a slope against sliding divided by the driving force: below 1 a landslide is likely.', needs: has('hydrology') },
      { key: 'floodclass', title: 'Flood hazard class', subtitle: '0 none → 4 extreme', tooltip: 'Danger of flood water to people from its depth and velocity.', needs: has('hydrology') },
    ],
  },
  {
    id: 'ocean', title: 'Ocean currents', tooltip: 'The streams of the upper ocean, its salt and the sinking that drives the overturning.',
    layers: [
      { key: 'current', title: 'Current speed', subtitle: 'Ocean layer · m/s', tooltip: 'Speed of the upper-ocean current driven by the wind and by density differences.', needs: has('ocean') },
      { key: 'surfacesalinity', title: 'Surface salinity', subtitle: 'Mixed layer · psu', tooltip: 'Salt in the surface water: rivers, rain and meltwater freshen it, evaporation makes it saltier.', needs: has('ocean') },
      { key: 'salinity', title: 'Layer salinity', subtitle: 'Upper-ocean bulk · psu', tooltip: 'Mean salinity of the whole upper ocean layer, not just its surface.', needs: has('ocean') },
      { key: 'sst', title: 'Layer temperature', subtitle: 'Upper ocean · °C', tooltip: 'Mean temperature of the whole upper ocean layer above the thermocline.', needs: has('ocean') },
      { key: 'thermocline', title: 'Thermocline depth', subtitle: 'Layer thickness · m', tooltip: 'Thickness of the warm upper layer above the cold deep water.', needs: has('ocean') },
      { key: 'freshwater', title: 'Freshwater fraction', subtitle: 'Surface dilution · %', tooltip: 'Share of fresh water in the surface water: the river and meltwater plumes.', needs: has('ocean') },
      { key: 'sinking', title: 'Deep-water formation', subtitle: 'Sinking rate · m/day', tooltip: 'Where cold, salty surface water becomes dense enough to sink: the engine of the overturning circulation.', needs: has('ocean') },
    ],
  },
  {
    id: 'oceanheat', title: 'Ocean heat & rain', tooltip: 'Heat and water exchanged between the sea and the air, and the layered water column.',
    layers: [
      { key: 'oceansst', title: 'Sea-surface temperature', subtitle: 'Top ocean layer · °C', tooltip: 'Temperature of the top layer of the water column.', needs: has('ocean.thermal') },
      { key: 'mld', title: 'Mixed-layer depth', subtitle: 'Density criterion · m', tooltip: 'Depth to which the surface water is stirred by wind and cooling.', needs: has('ocean.thermal') },
      { key: 'netheat', title: 'Net air–sea heat flux', subtitle: 'Into the ocean · W/m²', tooltip: 'Heat the sea gains (positive) or loses: sunshine minus longwave, sensible and evaporative losses.', needs: has('ocean.airSea') },
      { key: 'latent', title: 'Latent heat flux', subtitle: 'Evaporative cooling · W/m²', tooltip: 'Heat taken out of the sea by evaporation (negative = cooling).', needs: has('ocean.airSea') },
      { key: 'evaporation', title: 'Sea evaporation', subtitle: 'Bulk formula · mm/day', tooltip: 'Water evaporating from the sea surface, strongest under dry, windy air over warm water.', needs: has('ocean.airSea') },
      { key: 'searain', title: 'Rain on sea and coast', subtitle: 'Rate · mm/h', tooltip: 'Rain falling on the sea (a fresh surface lens) and on the coast (running off into the sea).', needs: has('ocean.rain') },
      { key: 'coastalstore', title: 'Coastal runoff store', subtitle: 'Draining to the sea · mm', tooltip: 'Rain water on the coastal land still draining to the sea.', needs: has('ocean.rain') },
    ],
  },
  {
    id: 'polar', title: 'Polar ice', tooltip: 'Ice sheets and sea ice, and how their melt changes the ocean.',
    layers: [
      { key: 'icethickness', title: 'Ice sheet thickness', subtitle: 'Greenland · m', tooltip: 'Thickness of the land ice sheet (Greenland-like), flowing under its own weight.', needs: d => !!(d.polar || d.ocean?.iceSheet) },
      { key: 'icesmb', title: 'Ice surface mass balance', subtitle: 'Snowfall − melt · m ice/yr', tooltip: 'Snow gained minus melt lost at the ice surface: negative means the surface melts faster than it snows.', needs: d => !!(d.polar || d.ocean?.iceSheet) },
      { key: 'icemelt', title: 'Ice surface melt', subtitle: 'Degree days · m ice/yr', tooltip: 'Surface melt of the ice sheet from positive degree days (warm hours above 0 °C).', needs: d => !!(d.polar || d.ocean?.iceSheet) },
      { key: 'seaice', title: 'Sea-ice concentration', subtitle: 'Ice-covered fraction · %', tooltip: 'Share of the sea surface covered by sea ice (0-100 %).', needs: has('ocean.seaIce') },
      { key: 'seaicethick', title: 'Sea-ice thickness', subtitle: 'Over the covered part · m', tooltip: 'Thickness of the sea ice over the ice-covered part of the cell.', needs: has('ocean.seaIce') },
      { key: 'oxygen', title: 'Surface oxygen capacity', subtitle: 'O₂ saturation · µmol/kg', tooltip: 'How much oxygen the surface water can hold: less in warmer and saltier water, a stressor for marine life.', needs: has('ocean.oxygen') },
    ],
  },
  {
    id: 'sun', title: 'Sun & space weather', tooltip: 'The solar disk of the sunspot cycle (spots, faculae and flaring regions on an illustrative day), the solar wind at the polar caps (aurora, escaping ions, cosmic radiation) and the cosmic dust entering the Earth.',
    layers: [
      { key: 'sunbrightness', title: 'Photosphere brightness', subtitle: 'Solar disk · K', tooltip: 'Brightness temperature of the visible solar surface: limb darkening toward the edge, dark sunspots, slightly brighter faculae. One illustrative day of the month.', needs: has('solar') },
      { key: 'suncontrast', title: 'Contrast to the quiet Sun', subtitle: 'Spots − / faculae + · %', tooltip: 'Intensity against the same cell of a spotless Sun: sunspots remove light, faculae add a little. Integrated over the disk the faculae usually win, so the Sun is brighter at maximum.', needs: has('solar') },
      { key: 'windaurora', title: 'Aurora (particle energy)', subtitle: 'Storm hour · mW/m²', tooltip: 'Energy carried into the upper atmosphere by electrons and ions raining down along the auroral oval, the ring around each magnetic pole where the field lines reach into the magnetotail. It spreads towards the equator in storms.', needs: has('solarWind') },
      { key: 'windoutflow', title: 'Escaping oxygen ions', subtitle: 'O⁺ upflow · 10⁷ ions/cm²/s', tooltip: 'Oxygen ions lifted out of the ionosphere along open and stretched field lines: the cleft ion fountain at the cusp (noon) and the night-side oval. Part of it leaves the Earth for good. Oxygen cannot escape any other way.', needs: has('solarWind') },
      { key: 'winddose', title: 'Cosmic radiation at 12 km', subtitle: 'Flight altitude · µSv/h', tooltip: 'Effective dose rate from galactic cosmic rays (and solar protons) at cruising altitude. The field shields the equator (cutoff about 14 GV); over the poles almost nothing is held back.', needs: has('solarWind') },
      { key: 'windsep', title: 'Solar-proton ionisation', subtitle: 'Column · 10⁹ ion pairs/cm²/s', tooltip: 'Ion pairs made by solar energetic protons in the whole atmospheric column. They only reach the polar caps (polar cap absorption), where they make NOx that destroys ozone.', needs: has('solarWind') },
      { key: 'windcutoff', title: 'Cutoff rigidity', subtitle: 'Magnetic shielding · GV', tooltip: 'The lowest rigidity (momentum per charge) a charged particle needs to reach the atmosphere here from straight above: high at the magnetic equator, near zero at the poles; lowered by the ring current in storms.', needs: has('solarWind') },
      { key: 'windthermo', title: 'Thermosphere temperature', subtitle: 'Exosphere · K', tooltip: 'Temperature of the upper thermosphere (Jacchia): set by the Sun\'s EUV with a day-night cycle, and raised in storms by Joule heating and particles, most at high latitudes. A hotter thermosphere expands: more drag on satellites, more hydrogen escape.', needs: has('solarWind') },
      { key: 'windregion', title: 'Open and closed field', subtitle: 'Polar cap · oval · cusp', tooltip: 'Where the field lines are open to the solar wind (polar cap), where they reach the magnetotail (auroral oval), the cusp near noon where the solar wind enters, and the closed, shielded field at lower latitudes.', needs: has('solarWind') },
      { key: 'dustinflux', title: 'Cosmic dust influx', subtitle: 'This hour · µg/m²/day', tooltip: 'Mass of interplanetary dust and small meteoroids entering the atmosphere per square metre, as a daily rate at this hour: the side of the Earth facing its direction of motion (local morning) sweeps up more.', needs: has('dust') },
      { key: 'dustdaily', title: 'Cosmic dust, daily mean', subtitle: 'Over the day · µg/m²/day', tooltip: 'The influx averaged over the day: its variation with latitude follows the position of the apex of the Earth\'s way in the sky, which changes with the seasons.', needs: has('dust') },
    ],
  },
  {
    id: 'carbon', title: 'Carbon & ocean life', tooltip: 'CO₂ in the air and the sea: the ocean\'s uptake, acidification, oxygen, and the dead zones where sea life cannot live.',
    layers: [
      { key: 'co2air', title: 'CO₂ in the air', subtitle: 'Boundary layer · ppm', tooltip: 'CO₂ of the air near the ground over land and sea: the scenario background, raised by cities and industry, carried by the wind, drawn down by plants in daylight and by the sea.', needs: has('carbon') },
      { key: 'co2flux', title: 'Air–sea CO₂ flux', subtitle: 'Into the sea · mol/m²/yr', tooltip: 'CO₂ the sea takes up (positive) or releases (negative): the gas transfer with the wind times the pCO₂ difference between air and water.', needs: has('carbon') },
      { key: 'ph', title: 'Surface pH', subtitle: 'Total scale · acidification', tooltip: 'Acidity of the surface water from its carbonate chemistry: the CO₂ the sea takes up lowers it (about 8.05 today, 7.7 at 1000 ppm).', needs: has('carbon') },
      { key: 'omega', title: 'Surface aragonite saturation', subtitle: 'Ω · shells dissolve below 1', tooltip: 'How saturated the surface water is in aragonite, the mineral of shells and corals: below 1 the water is corrosive.', needs: has('carbon') },
      { key: 'omegabottom', title: 'Bottom aragonite saturation', subtitle: 'Ω at the seafloor', tooltip: 'Aragonite saturation of the deepest water: respiration at depth adds CO₂, so bottom water under dead zones turns corrosive first.', needs: has('carbon') },
      { key: 'bottomo2', title: 'Bottom-water oxygen', subtitle: 'Dissolved O₂ · µmol/kg', tooltip: 'Oxygen at the seafloor (or 600 m in deep water). Below 60 µmol/kg (about 2 mg/L) the water is hypoxic: a dead zone.', needs: has('carbon') },
      { key: 'habitat', title: 'Habitat of the column', subtitle: '0 habitable → 3 anoxic', tooltip: 'The worst level of the water column: 0 habitable, 1 stressed (corrosive or too little oxygen for the temperature), 2 hypoxic, 3 anoxic.', needs: has('carbon') },
      { key: 'habitable', title: 'Habitable share of the column', subtitle: 'Volume · %', tooltip: 'Share of the water column (to the seafloor or 600 m) with enough oxygen, not corrosive and not metabolically stressed.', needs: has('carbon') },
      { key: 'production', title: 'Plankton production', subtitle: 'Net · g C/m²/day', tooltip: 'Net production of the plankton in the sunlit water, fed by nitrate: rivers and winter mixing supply it; what sinks is respired at depth.', needs: has('carbon') },
      { key: 'nitrate', title: 'Surface nitrate', subtitle: 'Nutrient · µmol/kg', tooltip: 'Nitrate at the surface: river fertiliser near the mouths, used up by the plankton elsewhere.', needs: has('carbon') },
    ],
  },
  {
    id: 'coast', title: 'Sea level & coasts', tooltip: 'What a rising sea does to the coast: land lost, storm floods, defences, rivers and islands.',
    layers: [
      { key: 'coastlost', title: 'Land lost to the sea', subtitle: 'Share of the cell · %', tooltip: 'Land permanently under the daily high water (100 %) or eroded from sandy shores (partial) compared with today, in the map scenario.', needs: has('coast') },
      { key: 'coastyear', title: 'Year the land is lost', subtitle: 'Scenario in focus · year', tooltip: 'The first year at least half of each cell is lost, for the scenario in focus. Empty: not within the run.', needs: has('coast') },
      { key: 'coaststorm', title: 'Design-storm flood depth', subtitle: '100-year storm · m', tooltip: 'Water depth on land in the design storm: the 100-year surge on top of high tide and the risen sea, plus the wind setup, losing height inland.', needs: has('coast') },
      { key: 'coastinundation', title: 'Permanent inundation', subtitle: 'Depth at high water · m', tooltip: 'Depth of the sea at daily high water over land that is newly and permanently flooded.', needs: has('coast') },
      { key: 'coastfrequency', title: "Today's 100-year flood", subtitle: 'Now every … years', tooltip: 'How often the flood that comes once a century today arrives at each coastal cell, given its relative sea-level rise (regional rise plus land subsidence).', needs: has('coast') },
      { key: 'coastsetup', title: 'Wind setup', subtitle: 'Design storm · m', tooltip: 'Water piled up against the coast by the design storm\'s onshore wind; largest over shallow shelves and in funnel-shaped estuaries.', needs: has('coast') },
      { key: 'coastsubsidence', title: 'Land subsidence', subtitle: 'Sinking · mm/yr', tooltip: 'How fast the land sinks (deltas, drained peat, groundwater pumping): it adds to the rise of the sea.', needs: has('coast') },
      { key: 'coastpopulation', title: 'Population', subtitle: 'People per km²', tooltip: 'Where people live: it turns flooded land into people affected.', needs: has('coast') },
    ],
  },
]

export const LAYERS = LAYER_GROUPS.flatMap(g => g.layers.map(l => ({ ...l, group: g.id })))

export const groupOfLayer = key => LAYERS.find(l => l.key === key)?.group ?? null

export function layerAvailable(key, data) {
  const layer = LAYERS.find(l => l.key === key)
  return !!layer && !!layer.needs(data ?? {})
}

// The subtitle under the layer name; a few layers describe a different source depending on the loaded data.
export function layerSubtitle(layer, data) {
  if (data?.atmosphere && layer.key === 'temperature') return 'Synthetic regional field · °C'
  if (data?.coupled) {
    if (layer.key === 'temperature') return 'Coupled moist layer · °C'
    if (layer.key === 'wind') return 'Pressure · Coriolis · friction · m/s'
    if (layer.key === 'pressure') return 'Prescribed static field · hPa'
    if (layer.key === 'humidity') return 'Diagnosed from T, q, p · %'
  }
  return layer.subtitle
}

// The example datasets, grouped the same way, each with a tooltip.
export const EXAMPLE_GROUPS = [
  { id: 'earth', title: 'Coupled Earth', examples: [
    { action: 'openEarthForecast', label: '🌍 Open the coupled 10-day forecast', tooltip: 'Ocean, land, mountains, the Sun and a hurricane coupled to the weather: a 10-member ensemble forecast with 3/5/7/10-day products, a 3-day wind forecast and severe-weather warnings (Coupled Earth workspace).' },
    { action: 'openEarthClimate', label: '🌡 Open climate drivers & CO₂ impact', tooltip: 'Which drivers change the climate most (CO₂, methane, aerosols, the Sun, volcanoes, space), what the warming does, and where CO₂ comes from, goes and hits hardest (Coupled Earth workspace).' },
  ] },
  { id: 'weather', title: 'Weather', examples: [
    { action: 'loadEngineExample', label: '→ Load engine example', tooltip: 'Temperature transport, radar rainfall and surface water on a small synthetic grid: wet weather, then drying.' },
    { action: 'loadCoupledExample', label: '⇶ Load coupled wind example', tooltip: 'Wind from pressure systems with Coriolis and friction, coupled to temperature, humidity and air density.' },
    { action: 'loadCloudExample', label: '⛅ Load cloud formation example', tooltip: 'Fronts, frontal and convective clouds, afternoon thunderstorms and what they rain.' },
    { action: 'loadHurricaneExample', label: '🌀 Load hurricane landfall example', tooltip: 'Two tropical cyclones merge, intensify over warm water, make landfall with a storm surge and decay inland.' },
    { action: 'loadAlpsExample', label: '⛰ Load Alps foehn example', tooltip: 'Southerly flow over an Alpine ridge: rain on the windward side, foehn in the lee, cold pools in the valleys.' },
    { action: 'useCloudMock', label: '⛈ Cloud formation mock (synthetic)', tooltip: 'A hand-built mock in the cloud-formation format; not engine output.' },
    { action: 'useRegionalDemo', label: '☁ Regional weather demo', tooltip: 'An analytic illustration of pressure systems, a front and cloud bands; not engine output.' },
  ] },
  { id: 'water', title: 'Water & hazards on land', examples: [
    { action: 'loadOutletExample', label: '↘ Load outlet scenario', tooltip: 'Surface water draining through a prescribed outlet.' },
    { action: 'loadComparisonDemo', label: '⇄ Compare hydrology scenarios', tooltip: 'The same rain on soils that take ten times less water: a side-by-side comparison.' },
    { action: 'loadFloodExample', label: '🌊 Load alpine flood example', tooltip: 'Two days of rain and a cloudburst on a pre-alpine range: rivers, lakes, groundwater, floods, erosion and landslides.' },
  ] },
  { id: 'ocean', title: 'Ocean & ice', examples: [
    { action: 'loadOceanExample', label: '🌐 Load Atlantic streams example', tooltip: 'A Gulf-Stream-like current, river plumes and deep-water formation that a meltwater pulse switches off.' },
    { action: 'loadOceanHeatExample', label: '🌧 Load ocean heat & rain example', tooltip: 'Rain on the sea and the coast, evaporation, air–sea heat exchange and a layered water column.' },
    { action: 'loadPolarExample', label: '🧊 Load polar melt & overturning example', tooltip: 'Greenland, the Arctic sea ice and the Atlantic overturning under warming scenarios to 2300: when the currents collapse.' },
    { action: 'loadPolarOceanExample', label: '❄ Load polar ocean (sea ice & hosing) example', tooltip: 'Ten years of a layered ocean with drifting sea ice, a melting ice sheet, warming and meltwater hosing.' },
  ] },
  { id: 'carbon', title: 'Carbon & ocean life', examples: [
    { action: 'loadCarbonExample', label: '🫧 Load ocean carbon & dead zones (today)', tooltip: 'A Gulf-like sea under today\'s 427 ppm: city and refinery CO₂ plumes, the sea taking up CO₂, a delta river whose nitrate feeds a summer dead zone.' },
    { action: 'loadCarbon2100Example', label: '🌡 Load ocean carbon & dead zones (2100)', tooltip: 'The same sea in 2100 under high emissions (about 1,010 ppm, 3 K warmer): more acid, less shell-friendly, larger dead zones.' },
  ] },
  { id: 'space', title: 'Sun & space weather', examples: [
    { action: 'loadSolarExample', label: '☀ Load solar cycle & space weather example', tooltip: 'The 11-year sunspot cycle since 1850 with its minima and maxima, a tested forecast to 2050, and what the cycle does to UV, ozone, flares and the climate.' },
    { action: 'loadWindExample', label: '🧲 Load solar wind & polar caps example', tooltip: 'A severe geomagnetic storm at the March equinox: the solar wind, the polar caps and the aurora, the ions escaping to space, cosmic radiation and solar protons over the poles, and what it means globally.' },
    { action: 'loadDustExample', label: '☄ Load cosmic dust & extraterrestrial water example', tooltip: 'The 30-100 t of dust and small meteoroids entering the Earth every day: what burns up, what reaches the ground, where the extraterrestrial water goes, and whether it matters.' },
  ] },
  { id: 'coast', title: 'Sea level & coasts', examples: [
    { action: 'loadCoastExample', label: '🏝 Load sea-level rise & coasts example', tooltip: 'The sea-level rise of the polar-melt scenarios on a coast with a delta, an estuary city, barrier islands, cliffs and low islands: land lost, storm floods, defences, rivers.' },
  ] },
]
