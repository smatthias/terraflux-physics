# Solar wind, polar caps and atmospheric escape v1

The solar wind meets the Earth's magnetic field. Over the polar caps, the field lets charged particles in and ions out. `terraflux-run-solar-wind` maps both and asks what they mean globally:

- **The magnetosphere.** Its coupling to the solar wind (Kp, Dst, the magnetopause), the polar caps of open field lines and the auroral ovals, and the energy they let into the upper atmosphere.
- **Atmospheric escape.** Hydrogen by its three routes, oxygen ions from the cusp and the oval, and helium.
- **Cosmic radiation.** Galactic cosmic rays behind the geomagnetic cutoff, their solar-cycle change and Forbush decreases, and solar proton events with their NOx and polar ozone loss.
- **A storm.** An hour-by-hour severe storm on a global map.
- **The solar cycle.** Monthly values driven by the solar-cycle example.
- **Scenarios.** Storms from quiet to Carrington-class, proton events, a weaker dipole, methane, and places on the Earth.

Kernels: `include/terraflux/weather/magnetosphere.hpp`, `cosmic_radiation.hpp`, `atmospheric_escape.hpp`. Request: `terraflux.solar.wind-run/v1` ([schema](solar-wind-run-v1.schema.json), [example](../../examples/solar-wind-request.json)). The output is a viewer timeline with a `solar_wind` block (`terraflux.solar.wind/v1`, [block schema](solar-wind-v1.schema.json)). In the viewer it is the **Solar wind & poles** view; load it with **Sun & space weather → 🧲 Load solar wind & polar caps example**.

```sh
build/terraflux-run-solar-wind examples/solar-wind-request.json /tmp/solar-wind.json     # about 2 s
python3 tools/build_ocean_example.py --build-dir build --cli terraflux-run-solar-wind \
    --request examples/solar-wind-request.json --output viewer/public/solar-wind/engine-timeline.json
```

## Why the poles?

The magnetic field is **not weaker at the poles**. A dipole's surface field is twice as strong at its poles as at its equator: about 60 µT against 30 µT today. What sets the poles apart is the direction and the topology of the field lines:

- **Near the equator** the lines run horizontally and close in the other hemisphere. A charged particle coming from space has to cross them and is turned back unless its rigidity (momentum per charge) exceeds the **cutoff**: about 14 GV at the magnetic equator.
- **Poleward of the auroral ovals** the lines stand nearly vertical and are **open**: connected to the solar wind and stretched into the magnetotail. Along them:
  - cosmic rays and solar protons come straight in (the cutoff falls below 1 GV);
  - solar-wind energy rains down as aurora and Joule heating;
  - ions of the upper atmosphere flow out to space.

So the polar caps are the magnetosphere's windows. Their size is set by the solar wind: they widen when the interplanetary field turns southward and reconnects with the Earth's field at the day-side magnetopause.

Not everything that escapes leaves through them. By mass, the Earth's escape is almost all **hydrogen**, and most of it leaves everywhere above the exobase, thermally or by charge exchange. **Oxygen** cannot escape thermally at all. It leaves only as ions along the open and stretched field lines of the caps, the cusp and the night-side oval.

## 1. The field and magnetic coordinates

- **The dipole.** A centred, tilted dipole: moment 7.7·10²² A m², geomagnetic north pole at 80.8° N, 72.7° W, both rounded for about 2025. Magnetic latitude and longitude follow from the pole. Magnetic local time is MLT = 12 + (mlon − mlon of the subsolar point)/15.
- **Cutoff.** The vertical Störmer cutoff at 20 km is Rc = μ₀ M c cos⁴λ / (16π r²): 14.1 GV at the magnetic equator, 2.4 GV at 50°, below 0.5 GV poleward of about 65°. In storms the ring current lowers it, crudely by 5 % per 100 nT of −Dst (at most 50 %).
- **Flux of a cap.** The magnetic flux through a cap bounded at colatitude θ is F = μ₀ M sin²θ / (2r). This turns the open flux into the size of the polar cap.

## 2. The solar wind and the magnetosphere

