# Cosmic dust and extraterrestrial water v1: what enters the atmosphere every day, and how much it matters

Every day tens of tonnes of interplanetary dust and small meteoroids enter the Earth's atmosphere. The estimates run from about 5 to 300 t/day; **30–100 t/day** is the range usually quoted. Most of it comes from comets and asteroid collisions. The grains are mostly 50–500 µm across and arrive at 11–72 km/s. A few per cent of the mass is water: bound in hydrated minerals, or implanted by the solar wind in the grains' rims.

`terraflux-run-cosmic-dust` maps this into TerraFlux:

- **Entry.** What evaporates, at which height, what reaches the ground, and in what state (unmelted, partly melted, cosmic spherule).
- **Extraterrestrial water.** Where it is released, and how it compares with the water the Earth already has and the water it loses.
- **Budgets.** The Earth's mass, energy, iron and meteoric smoke, against their terrestrial counterparts. Also "what would it take" for the dust to matter for the climate.
- **Map.** Where and when the dust arrives over one day, from the sporadic meteoroid sources on the sky.

Kernel: `include/terraflux/weather/cosmic_dust.hpp`. Request: `terraflux.cosmic.dust-run/v1` ([schema](cosmic-dust-run-v1.schema.json), [example](../../examples/cosmic-dust-request.json)). The output is a viewer timeline with a `cosmic_dust` block (`terraflux.cosmic.dust/v1`, [block schema](cosmic-dust-v1.schema.json)). In the viewer it is the **Cosmic dust** view. To load it, use **Sun & space weather → ☄ Load cosmic dust & extraterrestrial water example**.

```sh
build/terraflux-run-cosmic-dust examples/cosmic-dust-request.json /tmp/dust.json
python3 tools/build_ocean_example.py --build-dir build --cli terraflux-run-cosmic-dust \
    --request examples/cosmic-dust-request.json --output viewer/public/dust/engine-timeline.json
```

**The short answer.** At 60 t/day the dust is:

- **Negligible for the weather and the climate:** at most ±0.006 W/m² and ±0.005 K.
- **Negligible for the water cycle:** about 2·10⁻¹² of the rain.
- **Smaller than the Earth's own losses:** the Earth loses about four times more mass to space than it gains (hydrogen and helium escape), and hundreds of times more water (as escaping hydrogen) than the dust brings.

It does matter for the upper atmosphere. It is the source of the metal layers (Na, Fe, Mg at 80–105 km) and of meteoric smoke. It adds a small but measurable share of the water above 80 km. And it leaves micrometeorites on the ground, about 6,000 t a year.

## 1. The sizes: Grün et al. (1985)

The interplanetary flux at 1 AU (`gruen_cumulative_flux`) gives the number of particles heavier than *m* (grams), per square metre and second:

    F(m) = (c4 m^g4 + c5)^g5 + c6 (m + c7 m^g6 + c8 m^g7)^g8 + c9 (m + c10 m^g6)^g9

It sets the **shape** of the mass distribution: 36 logarithmic bins from 10⁻¹² g to 1 g (about 0.1 µm to 1 cm at 2,000 kg/m³). Most of the mass lies between 10⁻⁷ and 10⁻⁴ g (50–500 µm). Integrated over the Earth, the distribution alone gives 21 t/day (`influx.gruen_t_per_day`). Gravitational focusing adds about half again.

The **total** is a scenario input. The request's `influx` gives a central value and a low and high one (the example: 30/60/100 t/day). All the budgets scale linearly with it. The shares (what burns, what survives, where the water goes) do not depend on it.

| Method | Typical estimate | Reference (rounded) |
|---|---|---|
| Spacecraft impact craters (LDEF) | about 110 t/day | Love & Brownlee 1993 |
| Models of the dust sources, checked against meteor radars and metal layers | about 30–45 t/day | Carrillo-Sánchez et al. 2016, 2020 |
| Review of all methods | about 5–270 t/day | Plane 2012 |
| Micrometeorites in Antarctic snow (what reaches the ground) | about 5,200 t/yr (14 t/day) | Rojas et al. 2021 |

