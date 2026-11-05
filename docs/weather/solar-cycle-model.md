# Solar cycle v1: sunspots, the forecast, UV, flares and the climate

The Sun's activity rises and falls in a cycle of about eleven years (the Schwabe cycle). At a maximum the disk carries many sunspot groups, the Sun is slightly brighter, its ultraviolet is markedly stronger and flares are frequent. At a minimum spotless days are the rule. This component maps that cycle into TerraFlux:

- it records the cycle and finds its minima and maxima;
- it forecasts the next ones, and backtests the method on the past cycles;
- it translates the cycle into what reaches the Earth: total and ultraviolet irradiance, ozone, the surface UV index, flares and radio blackouts, and the response of the global surface temperature.

Two existing models can take its output: the ocean's air–sea shortwave and the polar-melt warming scenarios.

| | |
|---|---|
| Question | When are the next minimum and maximum, and how large? How much do TSI, UV, ozone and the UV index change over the cycle? How many flares and radio blackouts should we expect? How much does the cycle change the global temperature? |
| CLI | `terraflux-run-solar-cycle <request.json> <timeline.json>` |
| Request / output | `terraflux.solar.cycle-run/v1` ([schema](solar-cycle-run-v1.schema.json)) → `solar` block `terraflux.solar.cycle/v1` ([schema](solar-cycle-v1.schema.json)) in a `terraflux.weather.timeline/v1` |
| Example | [examples/solar-cycle-request.json](../../examples/solar-cycle-request.json) → `viewer/public/solar/engine-timeline.json` (**☀ Load solar cycle & space weather example**, view **Sun & solar cycle**) |
| Kernels | `include/terraflux/weather/solar_cycle.hpp` (shape, smoothing, extrema, fits, forecast, backtest), `solar_irradiance.hpp` (proxies, UV, ozone, UV index, flares, energy balance, disk), `solar_forcing.hpp` (the export that other runs read) |
| Real data | `tools/silso_to_request.py` turns the SILSO monthly file into a `monthly` record (§1) |

## 1. The sunspot record

The record is the monthly mean total sunspot number, version 2 (SILSO; Clette et al. 2014). The request gives it in one of two ways:

- **`"kind": "monthly"`:** the values month by month without gaps. `tools/silso_to_request.py SN_m_tot_V2.0.csv --start 1850-01 --first-cycle 10 --output request.json` converts the official file, which must be downloaded separately (nothing is fetched). It refuses gaps unless `--fill-gaps` interpolates them, and records the provisional months and the licence (CC BY-NC 4.0) in `data_origin`.
- **`"kind": "cycle_table"`:** a table of cycles (date of the minimum, and the value and date of the maximum of the 13-month smoothed series) turned into a synthetic monthly record.
  1. Every cycle becomes a Hathaway curve (§2).
  2. The onsets are shifted until the summed curve has its minima at the table's dates; the first cycle has no predecessor and starts `first_lead_years` (0.3) before its minimum.
  3. The amplitudes are scaled until the maxima have the table's values.
  4. Monthly noise is added: R_m = max(0, R(t_m) + f √(R + 1) e_m), where e_m = φ e_(m−1) + √(1 − φ²) z_m is correlated from month to month (f = 2.5, φ = 0.6). Values are rounded to 0.1 like the published ones.

  With `calibrate_widths` the widths are also scaled until each maximum falls on the table's date. This is off by default: one smooth curve cannot represent a double peak (cycles 23 and 24), and stretching it to a late second peak lifts the following minimum far above the observed one (about 45 instead of 2–6).

**The example's data.** The table covers cycles 9–25 with approximate SILSO v2 values: minima, maxima and their dates, rounded, with cycle 25's maximum (160.9 in late 2024) provisional. The values were entered by hand and **have not been verified against the SILSO files**; the request's `data_origin` says so, and the viewer shows it. The monthly values after the last table entry continue the curve of cycle 25 to the issue date (October 2026); they are synthetic, not observations. For quantitative work, convert the SILSO file and run the same request with a `monthly` record: the check `weather.solar_cycle_cli` verifies that a monthly record reproduces the forecast of the same values exactly.

The noise has visible consequences in the synthetic record:

- **Maxima:** the detected maxima run higher than the table's. The maximum of a noisy series is biased upward.
- **Minima:** the detected minima fall on average about half a year before the table's. Around a minimum the old cycle's tail is flatter than the new cycle's rise, so noise makes the lowest smoothed month early.

Real SILSO dates carry the same kind of uncertainty.

## 2. The shape of a cycle

Hathaway, Wilson & Reichmann (1994) and Hathaway (2015), with t the months after the cycle's onset t₀:

R(t) = a t³ / (exp(t²/b²) − c),  c = 0.71,  b = β b_H(a),  b_H(a) = 27.12 + 25.15 / (1000 a_v1)^¼

- **Width relation.** b_H is Hathaway's width relation, calibrated on the original (v1) numbers, so it is evaluated with a_v1 = 0.6 a (SN v2 = SN v1 / 0.6). Large cycles are narrower and peak sooner: the **Waldmeier effect**.
- **Parameters.** A single cycle peaks at t = u* b with u* = 1.0813, at A = a b³ g* with g* = 0.503803. A cycle is therefore set by three parameters: its onset, its amplitude A (the peak of the smoothed sunspot number) and a width scale β (1 = Hathaway's relation). `make_solar_cycle_curve` solves A = a b(a)³ g* for a by Newton's method.
- **Overlap.** Successive cycles overlap: the sunspot number is the sum of the curves, and a minimum lies where the old cycle's decline meets the new one's rise, a few months after the new onset.

## 3. Minima and maxima

- **Smoothing.** The SILSO 13-month running mean with half weights at the ends: S_n = [½R_(n−6) + R_(n−5) + … + R_(n+5) + ½R_(n+6)] / 12. It is undefined for the first and last six months.
- **Extrema.** An extremum is the lowest (highest) smoothed value within ±30 months. The window must lie inside the smoothed series, except at its end, where 12 smoothed months after the candidate suffice (the confirmation). Of two neighbouring extrema of the same kind the more extreme is kept.
- **Detection lag.** A minimum therefore becomes detectable about 1.5 years after it happened (6 months of smoothing lag plus 12 months of confirmation). This matters for the forecast (§4).
- **Cycles.** A cycle runs from minimum to minimum and is numbered from the first detected minimum (`first_cycle_number`). The output lists for every cycle:
  - the minimum (date and value) and the maximum;
  - its length (minimum to minimum), rise and decline;
  - the fitted curve (§4);
  - for a table record, the table's entry;
  - the statistics of the complete cycles, including the Waldmeier correlation of rise time and maximum.

## 4. The forecast

The forecast uses only the record before `issue_year`.

### 4.1 The past cycles

Every complete cycle is fitted with its onset (±2 years), amplitude and width, on top of the earlier cycles' curves. The fit runs over a coarse grid, then a pattern search: onset and width trade off along a diagonal valley, which a fixed refinement grid can miss. From the fits come:

- the mean period (onset to onset) and its spread;
- the mean amplitude, its spread and its lag-1 autocorrelation ρ (clamped to 0–0.8);
- the lead of the detected minima over the onsets;
- the width scales;
- the noise of the monthly values around the curves and its month-to-month correlation φ.

### 4.2 The cycle in progress

The cycle in progress is the one that started at the last detected minimum.

- **Onset.** The onset has a Gaussian prior: the minimum minus the mean lead, with the lead's spread (at least 0.15 years). Minima are flat and noisy, so they fix the onset only loosely.
- **Fit.** Onset and amplitude (and, once the cycle is older than 5.5 years and past its peak, the width) minimise SSE/σ² + ((t₀ − t₀,prior)/σ_t)², with σ² the residual noise variance inflated by (1 + φ)/(1 − φ) for correlated months. Near the peak, onset, width and amplitude cannot be told apart; after it the width is well determined. The onset's spread follows from the curvature of the profile cost.
- **Prior.** An AR(1) prior from the previous cycle: A_prior = μ + ρ (A_prev − μ), with σ_prior = σ_A √(1 − ρ²).
- **Combination.** Fit and prior are weighted by their inverse variances. The fit's variance is the RMS relative error that the backtest (§4.4) found at the same phase, interpolated between phases and held beyond them. Where the backtest has no fit (phases before the minimum is detectable) only the prior counts.
- **Width.** The width's spread is converted from the backtest's error in the peak date (t_peak = t₀ + u* b).

### 4.3 Later cycles and the ensemble

Each later cycle starts one period after the previous one (period ~ N(mean, sd), truncated to ±2.5 sd and at least 7 years). Its amplitude follows the AR(1) chain and its width scatters around the mean width.

- **Ensemble.** 200 members sample every uncertainty: the amplitude, onset and width of the cycle in progress, and the periods, amplitudes and widths of the later ones. The output gives the sunspot percentiles (5, 25, 50, 75, 95) per month and the central member (all parameters at their central values).
- **Turning points.** Per member, the turning points are located in windows tied to each cycle's onset: the minimum in [t₀ − 1.5, t₀ + 2.5] years, the maximum in [t₀ + 1.5, t₀ + 8] years. They are reported as medians with 5–95 % ranges of date and value; `beyond_horizon` flags turning points that more than 5 % of the members reach only after the horizon.

### 4.4 Backtest: how good is it?

Every past cycle with three complete cycles before it is forecast again with the data available at its detected minimum + each phase (0, 1, 1.5, 2, 2.5, 3, 4, 5, 6 years), and compared with the maximum that followed. Before the minimum is detectable, the cycle is predicted from the prior and the mean period, which also yields a predicted minimum date.

Results for the example (12 cycles, 13–24, smoothed sunspot number at maximum):

| Years after minimum | 0 | 1–1.5 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|---|
| Fit, mean absolute error | – | (not detectable) | 44 | 29 | 18 | 10 | 8 |
| Prior (earlier cycles) | 47 | 47 | 48 | 48 | 48 | 48 | 48 |
| Combined | 53 | 47 | 43 | 35 | 21 | 13 | 10 |
| Date of the maximum | 1.0 yr | 0.9 yr | 0.5 yr | 0.5 yr | 0.5 yr | 0.5 yr | 0.3 yr |
| Date of the minimum | 0.8 yr | 0.8 yr | | | | | |

A forecast from the minimum is climatology: its error (±47) is the spread of the amplitudes. It becomes useful about two to three years into a cycle and good after four; this matches the experience with curve-fitting methods (Hathaway 2015).

Three caveats apply:

- **In-sample weights.** The combined column uses error estimates from these same cycles (partly in-sample).
- **Synthetic test.** On the synthetic example the backtest measures the method against a record made from the same shape, plus noise.
- **No precursors.** Physical precursors of the next cycle, chiefly the Sun's polar magnetic field at minimum, are not in this model. They are what gives operational predictions skill before a cycle starts.

### 4.5 The example's forecast (issued October 2026, synthetic record)

- **Cycle 25** (minimum 2019.5 in the synthetic record, maximum passed): fitted amplitude 150 (backtest error ±10) against the prior 169 ± 49, combined 150 ± 10.
- **Next minimum**, starting cycle 26: 2030.6 (90 %: 2029.2–2032.0).
- **Cycle 26 maximum:** around 2034.7 (2033.1–2036.5) at 178 (94–256), essentially the climatology of the record.
- **Next minimum after that:** 2041.4 (2038.8–2043.7); cycle 27's maximum lies partly beyond the 2050 horizon.

## 5. Irradiance and ultraviolet

Monthly proxies of the sunspot number SN, defaults in brackets:

| Quantity | Relation |
|---|---|
| F10.7 radio flux | F = F₀ + f₁ SN + f₂ SN² (65 sfu, 0.62, 0) |
| Mg II core-to-wing index | M = M₀ + m (F − F₀) (0.150, 1.1·10⁻⁴ per sfu) |
| Facular index | x = (M − M₀) / (M(SN_ref) − M₀): 0 for the quiet Sun, 1 at the reference maximum (SN_ref = 180) |
| Total solar irradiance | S = S_quiet + B x − D SN (1360.6 W/m², B = 2.1 W/m², D = 0.0055 W/m² per sunspot) |
| UV band b | E_b = E_b,quiet (1 + v_b x) |

- **TSI.** Faculae brighten the Sun more than the spots darken it, so it is brightest at maximum. At SN 180: +2.10 − 0.99 = **+1.11 W/m² (0.08 %)**. For SN 116 (cycle 24) the change is +0.7 W/m², consistent with the ~1 W/m² cycles measured since 1978.
- **UV.** The shorter the wavelength, the larger the cycle:

| Band | Quiet Sun | Change to the reference maximum |
|---|---|---|
| Lyman-α 121.6 nm | 0.006 W/m² | +55 % |
| 200–242 nm (splits O₂: makes ozone) | 1.7 W/m² | +6 % |
| UV-C 100–280 nm (contains the two above) | 6.3 W/m² | +3.5 % |
| UV-B 280–315 nm | 18.5 W/m² | +0.9 % |
| UV-A 315–400 nm | 85 W/m² | +0.3 % |

Wavelengths below 400 nm carry 58 % of the TSI change. Measured UV variability differs by about a factor of two between instruments and reconstructions (SORCE SOLSTICE/SIM against SATIRE and NRLSSI2; Ermolli et al. 2013); the defaults are mid-range and every band is configurable.

## 6. Ozone, the stratosphere and the UV index at the ground

- **Middle atmosphere.** Linear in x: total ozone +2 % and upper-stratospheric temperature +1 K at the reference maximum. These are the orders of magnitude of the observed solar-cycle signals (Gray et al. 2010).
- **UV index.** The clear-sky noon UV index at each site follows Madronich (2007):

  UVI = 12.5 μ₀^2.42 (Ω / 300 DU)^−1.23 · d(day) · (1 + v_ery x),  Ω = Ω_site(day) (1 + 0.02 x)

  - μ₀ is the cosine of the noon solar zenith angle (from the ocean model's sun).
  - d = 1 + 0.0334 cos(2π(day − 3)/365.25) is the Earth–Sun distance factor.
  - v_ery (1 %) is the change of the erythemally weighted UV at the top of the atmosphere.
  - Ω_site has an annual mean, a seasonal amplitude and a peak day.
- **Near cancellation.** More UV at the top also makes more ozone, which absorbs UV-B. With these settings the UV index changes by (1 + 0.01)(1 + 0.02)^−1.23 − 1 = **−1.4 %** between the quiet Sun and the maximum, at every site and season, far less than the seasons, clouds or the ozone layer's own variations. The sign depends on the assumed ozone response.

## 7. Flares

- **Rate.** The number of flares per year with a GOES 1–8 Å peak flux ≥ S:

  N(≥S) = r_X (SN / SN₀)^γ [(S/S_X1)^(1−α) − (S_max/S_X1)^(1−α)] / [1 − (S_max/S_X1)^(1−α)]

  with r_X = 15 X-class flares per year at SN₀ = 100, γ = 1.3, size index α = 2.2 (about 16 M-class flares per X-class flare) and the cut-off S_max = X40.
- **Calibration.** The rate reproduces the order of the X-flare counts of cycles 23 and 24 (≈ 125 and ≈ 49), whose rate per sunspot differs by about a factor of two. The cut-off is about the largest flares known (4 November 2003, saturated at X28 and estimated at X35–X45; Carrington 1859, estimated about X45). A pure power law to X100 would make flares above X50 routine over a century. Even so, a truncated power law overestimates the very largest flares (≥ X20) compared with the space-age record by a factor of a few: treat that tail as an upper estimate.
- **Radio blackouts.** The NOAA scale: R1 ≥ M1, R2 ≥ M5, R3 ≥ X1, R4 ≥ X10, R5 ≥ X20. Expected counts per month and the probability of at least one, 1 − exp(−N), follow from the rate. The annual outlook of the forecast gives X-flares per year and the probability of an X10+ flare in the year, as ensemble percentiles.
- **Synthetic record flares.** For the record, monthly Poisson draws with power-law sizes give synthetic counts, the largest flare of each month and flares located in the disk's groups. They are random draws consistent with the rate, not a flare list.
- **Energy.** E = E_X1 (S/S_X1)^0.8 with E_X1 = 10²⁵ J radiated (X10: 6·10²⁵ J, 224 J/m² at the Earth). All flares ≥ C1 together average 7·10⁻⁴ W/m² at SN 180: 0.06 % of the cycle's TSI change.

Flares matter for the ionosphere (radio blackouts on the day side), the thermosphere (EUV heating, satellite drag) and, through the particles of the eruptions that accompany them, for polar ozone. They do not matter for the energy budget of weather and climate.

## 8. The climate response

The forcing is the TSI anomaly spread over the sphere minus what is reflected: F = ΔS (1 − 0.29) / 4, 0.20 W/m² between the quiet Sun and SN 180. ΔS is measured against the mean TSI of the record's complete cycles, so the forcing averages to zero over them.

It drives a two-layer energy balance (Held et al. 2010), with the CMIP5 multi-model means of Geoffroy et al. (2013):

C dT/dt = F − λT − γ(T − T_d),  C_d dT_d/dt = γ(T − T_d)

- C = 7.3 and C_d = 106 W yr m⁻² K⁻¹; λ = 1.13 and γ = 0.74 W m⁻² K⁻¹.
- Monthly implicit steps; each ensemble member is continued from the record's state at the issue date.

The ocean's heat capacity damps and delays the 11-year swing:

- **Size of the response.** A strong cycle moves the global surface temperature by **about 0.05 K peak to peak**, a weak one (cycle 24) by 0.026 K. That is about what two to three years of the present warming trend (roughly 0.2 K per decade) add.
- **Observations.** Observational analyses find up to about 0.1 K (e.g. Lean & Rind 2008). The difference may come from pathways this model leaves out: the UV–ozone heating of the stratosphere that changes the winter circulation from above (top-down), and ocean dynamics.

## 9. The solar disk and the butterfly diagram

The timeline's frames show an illustrative day on the visible disk every six months (2008–2045), seen from the solar equator plane.

- **Groups.** Their number is Poisson with a mean of SN/17. Each group comes from one of the cycles present, in proportion to its share of the sunspot number, at the latitude of that cycle's spot zone (Spörer's law, Hathaway 2011): λ = 28° exp(−(t − t₀)/90 months), with a spread of max(3°, 0.25 λ). Longitudes are uniform on the visible hemisphere and areas lognormal (median 100 µHem).
- **Rendering.** 6 × 6 subsamples per cell:
  - limb darkening I/I₀ = 1 − 0.6(1 − μ);
  - umbrae (20 % of the spot area) at 0.25 and penumbrae at 0.78 of the photosphere;
  - faculae around each group over 23 times its spot area, with a contrast rising from 0.5 % at the disk centre to 12 % at the limb.
- **Output.** Cells hold the area-weighted intensity as a brightness temperature (T_eff = 5772 K).
- **Calibration of the faculae.** The facular area ratio is chosen so that the rendered disk brightens by as much as the TSI proxy's faculae (at SN 160 the rendered disk gives −700 ppm from the spots and +1360 ppm from the faculae, the proxy −650 and +1370 ppm). Each frame reports its disk-integrated spot, facular and net change.
- **Flares.** Record months carry the month's synthetic flares, located in the groups in proportion to their area; forecast months carry none.
- **Butterfly diagram.** The expected spot area density by time and latitude (µHem per degree), from the cycle curves (fits for the past, the central forecast after) and the same law. The record holds no spot positions.

The forecast/v1 fields of the frames are placeholders (288 K, no wind; there is no Earth grid), as in the coastal timeline. The viewer hides the air-temperature and wind layers for solar timelines.

## 10. Using the solar cycle in other runs

The `solar.forcing_export` block (`terraflux.solar.forcing/v1`) holds annual means of the TSI and of the surface-temperature response, from the record and then from the forecast median. `load_solar_forcing` reads it from a solar timeline or from a bare export file. Other runs use it as an anomaly about the mean Sun, interpolated between annual means; outside its years the Sun is its mean.

- **Ocean (`terraflux-run-ocean-circulation`).** `forcing.air_sea.shortwave` can take
  - `"solar_constant_w_m2"`: a fixed top-of-atmosphere irradiance (default 1361), or
  - `"solar_cycle_path"`: a solar timeline. The astronomical shortwave then uses 1361 W/m² + (TSI(t) − mean TSI) at each step's date; the provenance records the source.

  Over days to years this changes the surface shortwave by at most ±0.05 %.
- **Polar melt (`terraflux-run-polar-melt`).**

  ```json
  "solar_cycle": {"timeline_path": "viewer/public/solar/engine-timeline.json", "warming": true, "arctic_shortwave": true}
  ```

  This adds the solar response to every scenario's warming, the control included (the Sun varies in every world, so comparisons against the control isolate the scenario), and the TSI anomaly to the Arctic column's sunshine. The block then carries a `solar_cycle` entry. The response is at most a few hundredths of a kelvin against scenario warmings of kelvins: the solar cycle does not move the overturning's tipping point.

Runs without these options are unchanged: their committed fixtures regenerate exactly.

## 11. Verification

- **`weather.solar_cycle`** (C++):
  - the shape identities (peak = amplitude, peak date, Hathaway's width, Waldmeier effect);
  - the smoothing weights, the alternating extrema and their confirmation at the end of a record;
  - the table calibration and the synthetic record;
  - exact fits of noise-free data, and the onset prior giving way to data (or holding);
  - a forecaster on noise-free periodic cycles (the next minimum within half a year, ordered percentiles, the backtest);
  - the proxies (TSI rises with SN, UV variability falls with wavelength), Madronich's formula, the flare power law (rates, sampled sizes, the energy integral against quadrature, GOES classes, R scale);
  - the energy balance (equilibrium, the implicit step, damping), the disk (limb darkening, a central spot's deficit against its projected area, Spörer drift, groups per sunspot, butterfly area);
  - the forcing loader.
- **`weather.solar_cycle_cli`:** the contract (JSON Schemas when `jsonschema` is installed), and every identity recomputed from the output alone:
  - the smoothing, F10.7, the facular index, TSI, the UV bands, ozone and the UV-index change;
  - the expected flares and the synthetic counts against them;
  - the energy balance integrated again from the forcing, and the backtest MAEs;
  - the ordered forecast percentiles and P(X) = 1 − exp(−N), the butterfly area and the disk photometry;
  - that a monthly record reproduces the forecast;
  - that ten invalid requests fail without output.
- **`weather.solar_coupling`:** the ocean shortwave scales exactly with the TSI anomaly at the model date (and 1361 W/m² reproduces the default run); the polar-melt warming rises by the solar response in every scenario; invalid couplings are rejected.
- **`weather.viewer_solar_cycle`:** the committed fixture matches regeneration.
- **Python tests:** `test_silso_to_request.py` (reading, gaps, the request).
- **Viewer:** `solar.test.js` (validation and rejection of broken blocks, metrics, cycles and turning points, charts, UV sites, the R scale, geometry and colours).

## 12. Limitations

- The example's record is synthesised from an approximate, unverified cycle table: dates and values in it are illustrations of the method, not a forecast of the real Sun. Use the SILSO conversion for that.
- One smooth curve per cycle: no double peaks, no north–south asymmetry, no Gnevyshev gap. Cycles that have not started are climatology with a weak AR(1) memory; no polar-field or geomagnetic precursor.
- The irradiance, UV, ozone and UV index are linear proxies of the sunspot number: no separate Mg II or F10.7 input, no spectral model. UV variability is uncertain by a factor of about two.
- The flare model is a rate and a size distribution: no flare list, no CMEs or particle events, no geomagnetic storms. The synthetic flares are random draws; the extreme tail is an upper estimate.
- The climate response is a global two-layer energy balance: no regional pattern, no stratospheric (top-down) or ocean-dynamical amplification, no volcanic or other forcing.
- The disk is an illustrative day: group positions, sizes and flares are draws consistent with the cycle, not observations.

## References

Clette et al. 2014 (sunspot number recalibration, v2); Hathaway, Wilson & Reichmann 1994; Hathaway 2011 (spot-zone drift); Hathaway 2015 (*The Solar Cycle*, Living Reviews); Waldmeier 1935; Ermolli et al. 2013 (solar spectral irradiance variability); Gray et al. 2010 (solar influences on climate); Madronich 2007 (UV-index formula); Lean & Rind 2008; Held et al. 2010 and Geoffroy et al. 2013 (two-layer energy balance); Veronig et al. 2002 (flare statistics); Emslie et al. 2012 (flare energies); Woods et al. 2004 (TSI during the 2003 flares); Cliver & Dietrich 2013 (the Carrington flare); NOAA SWPC space-weather scales.