| Quantity | Formula | Source |
|---|---|---|
| Dynamic pressure | P = m_p n v² | |
| Coupling | dΦ/dt = v^(4/3) B_T^(2/3) sin^(8/3)(θ_c/2) | Newell et al. 2007 |
| Kp | 0.05 + 2.244·10⁻⁴ dΦ/dt + 2.844·10⁻⁶ n^½ v², with the viscous term limited to 3 and a soft saturation above 6 (6 + 3 tanh((Kp − 6)/3)) | Newell et al. 2008, modified |
| NOAA storm scale | Kp in thirds: G1 from 5−, G2 6−, G3 7−, G4 8−, G5 at 9o | NOAA |
| Ring current | dDst*/dt = Q − Dst*/τ, Q = −4.4 (vB_s − 0.49) nT/h, τ = 2.4 exp(9.74/(4.69 + vB_s)) h, Dst = Dst* + 7.26 √P − 11 | O'Brien & McPherron 2000 |
| Magnetopause | r₀ = (10.22 + 1.29 tanh(0.184 (B_z + 8.14))) P^(−1/6.6), scaled by (M/M_today)^(1/3); flaring α = (0.58 − 0.007 B_z)(1 + 0.024 ln P) | Shue et al. 1998 |

The two changes to the Kp fit keep extreme driving bounded. Without them, a dense, fast sheath with a northward field would already reach Kp 9, and so would any strong storm.

## 3. The polar cap and the auroral oval

**Open flux.** The open flux F follows the expanding/contracting polar cap model (Milan et al. 2012):

- **Dayside reconnection opens flux:** Φ_D = Λ v^(4/3) B_yz sin^(9/2)(θ_c/2), with Λ = 3.3·10⁵ (SI). It saturates as Φ_D/(1 + Φ_D/250 kV), after Hill and Siscoe.
- **Nightside reconnection closes it again:** the tail relaxes towards a base flux of 0.35 GWb with a time scale of 1.5 h.
- **Cross-polar-cap potential:** 10 kV (viscous) + (Φ_D + Φ_N)/2.

**Oval geometry.** The cap is bounded at the colatitude whose dipole flux is F (at 110 km).

- **Offset.** The cap is displaced towards midnight by a quarter of its radius (Holzworth & Meng 1975).
- **Width.** The oval lies equatorward of it, 4° + 0.6°·Kp wide at midnight and 1.5° + 0.25°·Kp at noon.
- **Storm shift.** In storms the ring current pushes it further equatorward, by 0.015° per nT of −Dst. This shift is tuned so that a Dst of about −300 to −400 nT brings the overhead aurora to about 40–45° magnetic latitude, as in the largest storms of recent decades.

Resulting cap edges and oval:

| Conditions | Cap edge | Oval reaches |
|---|---|---|
| Quiet | about 77° | 67–69° |
| Severe storm (Dst −286 nT in the example) | 65° | 45° |
| Carrington-class | 62° | about 29° |

**Shapes on the map.** The block tags each cell as closed field, auroral oval, polar cap or cusp (near noon at the cap edge).

- **Aurora.** The auroral energy flux is a Gaussian across the oval, brightest before midnight, plus a weak cusp and polar rain. Its integral over each hemisphere is the **hemispheric power**, 5 GW + 2.3·10⁻³ dΦ/dt: about 15 GW in quiet conditions and over 200 GW in the storm.
- **Oxygen outflow.** The O⁺ upflow comes from the cleft ion fountain (at the cap edge around noon) and the night-side oval, with a weak polar-cap part. Its integral is the global upflow (§5).

**Joule heating.** The Joule heating of each hemisphere is Σ_P E² over cap and oval, with E = Φ_PC/(2 r sin θ_cap) and Σ_P = 6 S. It is a few GW when quiet and up to about 390 GW per hemisphere in the storm.

## 4. The thermosphere

The exospheric temperature follows Jacchia (1970/71):

- **Night minimum:** T_c = 379 + 3.24 F̄10.7 + 1.3 (F10.7 − F̄10.7).
- **Day side:** up to 30 % warmer, peaking at 14 h local time.
- **Geomagnetic heating:** 28 Kp + 0.03 e^Kp. It follows Kp with a 4-hour lag and is weighted towards high magnetic latitudes (0.6 + 1.2 sin² of the magnetic latitude, area mean 1).