## 2. Entry: heating, melting, evaporation

`simulate_dust_entry` follows one homogeneous sphere on a straight path through the US Standard Atmosphere 1976. The density and pressure are log-linear between nodes every 10 km, up to 200 km. The equations are a simplified form of Love & Brownlee (1991):

    m dv/dt   = −Γ ρₐ A v² + m g cos z                                        drag
    c m dT/dt = ½ Λ ρₐ A v³ − ε σ S (T⁴ − Tₐ⁴) − L J S                        air heating, radiation, evaporation
    dm/dt     = −J S,   J = p_v(T) √(μ / 2πRT),   log₁₀ p_v [Pa] = A − B/T      Langmuir evaporation

Here A and S are the cross-section and the surface. The temperature step is implicit (Newton), because micrometeoroids heat and cool within tenths of a second. The particle starts at 180 km. Its run ends when it is slow (< 300 m/s) and cool again, or when less than a millionth of its mass is left.

- **State.** The state follows the peak temperature. Below 1,200 K the particle stays **unmelted**. From 1,200 to 1,650 K it is **partly melted** (scoriaceous). Above 1,650 K it is **melted** (a cosmic spherule). With less than 1 % left, it counts as **vaporised**.
- **Water.** A share of the grain's mass is water. It leaves in two ways. Heating releases it between 700 and 1,100 K: phyllosilicates lose their bound water at about 500–800 °C. Evaporation also takes water with the mass that leaves.
- **Energy.** The kinetic energy lost by the particle (deceleration plus the evaporated mass) is handed to the air in the altitude bin where it is lost.

**Calibration.** The vapour pressure of chondritic melt is the least certain parameter. It decides how much of the mass evaporates. A = 8.7 and B = 13,500 K were chosen (at Λ = 1) so that about a quarter of the incoming mass reaches the ground, in line with the Antarctic collections:

| Quantity (60 t/day) | TerraFlux | Observed (rounded) |
|---|---|---|
| Reaching the ground | 16.7 t/day = 6,100 t/yr | about 5,200 ± 1,500 t/yr of micrometeorites (Rojas et al. 2021) |
| … of which cosmic spherules (melted) | 9.2 t/day = 3,350 t/yr | about 2,700 ± 1,400 t/yr of spherules 50–700 µm at the South Pole (Taylor et al. 1998) |
| Median height of evaporation | 88 km | the metal layers peak near 85–95 km |

**Population.** `simulate_dust_population` combines every mass bin with 8 entry speeds and 4 entry angles (weights given):

- **Speeds.** 11.8–62 km/s, mass-weighted. Most of the mass is slow, from asteroidal and Jupiter-family-comet dust. A tail is fast, from Halley-type and Oort-cloud comets: a rounded version of the source mix of Carrillo-Sánchez et al. (2016).
- **Angles.** 20.7°, 37.8°, 52.2° and 69.3°: the zenith quartiles of an isotropic flux on a horizontal surface.

In the example:

| | t/day | share |
|---|---|---|
| Entering | 60 | 100 % |
| Evaporated (metal atoms, meteoric smoke) | 43.3 | 72 % |
| Reaching the ground melted (cosmic spherules) | 9.2 | 15 % |
| Reaching the ground partly melted | 6.4 | 11 % |
| Reaching the ground unmelted | 1.1 | 2 % |

- **By size.** Particles smaller than about 10 µm survive mostly unmelted: they decelerate high up, where they can radiate the heat away. From about 50 µm most melt. Above about 300 µm most of the mass evaporates.
- **Deposition.** About 12 µg per square metre reaches the ground each year. This includes 1.7 particles larger than 100 µm per square metre and year.
- **Profiles.** The output `profiles` give, per 1 km from 50 to 180 km, the evaporated mass, the released water and the heat given to the air.