The frames carry the exospheric temperature as their air-temperature field. The density at 400 km is scaled with the scale height of atomic oxygen: the drag on satellites. In the example storm the global mean rises from 1,030 to 1,400 K and the density at 400 km by a factor of 3.3.

## 5. Escape to space

**Hydrogen.** Every hydrogen-bearing molecule that crosses the cold tropopause is split higher up, and the hydrogen diffuses to the exobase. Today the escape is limited by this diffusion (Hunten 1973):

    Φ = 2.5·10¹³ f_T  H₂ molecules cm⁻² s⁻¹,   f_T = f(H₂) + f(H₂O) + 2 f(CH₄)

With 3.8 ppm water above the cold trap, 1.93 ppm methane and 0.53 ppm H₂, this gives 2.2·10²⁷ H atoms/s, or 3.6 kg/s.

Above the exobase (500 km) three routes share this supply:

- **Jeans escape.** The thermal tail of the hydrogen, with effusion velocity v₀/(2√π)(1 + λ)e^(−λ). It rises about tenfold from 1,000 to 1,400 K.
- **Charge exchange** with hot plasmaspheric protons. Its effective velocity is calibrated so that Jeans escape carries 30 % at 1,000 K.
- **The polar wind.** H⁺ flowing out along the open field lines, with 14 m/s per exobase density (about 2·10⁸ cm⁻² s⁻¹).

The exospheric hydrogen is a reservoir of about half a day's escape. It relaxes towards the density at which the routes together carry the supply. In a storm the hotter exosphere briefly drains it faster (+30 %), then the escape dips below the supply while it refills. Over the 4 days of the example, the storm and a quiet background lose the same 1,250 t of hydrogen. Over the cycle, the Jeans share varies from 3 % (solar minimum) to 65 % (maximum), while the total stays fixed. The polar wind carries about 2 %.

**Oxygen.** O⁺ upflow = 5·10²⁴ s⁻¹ · exp(0.45 Kp + 0.012 (F10.7 − 100)) · (area factor). The exponential form follows the empirical laws of Yau et al. (1988) and later studies. Half of it escapes (the escape fraction); the rest returns to the ring current and the plasma sheet.

| Conditions | O⁺ escape |
|---|---|
| Quiet | 8·10²⁴ s⁻¹ (0.2 kg/s) |
| Severe storm (Kp 9) | 2.6·10²⁶ s⁻¹ (7 kg/s) |
| Mean of 1996–2040 | 1.1·10²⁵ s⁻¹ (0.30 kg/s) |

**Helium.** Radiogenic helium leaves mainly as He⁺ in the polar wind: 7.5·10²⁴ s⁻¹ (0.05 kg/s) at the quiet open area and F10.7 = 100. It scales with the open area and √F10.7.

**Budget (mean 1996–2040).**

| | kg/s | t/yr | Through the poles |
|---|---|---|---|
| Hydrogen | 3.6 | 114,000 | 2 % (polar wind) |
| Oxygen | 0.30 | 9,500 | 100 % |
| Helium | 0.06 | 1,900 | 100 % |
| Total | 4.0 | 125,000 | 11 % |

What that means:

- **Atmosphere.** The loss is 2.4·10⁻⁵ of the atmosphere per million years.
- **Oxygen.** Over the Earth's age the oxygen lost would be 3.6 % of today's O₂, which photosynthesis renews far faster.
- **Water.** The hydrogen lost is water split high up: 32 kg of water per second, 0.33 % of the oceans over 4.5 billion years.
- **Methane.** It now supplies about as much hydrogen as the water does. The escape was 71 % of today's with pre-industrial methane and would be 138 % at about 3.5 ppm.
- **A storm.** The example storm removes 570 t of oxygen in 4 days against 110 t when quiet: 17 extra days of the average loss.

## 6. Cosmic rays

**Spectrum.** The local interstellar proton spectrum is J_LIS(T) = 1.9·10⁴ P^−2.78 / (1 + 0.4866 P^−2.51) (Burger et al. 2000, in the form of Usoskin et al. 2005). The heliosphere modulates it in the force-field approximation (Gleeson & Axford 1968) with the potential φ:

- **In the cycle:** φ = 0.35 GV + 0.004 GV × the 12-month mean sunspot number.
- **In the storm:** 0.75 GV, raised by 0.3 GV when the CME passes (the Forbush decrease), then recovering with a 48-hour time scale.