## 3. The extraterrestrial water

The example gives the dust 5 % water by mass. This mixes two kinds of material. The water-rich CI/CM-like carbonaceous material carries about 10–20 % water, bound in hydrated minerals. Anhydrous cometary dust carries almost none. Solar-wind hydrogen implanted in the grains' rims makes water as well (Bradley et al. 2014). The fraction is uncertain by a factor of a few.

At 60 t/day:

| Quantity | Value | Compared with | Ratio |
|---|---|---|---|
| Water arriving | 3 t/day = 1,096 t/yr | water lost as escaping hydrogen (3 kg/s of H ≈ 850,000 t/yr of water) | 1.3·10⁻³ |
| Released above 80 km | 2.99 t/day (99.7 %), median 105 km | the water there (3 ppmv, 30-day lifetime) | 9·10⁻⁴ |
| Reaching the ground in cool grains | 8 kg/day | the water arriving | 3·10⁻³ |
| Into the oceans per year | 780 t/yr | the oceans (1.4·10²¹ kg) | 8·10⁻¹⁶ |
| At this rate over 4.5 billion years | | the oceans | 3.5·10⁻⁶ |
| Against the rain | 1,096 t/yr | global precipitation (5·10¹⁷ kg/yr) | 2·10⁻¹² |
| Against volcanic degassing | 1,096 t/yr | volcanic and hydrothermal water (about 1 Gt/yr) | 1·10⁻⁶ |

The water leaves the grains **higher** than the metals (median 105 km against 88 km), because dehydration starts well below the melting point. Up there, sunlight splits it within weeks. It does not reach the troposphere as water.

Today's dust is therefore **not** a source of the oceans. The late accretion of water-rich planetesimals 4 billion years ago is a different question, and it is not modelled here.

**Coupling.** TerraFlux does **not** add the extraterrestrial water to the rain, river and ocean models. It would change their water budgets by about 2·10⁻¹², far below their numerical error. Instead the block exports global per-area fluxes (`deposition_export`, `terraflux.cosmic.deposition/v1`), for any model that wants them:

| Quantity | kg m⁻² s⁻¹ (60 t/day) |
|---|---|
| Dust reaching the ground | 3.8·10⁻¹⁶ |
| Meteoric smoke (evaporated mass) | 9.8·10⁻¹⁶ |
| Water | 6.8·10⁻¹⁷ |
| Iron | 2.5·10⁻¹⁶ |

## 4. The budgets

| Budget | Cosmic dust (60 t/day) | Compared with | Ratio |
|---|---|---|---|
| Mass | +21,900 t/yr | hydrogen (94,700 t/yr) and helium (1,600 t/yr) escaping to space | 23 %: the Earth loses about 74,000 t/yr net |
| Kinetic energy | 0.2 GW = 4·10⁻⁷ W/m² | sunlight (340 W/m² on average) | 1.2·10⁻⁹ |
| | | the Earth's geothermal heat flow (47 TW) | 4·10⁻⁶ |
| Iron | 3,900 t/yr (2,800 into the oceans) | iron in wind-blown desert dust (about 16 Mt/yr) | 2.5·10⁻⁴ |
| Meteoric smoke | 63,000 t aloft, optical depth 2.5·10⁻⁴ | the Pinatubo eruption (optical depth about 0.15) | 1.7·10⁻³ |
| Radiative forcing | at most ±0.006 W/m² | the 11-year solar cycle (about 0.2 W/m²) | 3 % |
| Global temperature (equilibrium) | at most ±0.005 K | the warming since pre-industrial times (about 1.2 K) | 5·10⁻³ |

**Meteoric smoke.** The evaporated metals and silicates recondense into nanometre particles (Hunten et al. 1980). They spend years in the middle atmosphere and are carried down in the polar vortices.

- **Burden.** The evaporated mass times a residence time of 4 years.
- **Optical depth.** The burden times a mass extinction of 2,000 m²/kg, spread over the Earth.
- **Forcing.** At most 25 W/m² per unit optical depth. The **sign is uncertain**: the smoke absorbs as well as scatters. The result is therefore given as a bound (±). The temperature bound uses the climate feedback of the solar model (1.13 W/m²/K).

**What would it take?** The `what_if` table scales the influx by 1, 10, 100, 1,000 and 10,000 (`scenarios.multipliers`). The `thresholds` give the influx that reaches a forcing of 0.1 W/m²: about **16× today** (970 t/day). For 1 W/m² it takes about **160× today** (9,700 t/day). Influxes like that are thought to have happened after the break-up of asteroids. The dust from the break-up of the L-chondrite parent body, 466 million years ago, may have contributed to a cooling in the Ordovician (Schmitz et al. 2019).

**Against the Sun.** If the request names a solar-cycle timeline (`solar_timeline_path`), the block adds `solar_comparison`. The dust's kinetic energy is then compared with two quantities from that run:

- the 11-year cycle's change in sunlight (1.11 W/m², or 0.28 W/m² averaged over the globe): ratio 1.4·10⁻⁶;
- the average energy of solar flares: ratio 0.2 %.

## 5. Where and when it arrives

The sporadic meteoroids come from a few regions of the sky, fixed relative to the **apex** of the Earth's way. The apex lies on the ecliptic, 90° west of the Sun.

**Sources.** `default_meteoroid_sources`, after Jones & Brown (1993) and Campbell-Brown (2008), with rounded weights:

| Source | Position | Weight |
|---|---|---|
| Apex | on the ecliptic | 0.35 |
| Helion | 70° toward the Sun, on the ecliptic | 0.25 |
| Antihelion | 70° away from the Sun, on the ecliptic | 0.25 |
| North toroidal | 55° north of the apex | 0.075 |
| South toroidal | 55° south of the apex | 0.075 |

**Influx at a place.** `relative_dust_influx` computes the relative influx as:

- an isotropic share (0.3, for the spread of the sources), plus
- for each source, the weight times max(0, cos ψ) on a horizontal surface, where ψ is the source's zenith distance.

The map is normalised to a global mean of 1. The positions of the Sun and of the sidereal time come from the low-precision formulas of the Astronomical Almanac.

**Map.** The hourly frames cover one day (the example: 5 November 2026) on a 72 × 36 equirectangular grid (5°).

- **Frames.** Each frame holds the influx per square metre (as a daily rate, scaled to the central total), the daylight, the points where each source stands overhead, and the subsolar point.
- **Daily.** The block also gives the daily mean, the mean by latitude, and the land share.

**Land mask.** It comes from `tools/make_land_mask.py`. The script samples the `global-land-mask` package (MIT; GLOBE 1 km elevation data, NOAA) at the cell centres.

**What it shows.**

- **Morning side.** The apex stands overhead around 6 h local solar time, so the morning side of the Earth receives about **4–7× more** than the evening side.
- **Seasons.** The hemisphere tilted toward the apex gets more, and this changes with the seasons.
- **Land.** In the example, land receives 31 % of the daily influx.
- **Showers.** Meteor showers are **not** included. They bring little mass compared with the sporadic background.

## 6. Request and output

The request sections are checked strictly: unknown fields are rejected.

| Section | Contents |
|---|---|
| `influx` | `central_t_per_day` (required), `low_t_per_day`, `high_t_per_day`, `note` |
| `population` | `min_mass_g`, `max_mass_g`, `mass_bins`; `speeds` as `[km/s, weight]` pairs; `zenith_deg`; `composition` (mass fractions of water, iron, sodium, magnesium, silicon, nickel) |
| `entry` | particle density, heat capacity, Λ, Γ, emissivity, latent heat, vapour pressure A/B and molar mass, melting and dehydration temperatures, start height, air temperature, step length |
| `altitude` | `bottom_km`, `top_km`, `step_km` of the profiles |
| `map` | `date_utc`, `cells_east`, `cells_north`, `hours`, `land_mask` (rows north first, `#` land, `.` sea), `land_mask_source`, `sources`, `isotropic_share` |
| `comparisons` | the terrestrial quantities (see below) |
| `scenarios` | `multipliers` for the what-if table |
| `solar_timeline_path` | optional, a `terraflux-run-solar-cycle` timeline |