**Responses.** What a level of the atmosphere receives is the flux above the local cutoff, weighted with an effective yield Y(P) = P^β exp(−(P_a/P)^κ). Each response is anchored at the pole at φ = 0.4 GV. The yields and anchors are calibrated by hand to rounded observational targets; they are not particle-transport results.

| Response | β, P_a, κ | Anchor | Pole / equator | Cycle change, pole / equator |
|---|---|---|---|---|
| Sea level (neutron-monitor-like) | 1.4, 3.0 GV, 3 | 100 % | 1.6 | 20 % / 7 % |
| Effective dose at 12 km | 1.0, 2.8 GV, 3.5 | 6 µSv/h | 2.7 | 35 % / 9 % |
| Ionisation at the Pfotzer maximum | 0.8, 2.5 GV, 3 | 30 ion pairs cm⁻³ s⁻¹ | 3.7 | 45 % / 10 % |
| Cosmogenic production (¹⁰Be, ¹⁴C) | 0.6, 1.5 GV, 2 | relative | 6.5 | 73 % / 11 % |

The cycle change is between φ = 0.4 and 1.0 GV.

Consequences:

- **Aviation.** At 12 km over the poles the dose is about 6.2 µSv/h near solar minimum (4.4 at maximum), against 2.3 µSv/h at the magnetic equator. A ten-hour polar flight receives about 60 µSv; aircrew on polar routes about 4 mSv a year.
- **Forbush decrease.** In the storm it lowers the ground-level rate by 8 % at the poles and 3 % at the equator.
- **Weaker dipole.** At a tenth of today's dipole (the Laschamp excursion), the global cosmogenic production doubles, as the ¹⁰Be records show.

## 7. Solar proton events and polar chemistry

**Spectrum and event.** Solar energetic protons have an exponential spectrum in rigidity, J(>R) = J₁₀ exp(−(R − R₁₀)/R₀). It is given by the >10 MeV flux (pfu) and the e-folding rigidity R₀. The example event peaks at 4,000 pfu (S3) with R₀ = 0.12 GV, rising over 8 h and decaying with a 24-hour time scale.

**Deposition.** Only where the cutoff lies below a few hundred MV, poleward of about 63° magnetic latitude, do the protons get in: the polar cap absorption region.

- **Energy by layer.** Their energy is deposited by range, R(E) = 0.0022 E^1.77 g/cm² with a mean secant of 2, in four layers of the US Standard Atmosphere: above 50 km, 35–50, 20–35 and below 20 km.
- **Products.** Each ion pair takes 35 eV and yields 1.25 NOx (Porter et al. 1976) and 2 HOx.

**Ozone.** The NOx added to the polar upper stratosphere (35–50 km, poleward of 60° magnetic) is compared with a background NOy of 15 ppbv. It lives 30 days. The ozone there scales as 1/(1 + 0.6 ΔNOy/NOy): NOx catalyses about 60 % of the ozone loss at these heights. The 35–50 km layer holds about 12 % of the ozone column.

| Event | NOx per hemisphere | Ozone 35–50 km | Dose at 12 km over the pole |
|---|---|---|---|
| Example storm (4 days) | 0.7 Gmol | −13.5 % (column −1.6 %) | unchanged: the protons are soft |
| S3 (1,000 pfu, 1 day) | 0.12 Gmol | −3 % | unchanged |
| S4 (10,000 pfu) | 1.5 Gmol | −27 % | unchanged |
| About 30,000 pfu (the October 2003 peak) | 5.6 Gmol | −58 % | unchanged |
| Hard spectrum (5,000 pfu, R₀ = 0.35 GV), a ground-level enhancement | 2.2 Gmol | −19 % | × 19; +660 % at the ground |

**Over the cycle.** Solar protons make about 2.5 Gmol of NOx a year; their steady ozone loss in the polar upper stratosphere reaches about 5 % in active years. Auroral electrons make far more NO in the thermosphere. The part that descends in the polar winter (2 % assumed) is about 7 Gmol/yr. It is reported, but not added to the ozone estimate, because it belongs to the climatological background. For comparison, N₂O oxidation makes about 40 Gmol/yr of stratospheric NOy.