The `cosmic_dust` block holds:

- `influx`, `population` (with the bins and their fates), `fate`, `profiles`, `entry`;
- `scenarios` (low, central, high), `what_if`, `thresholds`;
- `comparisons`, `deposition_export`, `map`, `limitations`;
- optionally `solar_comparison`.

The frames are ordinary `terraflux.weather.forecast/v1` snapshots on the global grid, so the timeline plays back like any other. Their air temperature and wind fields are placeholders; the viewer draws the influx from the block.

**Comparison values.** The `comparisons` in the example are rounded literature orders of magnitude. They were entered from general knowledge and not checked against a single source. Replace them if you need specific values.

| Quantity | Example value |
|---|---|
| Hydrogen escape | about 3 kg/s |
| Helium escape | about 50 g/s |
| Oceans | 1.4·10²¹ kg |
| Global precipitation | about 5·10¹⁷ kg/yr |
| Volcanic and hydrothermal water | about 1 Gt/yr |
| Iron in aeolian dust deposition | about 16 Mt/yr |
| Geothermal heat flow | about 47 TW |
| Mesospheric water | 3 ppmv, 30-day lifetime |
| Smoke residence | 4 years |
| Smoke mass extinction | 2,000 m²/kg |
| Forcing per unit optical depth | 25 W/m² |

## 7. Verification

`ctest --test-dir build -R cosmic_dust` runs three tests.

**`weather.cosmic_dust`** (C++):

- **Sizes.** The Grün flux falls with mass, and its total over the Earth lies within 15–30 t/day. The bins add up to 1, and the mass peaks between 10⁻⁷ and 10⁻⁴ g.
- **Atmosphere.** The table's values at sea level, 80 and 100 km, and the density falling with height.
- **Single particles.**
  - A tiny slow particle stays unmelted and heats between 80 and 130 km. A large fast one evaporates completely and releases all its water.
  - For every mass and speed, the ablated and surviving mass add up to 1, the released water sits in the altitude bins, and hot particles lose all their bound water.
  - Faster and larger particles lose more. Grazing entries heat higher up. No particle gives the air more energy than it brought.
  - Without evaporation, no mass is lost.
- **Population.**
  - The budgets close: ablated + surviving = influx, and water released + delivered = water in.
  - About a quarter survives. Ablation happens at 80–100 km, and the water is released high up.
  - The kinetic power equals ½ influx ⟨v²⟩. Everything is linear in the influx.
- **Sky.**
  - The sidereal time at J2000 and the Sun at the March equinox.
  - The apex lies on the ecliptic.
  - The map's global mean is 1, and the side facing the apex receives more than twice the side facing away. An isotropic sky gives 1 everywhere.
- **Input.** Invalid input is rejected.

**`weather.cosmic_dust_cli`** (Python, `tests/tools/cosmic_dust_check.py`):

- **Schemas.** Both the output and the block validate against their schemas.
- **Fate.** About a quarter reaches the ground, and water is released higher than the metals. The fates add up, small particles survive while large ones burn, and no more energy is deposited than brought.
- **Budgets.** The scenarios scale with the influx, and the Earth loses net mass. At today's influx the dust is negligible for the mesosphere's water and for the climate. The what-if forcing rises with the multiplier.
- **Map.** The apex side gets more, and half the Earth is in daylight. The apex lies 60–120° west of the Sun, and the land share is plausible.
- **Solar comparison.** It is added when a solar timeline is named.
- **Input.** Invalid requests are rejected without writing output.