## 8. The storm, the cycle and the scenarios

**The storm** (`storm`): 96 hours from 18 March 2026, frames every 2 hours on a 90 × 45 (4°) global grid with a land mask.

- **Background.** 420 km/s, 6 cm⁻³, a 5 nT field fluctuating with a 2-hour correlation (seeded), F10.7 = 150.
- **Event.** The solar proton event starts at hour 6. A CME arrives at hour 30: a 6-hour sheath (950 km/s, 25 cm⁻³, a fluctuating 25 nT field), then a 20-hour magnetic cloud. In the cloud, Bz rotates from −30 to +12 nT while the speed falls from 855 to 650 km/s; faster wind follows.
- **Result.** Dst −286 nT (G5). The magnetopause sits at 5.2 R_E, inside geostationary orbit for 10 hours. The polar cap edge reaches 65° and the oval 45° magnetic latitude.
- **Per frame.** The block holds:
  - the maps of aurora, O⁺ outflow and regions;
  - the dose at 12 km and the solar-proton ionisation as tables over the cutoff (the maps follow from the static cutoff map and the storm factor);
  - the oval geometry, the magnetopause shape and the subsolar point.

**The solar cycle** (`cycle`, optional) is driven by the monthly sunspot number and F10.7 of a `terraflux-run-solar-cycle` timeline (`solar_cycle_path`): the record, then the central forecast. The monthly solar wind is a climatology:

- **Field:** B = 4 nT + 0.022 nT × sunspot number.
- **Speed:** 410 km/s + 80 km/s in the declining phase (high-speed streams from coronal holes).
- **Density:** 6 cm⁻³.
- **Clock angle:** the coupling uses the mean of the clock-angle factor over all angles.
- **Oxygen escape:** averaged over a normal spread of Kp (σ = 1.2), since it is convex in Kp.
- **Solar protons:** a fluence of 1.5·10⁷ cm⁻² sr⁻¹ a year per unit sunspot number.

**Scenarios.**

- **`storm_scenarios`.** Steady driving from quiet to Carrington-class (1,800 km/s, Bz −60 nT for 10 h), with Dst, Kp, magnetopause, cap and oval, aurora overhead and on the horizon (8° further), aurora and Joule power, thermosphere and oxygen escape. The Carrington-class case gives Dst −1,130 nT (estimates for 1859 range from about −850 to −1,760 nT) and aurora down to about 21° magnetic latitude.
- **`sep_scenarios`.** As in §7.
- **`dipole_scenarios`.** From the Holocene high (1.4×) over 1840 (1.1×) and today to the Laschamp excursion (0.1×), with magnetopause, cap, cutoffs, the polar window (< 1 GV), ionisation, cosmogenic production, dose and oxygen escape. Under a dipole the open flux scales as M^(2/3) and the reconnection voltage as M^(1/3); the oxygen escape scales with the area of cap and oval.
- **`methane_scenarios`.**
- **`cities`.** For each place: magnetic latitude, cutoff, dose at 12 km at solar minimum and maximum, the aurora and the electrojet at the storm's peak, and the solar-proton ionisation.

## 9. How this affects us globally

| Effect | Size | Verdict |
|---|---|---|
| Loss of atmosphere and water | 4 kg/s; oxygen 0.3 kg/s | negligible on any human or geological time scale that matters for air and oceans |
| Energy into the polar atmosphere (aurora + Joule) | 47 GW on average = 4·10⁻⁷ of absorbed sunlight | negligible for weather and climate |
| Cosmic-ray energy | 2.3·10⁻⁶ W/m² (10⁻⁸ of sunlight) | negligible |
| Cosmic rays and clouds | ionisation of the lower atmosphere changes by about 18 % over the cycle | ±0.02 W/m² if nucleation responds as weakly as the CLOUD experiments suggest; about 0.9 W/m² only under the disputed strong-coupling hypothesis; the solar cycle's sunlight forcing is 0.2 W/m². Both are illustrative sensitivities. |
| Thermosphere | +hundreds of K in storms, density at 400 km × 3–4 | strong: satellite drag, orbit decay |
| Polar ozone above 35 km | tens of percent after large proton events, for weeks | regional; can shift the winter polar vortex; global mean unaffected |
| Radiation at flight altitude | poles 2–3× the equator; × 10–100 in hard proton events | aviation dose on polar routes |
| Technology | magnetopause inside geostationary orbit, electrojet over Europe and North America, HF blackout over the poles | power grids, pipelines, satellites, radio, navigation |
| A weaker dipole | more cosmic rays everywhere (production × 2 at a tenth), polar caps wider, oxygen escape × 2 | recorded in ¹⁰Be/¹⁴C; the atmosphere is not stripped |

## 10. Request and output

The request sections are checked strictly: unknown fields are rejected.

| Section | Contents |
|---|---|
| `dipole` | moment, geomagnetic north pole |
| `polar_cap` | base open flux, tail closure time, reconnection coefficient, saturation and viscous potentials, ionosphere height, midnight offset, oval widths and their Kp dependence, ring-current shift, maximum colatitude, Pedersen conductance |
| `thermosphere` | diurnal amplitude and peak time, Kp lag, high-latitude weight |
| `hydrogen` | mixing ratios, limiting-flux coefficient, homopause and exobase heights, Jeans share at 1,000 K, polar-wind velocity |
| `ion_outflow` | reference O⁺ rate, Kp and F10.7 coefficients, escape fraction, helium rate |
| `radiation` | the four responses (yield exponent, threshold, sharpness, anchor, reference modulation) and the modulation (φ_min, φ per sunspot, storm cutoff reduction) |
| `sep_atmosphere` | range coefficient and exponent, mean secant, energy per ion pair, NOx and HOx per ion pair |
| `chemistry` | polar NOy, NOx share of ozone loss, upper-stratosphere ozone share, NOy lifetime, auroral NOx descent, polar region, N₂O source |
| `storm` (required) | start, hours, frame step, seed, `background`, `cme`, `sep` |
| `map` (required) | cells east/north, land mask (rows north first, `#` land) and its source |
| `solar_cycle_path`, `cycle` | the solar-cycle timeline and the climatology window and parameters |
| scenario lists | `dipole_scenarios`, `storm_scenarios`, `methane_scenarios`, `sep_scenarios`, `cities` |
| `comparisons` | reservoirs, absorbed sunlight, background dose, aircrew hours, cloud sensitivities, aurora visibility |

The `solar_wind` block holds:

- `dipole`, `static` (magnetic coordinates, cutoff and land mask per cell);
- `storm` (phases, hourly series, frames with maps and tables);
- `latitude_profile`, `responses` (with their calibration diagnostics), `cycle` (null without a solar timeline);
- the scenario tables, `budgets` (mean escape, reservoirs, energy, storm, chemistry, clouds, aviation), `comparisons` and `limitations`.

## 11. The view

**Sun & space weather → 🧲 Load solar wind & polar caps example** opens the **Solar wind & poles** view.

**The globe.** It is drawn in a Sun-fixed frame (the Sun along +x, north up, tilted by the season), so the Earth turns beneath the magnetosphere from frame to frame. Around it:

- closed dipole field lines (cut at the magnetopause on the day side) and open field lines from the caps swept into the tail, as a sketch of the topology;
- the Shue magnetopause, the solar wind coloured by its speed, the Sun;
- escaping O⁺ from the cusp and the night-side oval, and the H⁺ polar wind;
- cosmic rays or solar protons entering the polar caps;
- the edges of the cap and the oval.

Distances beyond the Earth are compressed (r^0.45). The colour shows the chosen layer: aurora, escaping oxygen, dose at 12 km, solar-proton ionisation, cutoff rigidity, thermosphere temperature, or the open and closed field. A flat world map is the alternative.

**The panels below:**

- the metrics of the hour;
- "why the poles", with magnetic polar dials for both hemispheres and latitude profiles of the cutoff and the dose;
- twelve storm charts with the current hour;
- the escape table and the methane table;
- the solar cycle in annual means;
- proton events and aviation, the storm, city and dipole tables, and a summary of the global effects.

## 12. Verification

`ctest --test-dir build -R solar_wind` runs three tests.

**`weather.solar_wind`** (C++):