**`weather.viewer_cosmic_dust`** (Node, `viewer/src/cosmicDust.test.js`): the viewer's validation is checked with the committed example (`viewer/public/dust/engine-timeline.json`), along with its rejection of broken blocks, the metrics, shares and scenarios, the budget rows and the number formatting.

## 8. Limitations

- **Total influx.** It is a scenario input. Estimates differ by more than an order of magnitude (5–300 t/day); the Grün distribution only sets the shape.
- **Entry model.** Single-body entry of homogeneous spheres on straight paths. The model leaves out fragmentation, porous or icy particles, and the differential evaporation of the elements (Na leaves before Fe, Mg and Si; Vondrak et al. 2008). The vapour pressure is calibrated to the micrometeorite collections, not derived from chemistry.
- **Water content.** Uncertain by a factor of a few. The release is a simple temperature ramp plus loss with the evaporated mass.
- **Meteoric smoke.** A burden estimate (ablation × residence time), not a transport model. Its optical properties and the sign of its forcing are uncertain, so the results are bounds.
- **Sky model.** Point sources stand for the sporadic meteoroid environment. Meteor showers, the speed of each source, the gravitational focusing by latitude and the seasonal change of the source strengths are left out.
- **Comparisons.** The comparison quantities are rounded literature orders of magnitude; see section 6.
- **Scope.** Large impacts (metre-sized and up), which deliver most of the mass over long times in rare events, are outside the model. So is the early bombardment.

## References

- Bradley, J. P. et al. (2014): Detection of solar wind-produced water in irradiated rims on silicate minerals. PNAS 111, 1732–1735.
- Campbell-Brown, M. D. (2008): High resolution radiant distribution and orbits of sporadic radar meteoroids. Icarus 196, 144–163.
- Carrillo-Sánchez, J. D. et al. (2016): Sources of cosmic dust in the Earth's atmosphere. Geophys. Res. Lett. 43, 11,979–11,986.
- Carrillo-Sánchez, J. D. et al. (2020): Cosmic dust fluxes in the atmospheres of Earth, Mars, and Venus. Icarus 335, 113395.
- Grün, E., Zook, H. A., Fechtig, H., Giese, R. H. (1985): Collisional balance of the meteoritic complex. Icarus 62, 244–272.
- Hunten, D. M., Turco, R. P., Toon, O. B. (1980): Smoke and dust particles of meteoric origin in the mesosphere and stratosphere. J. Atmos. Sci. 37, 1342–1357.
- Jones, J., Brown, P. (1993): Sporadic meteor radiant distributions: orbital survey results. Mon. Not. R. Astron. Soc. 265, 524–532.
- Love, S. G., Brownlee, D. E. (1991): Heating and thermal transformation of micrometeoroids entering the Earth's atmosphere. Icarus 89, 26–43.
- Love, S. G., Brownlee, D. E. (1993): A direct measurement of the terrestrial mass accretion rate of cosmic dust. Science 262, 550–553.
- Plane, J. M. C. (2012): Cosmic dust in the Earth's atmosphere. Chem. Soc. Rev. 41, 6507–6518.
- Rojas, J. et al. (2021): The micrometeorite flux at Dome C (Antarctica), monitoring the accretion of extraterrestrial dust on Earth. Earth Planet. Sci. Lett. 560, 116794.
- Schmitz, B. et al. (2019): An extraterrestrial trigger for the mid-Ordovician ice age: Dust from the breakup of the L-chondrite parent body. Sci. Adv. 5, eaax4184.
- Taylor, S., Lever, J. H., Harvey, R. P. (1998): Accretion rate of cosmic spherules measured at the South Pole. Nature 392, 899–903.
- Vondrak, T., Plane, J. M. C., Broadley, S., Janches, D. (2008): A chemical model of meteoric ablation. Atmos. Chem. Phys. 8, 7015–7031.
- U.S. Standard Atmosphere, 1976. NOAA/NASA/USAF.

The reference details (volumes, pages) are given from memory and should be checked before citing.