- **Magnetic coordinates.** Pole, antipode, equator, Berlin near 52°; MLT.
- **Field and cutoff.** The 30 µT field; the cutoff of about 14 GV, its cos⁴ and moment scaling; flux and colatitude inverse.
- **Solar wind.** The pressure value; no merging for a northward field; Kp bounded and about 2 in typical wind; the G scale; Milan's 50 kV.
- **Magnetopause.** About 10 R_E; inside geostationary orbit in a strong storm; the M^(1/3) scaling.
- **Ring current.** O'Brien–McPherron injection, equilibrium and recovery.
- **Polar cap and oval.** Open-flux relaxation and saturation; quiet cap at 72–80°; a big storm below 50°; shapes, regions and areas.
- **Energy and thermosphere.** Joule heating ∝ Φ²; the Jacchia terms; storm heating largest at high latitudes; density ratio.
- **Cosmic rays.** Rigidity ⇄ energy; the LIS and the force field; every response anchored, falling with cutoff and modulation, with a larger cycle change at the pole and a latitude effect of 1.3–10.
- **Solar protons.** The SEP spectrum; deposition adding up, 35 eV per ion pair, soft protons stopping high, 1 GV keeping most out; only hard spectra reaching the ground.
- **Escape.** f_T; the diffusion limit of about 2·10²⁷ s⁻¹; Jeans steeply rising, nil for oxygen; equilibrium carrying the supply with the calibrated Jeans share; a heating spike above the supply relaxing back; methane; the O⁺ law and its monthly convexity; rejections.

**`weather.solar_wind_cli`** (Python, `tests/tools/solar_wind_check.py`):

- **Contract.** Both schemas; one value per hour; the frames.
- **Identities.**
  - Pressure and the dipole flux of the cap edge; the hydrogen routes adding up.
  - The hemispheric power and the O⁺ upflow recovered from the maps (within 8 %).
  - Open field at the poles; radiation tables falling with cutoff.
- **The storm's sequence.**
  - Main phase after the arrival, then recovery; the magnetopause pushed in.
  - Steeply rising oxygen, thermosphere heating.
  - Solar protons and polar ozone loss; a Forbush decrease deepest at the pole.
  - Hydrogen over days equal to the supply, with a short storm peak.
- **The cycle.** Diffusion-limited hydrogen; mass loss; more Jeans share, less dose and more oxygen at solar maximum; larger cycle change at the poles.
- **Scenarios.**
  - Dipole: production, cutoff and window monotonic; cutoff ∝ M; r₀ ∝ M^(1/3); Laschamp production 1.5–3.5×.
  - Storms: ordered; Carrington below −800 nT.
  - Solar protons: only hard ones raise the dose; soft ones reach 55–75° and destroy ozone.
  - Methane ∝ f_T.
  - Cities: Singapore sees no aurora.
- **Budgets.** Totals, the polar share, the ocean share, the storm's oxygen against its unchanged hydrogen, the cloud bounds around the TSI forcing.
- **Runs and rejections.** A run without the solar cycle; eleven invalid requests rejected without output.

**`weather.viewer_solar_wind`.** This test regenerates the committed fixture (`viewer/public/solar-wind/engine-timeline.json`) and compares it with the stored copy. The viewer tests (`viewer/src/solarWind.test.js`) check:

- the validation and its rejections;
- that the browser's magnetic coordinates, oval boundaries and regions agree with the engine's;
- the layers, ranges and colours, and the storm metrics;
- the Sun-fixed rotation, the magnetosphere sketch and the dials;
- the escape rows and the chart helpers.

## 13. Limitations

- **The field.** A centred, tilted dipole: no South Atlantic Anomaly, no non-dipole cutoff structure (the real equatorial maximum is about 17 GV over India). The storm-time cutoff reduction is crude.
- **Coupling.** Empirical and global (Kp, Dst, Milan reconnection, Shue magnetopause), hourly; no magnetohydrodynamics, no substorm cycle, no hemispheric or seasonal asymmetry (both ovals are the same).
- **Spatial patterns.** The oval, the cusp and the outflow regions are analytic shapes around a shifted circle, normalised to the global rates.
- **Hydrogen.** The escape is diffusion-limited by construction. Its split into Jeans, charge exchange and polar wind is calibrated (Jeans 30 % at 1,000 K), not computed from the plasmasphere.
- **Oxygen.** Outflow follows an exponential law in Kp and F10.7 with an assumed escaping half. N⁺ and molecular ions are not counted. Since Kp saturates at 9, the index-based heating and oxygen escape are lower bounds for the strongest storms.
- **Cosmic rays.** Effective yields calibrated to rounded targets and anchored to rounded values (6 µSv/h at 12 km over the poles near solar minimum, 30 ion pairs cm⁻³ s⁻¹ at the Pfotzer maximum). Protons only; no particle transport; no anisotropy of solar protons.
- **Chemistry.** A steady catalytic estimate for one layer of the polar upper stratosphere: no transport, no mesospheric HOx chemistry, no dynamics. Auroral NOx is reported, not used. The cloud forcings are illustrative sensitivities, not model results.
- **The cycle and the storm.** The solar wind of the cycle is a climatology of the sunspot number, and the example's sunspot record is synthetic. The storm is synthetic too, not a reconstruction of a real event.
- **Unverified values.** Several coefficients and comparison values were entered from general knowledge of the literature (Newell's Kp coefficients, Jacchia's terms, the O⁺ law, the anchors, the reservoirs) and were not checked against the original papers. Check them before quantitative use.

## References

- Burger, R. A., Potgieter, M. S., Heber, B. (2000): Rigidity dependence of cosmic ray proton latitudinal gradients measured by the Ulysses spacecraft. J. Geophys. Res. 105, 27447.
- Gleeson, L. J., Axford, W. I. (1968): Solar modulation of galactic cosmic rays. Astrophys. J. 154, 1011.
- Holzworth, R. H., Meng, C.-I. (1975): Mathematical representation of the auroral oval. Geophys. Res. Lett. 2, 377.
- Hunten, D. M. (1973): The escape of light gases from planetary atmospheres. J. Atmos. Sci. 30, 1481.
- Jacchia, L. G. (1970, 1971): New static models of the thermosphere and exosphere with empirical temperature profiles. Smithsonian Astrophys. Obs. Spec. Rep. 313 and 332.
- Milan, S. E., et al. (2012): Magnetospheric flux throughput in the Dungey cycle. J. Geophys. Res. 117, A01209.
- Newell, P. T., et al. (2007): A nearly universal solar wind-magnetosphere coupling function inferred from 10 magnetospheric state variables. J. Geophys. Res. 112, A01206.
- Newell, P. T., et al. (2008): Pairs of solar wind-magnetosphere coupling functions. J. Geophys. Res. 113, A04218.
- O'Brien, T. P., McPherron, R. L. (2000): An empirical phase space analysis of ring current dynamics. J. Geophys. Res. 105, 7707.
- Porter, H. S., Jackman, C. H., Green, A. E. S. (1976): Efficiencies for production of atomic nitrogen and oxygen by relativistic proton impact in air. J. Chem. Phys. 65, 154.
- Shue, J.-H., et al. (1998): Magnetopause location under extreme solar wind conditions. J. Geophys. Res. 103, 17691.
- Siscoe, G. L., Chen, C.-K. (1975): The paleomagnetosphere. J. Geophys. Res. 80, 4675.
- Slapak, R., et al. (2017): Quantification of the total ion transport from the near-Earth plasma sheet. Ann. Geophys. 35, 869.
- Usoskin, I. G., et al. (2005): Heliospheric modulation of cosmic rays: Monthly reconstruction for 1951–2004. J. Geophys. Res. 110, A12108.
- Yau, A. W., et al. (1988): Quantitative parametrization of energetic ionospheric ion outflow. In: Modeling Magnetospheric Plasma, AGU Geophys. Monogr. 44.
- Further background: Catling, D. C., Kasting, J. F. (2017), Atmospheric Evolution on Inhabited and Lifeless Worlds (escape); Jackman, C. H., et al. (2005) on the October 2003 proton events; Randall, C. E., et al. (2007) on energetic-particle NOx; Seppälä, A., et al. (2009) on geomagnetic activity and polar winter temperatures; Gunell, H., et al. (2018) on whether a magnetic field protects an atmosphere; Dunne, E. M., et al. (2016) on cosmic rays and aerosol nucleation (CLOUD).

The reference details (volumes, pages) are given from memory and should be checked before citing.
