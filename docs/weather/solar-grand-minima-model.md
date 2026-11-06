# Grand solar minima and maxima v1: centuries of solar activity, their cause and their effects

The 11-year cycle ([solar-cycle-model.md](solar-cycle-model.md)) rides on slower changes. For decades at a time the Sun almost stops making spots: the grand minima, among them the Oort, Wolf, Spörer and **Maunder Minimum (1645–1715)**. At other times it stays highly active: the grand maxima, such as the Medieval and the Modern Maximum.

The `long_term` section of a `terraflux.solar.cycle-run/v1` request maps this into the solar model:

- **History:** the sunspot number from 1000 AD to the horizon, the catalogue of epochs, and the episodes detected in the series.
- **Effects:** total and UV irradiance with the slow brightening of the quiet Sun, ozone, cosmic rays and the ¹⁴C/¹⁰Be they produce, and the climate, with volcanic and CO₂ forcing for comparison.
- **Cause:** a stochastic dynamo map in which grand minima arise by themselves, calibrated to the isotope statistics of the Holocene.
- **Future:** the probability of a new grand minimum, and what one would change.

Kernel: `include/terraflux/weather/solar_long_term.hpp`. The output is `solar.long_term` ([block schema](solar-cycle-v1.schema.json)). In the viewer it appears in the **Sun & solar cycle** view below the butterfly diagram.

## 1. The historical envelope

- **Before 1755.** The SILSO cycle table begins with cycle 1 in 1755. Earlier cycles are synthesised around a **catalogue of epochs**, each with a name, a kind (grand minimum or maximum, or a weaker minimum or maximum), its years, the typical smoothed maximum of its cycles and the evidence for it.
  - **Envelope.** The epochs with an amplitude are blended into the `normal_amplitude` (120) in the logarithm, with tanh transitions of ±12 years.
  - **Cycles.** Lengths of 11 ± 1 years (8–15), counted backwards from cycle 1 (numbers 0, −1, −2, …), with amplitudes lognormal around the envelope (spread 0.25).
  - **Annotations.** Epochs without an amplitude (Dalton, the minimum around 1900, the Modern Maximum) only annotate: the cycle table and the record cover them.
- **From 1755.** The table's calibrated cycles until the record starts (1850), then the monthly record, then the central forecast. The forecast is continued with the record's mean cycle (period 10.9 years, amplitude 178) to the horizon.

The example's catalogue summarises the literature with rounded dates and typical amplitudes. It is **not a reconstruction**. For quantitative history, replace the envelope by a decadal reconstruction from ¹⁴C and ¹⁰Be (e.g. Usoskin et al. 2016/2021, Solanki et al. 2004) or by the group sunspot number (Svalgaard & Schatten 2016).

| Epoch | Years | Kind | Typical cycle maximum | Evidence |
|---|---|---|---|---|
| Oort Minimum | 1010–1050 | grand minimum | 20 | ¹⁴C, ¹⁰Be |
| Medieval Maximum | 1100–1250 | grand maximum | 190 | ¹⁴C; naked-eye sunspots and aurorae in East Asian chronicles |
| Wolf Minimum | 1280–1350 | grand minimum | 15 | ¹⁴C, ¹⁰Be |
| Spörer Minimum | 1450–1550 | grand minimum | 12 | ¹⁴C, ¹⁰Be (the isotopes suggest a start around 1400) |
| Maunder Minimum | 1645–1715 | grand minimum | 8 | telescopes, aurorae, ¹⁴C, ¹⁰Be |
| Dalton Minimum | 1790–1830 | minimum | (table: cycles 5, 6) | telescopes |
| Minimum around 1900 (Gleissberg) | 1880–1915 | minimum | (table: cycles 12–14) | telescopes |
| Modern Maximum | 1940–2009 | grand maximum | (table: cycles 18–23) | telescopes |

**Episodes.** The series marks a grand minimum (maximum) wherever the centred 11-year mean of the annual sunspot number stays below 25 (above 83) for at least 20 years. This is the isotope-era definition of Usoskin et al. (2007) on the version-2 scale; see `detect_solar_episodes`.

In the example the Oort (1010–1049), Wolf (1278–1342), Spörer (1452–1552) and Maunder (1644–1716) minima are found, and the Modern Maximum in two parts (1932–1965 and 1975–2003, split by the weak cycle 20). Two high-activity spells that the SILSO table implies also appear: 1767–1792 (cycles 2–4) and 1832–1851 (cycles 8–9). The Medieval Maximum is not detected: its synthetic cycles scatter around the threshold.

The Dalton Minimum is not a grand minimum by this definition. Its decadal mean stayed near 46.

## 2. What changes with solar activity

### 2.1 Irradiance: the cycle and the slow background

The cycle part is the same proxy model as in the cycle module (facular brightening minus spot darkening). On centuries, the quiet Sun's network also brightens with the activity of the preceding decades: the open flux and the ephemeral regions that survive solar minima. It enters as an extra facular index:

x_bg = k (M(t) − M_ref) / SN_ref

- M is a 15-year exponential moving average of the annual sunspot number.
- M_ref (94) is its mean over 1976–2019, the satellite era in which the quiet-Sun TSI is calibrated.
- k is uncertain, so three scenarios span it:
  - **low** (k = 0, cycle only),
  - **central** (k = 0.7),
  - **high** (k = 3.5).

| TSI of the Maunder Minimum against 1976–2019 | low | central | high |
|---|---|---|---|
| Model | −0.5 W/m² | −1.1 W/m² (−0.08 %) | −3.6 W/m² |
| Literature | | recent reconstructions ~0.5–1.5 W/m² (SATIRE, NRLTSI; PMIP4) | older estimates up to several W/m² |

The UV of the ozone-making 200–242 nm band falls by 2 % against the pre-industrial mean (4.7 % against the Modern Maximum). Total ozone falls by 0.7 % and the upper stratosphere cools by 0.34 K.

### 2.2 Cosmic rays and cosmogenic isotopes: how we know

A weaker solar wind and heliospheric field let more galactic cosmic rays reach the Earth. In the atmosphere they make ¹⁴C (stored in tree rings) and ¹⁰Be (stored in polar ice). The records of these isotopes are how grand minima before the telescope are known.

- **Modulation potential** (MV): φ = 150 + 4 (½ SN₃ᵧ + ½ M), with SN₃ᵧ the 3-year mean.
- **Relative production:** Q/Q_ref = ((φ_ref + 300) / (φ + 300))^0.53, with φ_ref = 513 MV (the satellite era).
- **Calibration:** the exponent is chosen so that a Maunder-like minimum raises the production by about 30 % over the modern mean, as the records show. This is a calibrated proxy, not cosmic-ray transport; geomagnetic-field changes, which also modulate the production over millennia, are not included.

In the example the production is 1.30 times the satellite era in 1680. Across the grand minima it is +7 to +12 % above the pre-industrial mean, and across the Modern Maximum −14 %.

### 2.3 The climate: the solar part in proportion

The same two-layer energy balance as the cycle module is run annually, separately for each forcing. Each forcing is an anomaly about its pre-industrial mean (start_year–1849), and the temperatures add linearly:

- **Solar:** ΔS (1 − 0.29)/4 for the low, central and high backgrounds, and for the future grand-minimum scenario.
- **Volcanic:** a pulse per eruption, F(t) = F_peak (u/r) e^(1 − u/r) with u the time since the eruption and r = 0.75 years.

  The example lists 15 large eruptions from Samalas (1257) to Pinatubo (1991). Their peak forcings are **rounded orders of magnitude** (−0.8 to −8 W/m²), and the list is short, so the volcanic contribution is indicative. Use eVolv2k or the CMIP6/PMIP4 volcanic forcing for quantitative work.
- **CO₂:** 5.35 ln(C/C₀), with C from ice-core and Mauna Loa values (rounded, including the dip to about 272 ppm around 1610) and an illustrative path to 600 ppm in 2100 (roughly SSP2-4.5). Other greenhouse gases, aerosols, land use and orbital forcing are not included.

**Regional winter (illustrative).** A parametrisation of the top-down pathway: ΔT_winter = 2 ΔT_global + 0.1 K per % of 200–242 nm UV. Less UV means less ozone and heating in the stratosphere, a weaker winter polar vortex and more blocking over the North Atlantic, which brings cold winters to northern and central Europe (Shindell et al. 2001; Ineson et al. 2015). It is not a circulation model; it shows that regional effects can be several times the global ones.

Results against the pre-industrial mean (1000–1849), which itself contains several grand minima. Solar values are central (low … high).

| Epoch | TSI vs today | ¹⁴C/¹⁰Be | Solar | Volcanic | CO₂ | Total | Winter (regional) |
|---|---|---|---|---|---|---|---|
| Oort Minimum | −0.96 W/m² | +8 % | −0.03 (−0.02 … −0.09) K | +0.04 | 0.00 | +0.01 | −0.13 |
| Medieval Maximum | −0.05 | −11 % | +0.06 (+0.03 … +0.19) | +0.05 | +0.01 | +0.12 | +0.47 |
| Spörer Minimum | −1.10 | +12 % | −0.05 (−0.02 … −0.14) | −0.01 | +0.02 | −0.04 | −0.28 |
| **Maunder Minimum** | **−1.10** | **+12 %** | **−0.05 (−0.03 … −0.16)** | −0.01 | −0.05 | **−0.11** | **−0.42** |
| Dalton Minimum | −0.46 | −4 % | +0.02 | **−0.21** | +0.02 | −0.17 | −0.30 |
| Modern Maximum | +0.10 | −14 % | +0.08 (+0.04 … +0.27) | −0.07 | **+0.56** | +0.57 | +1.42 |

What the model says:

1. **Grand minima cool the globe a little.** The Maunder Minimum's dimmer Sun cools by 0.05 K (0.03–0.16 K across the background scenarios) against the pre-industrial mean, about 0.13 K against the Modern Maximum. This is the range of model studies (e.g. Feulner 2011).
2. **The cold of the Little Ice Age was not mainly solar.** The model's Dalton-era cold is volcanic (1809, Tambora 1815: the "year without a summer" 1816), not solar. The Maunder Minimum coincided with a CO₂ low (about 272–277 ppm) and with further eruptions (the 1641 and 1695 events in the list; the real record has more). Reconstructions and models attribute most of the Little Ice Age to volcanic clusters and internal variability, with the Sun secondary (Schurer et al. 2014). The short, rounded eruption list here understates the volcanic part.
3. **Regional winter cold can be several times the global value.** −0.42 K in the illustrative European winter column for the Maunder Minimum. This is the pathway behind the severe winters (Thames frost fairs, the Great Frost of 1683–84).
4. **Solar activity did not cause the recent warming.** Since the Modern Maximum the Sun has contributed +0.08 K against +0.56 K from CO₂ alone (the real anthropogenic total, with other gases and aerosols, is larger).

## 3. The Maunder Minimum: what happened and what caused it

- **Observed.** Telescopic observation began in 1610 (Galileo, Harriot, Scheiner, Fabricius). Between 1645 and 1715 careful observers at Paris, Danzig and Greenwich (Picard, Cassini, La Hire, Hevelius, Flamsteed) recorded only a few dozen spot groups in seventy years, often none for years.
  - The few spots appeared mostly in the southern hemisphere (Ribes & Nesme-Ribes 1993).
  - Aurorae became rare in Europe.
  - Spörer and Maunder noticed the gap in the 1880s and 1890s. Eddy (1976) established it with the ¹⁴C of tree rings and historical aurora reports.
  - A reassessment of all data sets (Usoskin et al. 2015) concludes that it was a real grand minimum, with sunspot numbers far below any modern minimum, not a lack of observers.
  - The ¹⁰Be of the ice cores shows a weak 11-year magnetic cycle throughout (Beer, Tobias & Weiss 1998): **the dynamo did not stop; it stopped making sunspots.**
- **Cause.** The solar dynamo itself, not an external trigger. §4 shows how a dynamo can fall into and out of such a state by chance.
- **Effects.** The numbers in §2, and in the viewer's case-study panel: TSI −1.1 W/m² (central), UV −2 %, ozone −0.7 %, ¹⁴C/¹⁰Be +30 % over the modern mean, global −0.05 K, regional winter −0.4 K (illustrative).

## 4. Why grand minima happen: a stochastic dynamo map

The solar cycle is made by a dynamo. Differential rotation winds the poloidal field into a toroidal field, whose strongest flux ropes rise as sunspot groups. As the tilted groups decay, their flux drifts poleward and rebuilds the poloidal field with reversed sign: the **Babcock–Leighton mechanism**. This source depends on the tilts of the groups (Joy's law), which scatter strongly from group to group, so the poloidal field, and with it the next cycle, varies randomly from cycle to cycle. The map follows Charbonneau et al. (2005) and Passos et al. (2014), who add a weak mean-field α effect that works without spots.

For cycle n, with toroidal field b_n:

- **Emergence:** e(b) = ½ [1 + erf((b − b_e)/w)] is the share of the field strong enough to rise as spots (b_e = 0.5, w = 0.1).
- **Next field:** b_(n+1) = | α_BL (1 + σ ε_n) e(b_n) + α_w | · b_n / (1 + (b_n/b_q)²) + η |ζ_n|
  - α_BL (1 + σ ε_n): the Babcock–Leighton source, random from cycle to cycle (σ = 0.73). It can even reverse when badly tilted groups dominate.
  - α_w = 1.033: the weak, slightly supercritical turbulent α effect.
  - b_q = 1.5: the quenching that caps strong fields.
  - η = 0.01: a small random floor.
  - α_BL = 1 + 1/b_q² − α_w (0.411) puts the mean state at b = 1.
- **Spots:** A_n = 121 b_n² e(b_n), the cycle's sunspot maximum.

**How a grand minimum happens in the map.** A strongly negative fluctuation of the Babcock–Leighton source (e.g. an efficiency of −1.24 in the example) leaves the next toroidal field below the emergence threshold. Spots vanish (the emergence falls below 10 %), and without spots the Babcock–Leighton source switches off. Only the weak α effect remains: it keeps a faint magnetic cycle going (the 11-year ¹⁰Be signal of the Maunder Minimum) and lets the field grow back slowly. When chance lifts it above the threshold, spots return and the Babcock–Leighton source takes over again. Durations and waiting times are random: there is no regular clock.

**Calibration to the Holocene** (14C statistics of Usoskin, Solanki & Kovaltsov 2007, rounded; 100,000 cycles of the map, 10.93 years per cycle):

| | Map | Isotope records |
|---|---|---|
| Grand minima per 11,400 years | 27 | 27 |
| Share of time in grand minima | 16 % | 17 % |
| Mean length of a grand minimum | 67 years | ~70 years |
| Mean wait between grand minima | 356 years | |
| Grand maxima per 11,400 years | 21 | 19 |
| Share of time in grand maxima | 5 % | 9 % |
| Mean length of a grand maximum | 26 years | ~53 years |

- **Grand minima** are reproduced, including their skewed durations: many short ones (Maunder-type, 2–7 cycles) and a few long ones (Spörer-type, 10 cycles and more).
- **Grand maxima** are too short and too rare. Lasting high activity needs a slower memory than one cycle, for instance variations of the meridional flow, which this one-dimensional map lacks.

The viewer shows a synthetic Holocene of the map (11,400 years, 29 grand minima) next to the example.

## 5. A new grand minimum?

**Probability.** The map is started from the last two cycles (the fits of cycles 24 and 25: 112 and 150) and run 20,000 times. A grand minimum counts as begun once two consecutive cycles stay below the threshold (cycle mean below 25). That happens in:

| By cycle (peak) | 27 (2046) | 29 (2068) | 31 (2089) | 33 (2111) | 35 (2133) |
|---|---|---|---|---|---|
| Grand minimum begun | 2.0 % | 7.0 % | 12.4 % | 17.8 % | 22.5 % |

A constant rate of 27 per 11,400 years would give 16 % by 2100. For the next cycle the map is climatology (132, 57–237), because it has no polar-field precursor; the statistical forecast of the cycle module is used for the next cycles, and the map only for the risk of a regime change.

**What it would change.** In the scenario, cycles 26–31 have a maximum of 8 (Maunder-like); the difference is taken against the regular continuation:

| | 2040 | 2050 | 2060 | 2075 | 2090 |
|---|---|---|---|---|---|
| TSI | −0.34 W/m² | −0.58 | −0.83 | −0.69 | −1.58 |
| Global surface | −0.06 K | −0.09 | −0.11 | −0.09 | −0.13 |
| Regional winter (illustrative) | −0.22 K | −0.37 | −0.51 | −0.42 | −0.95 |
| CO₂ warming since 2026 (illustrative path) | +0.31 K | +0.53 | +0.71 | +0.94 | +1.14 |

At most −0.13 K (2091, when a regular maximum would have peaked). A new grand minimum would slow the warming by roughly a decade's worth at most, not stop it. This agrees with Feulner & Rahmstorf (2010), who found at most −0.3 K by 2100, and with Meehl et al. (2013) and Anet et al. (2013). Regionally, winter effects could be several tenths of a kelvin.

## 6. Request and output

```json
"long_term": {
  "start_year": 1000, "horizon_year": 2100, "normal_amplitude": 120, "transition_years": 12,
  "historical_cycles": {"period_years": 11.0, "period_sd_years": 1.0, "amplitude_log_sd": 0.25, "seed": 7},
  "epochs": [{"name": "Maunder Minimum", "kind": "grand_minimum", "start_year": 1645, "end_year": 1715, "cycle_amplitude": 8, "evidence": "..."}, ...],
  "episodes": {"window_years": 11, "min_years": 20, "grand_minimum_mean": 25, "grand_maximum_mean": 83},
  "secular_background": {"timescale_years": 15, "reference_start_year": 1976, "reference_end_year": 2019, "low": 0.0, "central": 0.7, "high": 3.5},
  "cosmic_rays": {"phi_min_mv": 150, "phi_per_sunspot_mv": 4.0, "background_weight": 0.5, "production_offset_mv": 300, "production_exponent": 0.53},
  "volcanic": {"rise_years": 0.75, "eruptions": [{"name": "Tambora", "year": 1815.3, "peak_forcing_w_m2": -6}, ...]},
  "co2_ppm": [[1000, 279], ..., [2100, 600]],
  "regional_winter": {"name": "...", "global_amplification": 2.0, "top_down_k_per_uv_pct": 0.1},
  "future": {"grand_minimum": {"first_cycle": 26, "cycles": 6, "amplitude": 8}},
  "dynamo": {"weak_alpha": 1.033, "babcock_leighton_noise": 0.73, "emergence_threshold": 0.5, "...": "...", "targets": {...}},
  "case_study": {"epoch": "Maunder Minimum", "from_year": 1600, "to_year": 1760}
}
```

Unknown keys are rejected. The output `solar.long_term` holds:

- `series`: annual values from start_year to the horizon:
  - the sunspot number and its decadal mean, the grand-minimum scenario, the source of each year (envelope, table, record, forecast) and the envelope;
  - the background M, TSI (low, central, high, scenario), UV 200–242 nm, ozone and the stratosphere;
  - φ and the cosmogenic production, CO₂;
  - the forcings and the temperatures of each component;
- `epochs`: every catalogue epoch with its means and anomalies;
- `episodes` (detected), `cycles` (every cycle with its source), `future`, `dynamo` (configuration, statistics, targets, example, synthetic Holocene, outlook) and `case_study`.

## 7. Verification

- **`weather.solar_cycle`** (C++):
  - the envelope (inside and outside epochs, annotation epochs, the log-midpoint at a boundary, monotone transitions, overlapping epochs rejected);
  - the historical cycles (backward numbering, 8–15-year lengths, weak cycles in the Maunder Minimum), annual means;
  - the moving average (1 − 1/e after τ), the running mean and episode detection (a 40-year low, a 30-year high, a too-short dip ignored);
  - cosmic rays, the volcanic pulse (peak at the rise time, integral F r e, annual means keep it) and CO₂ doubling;
  - the dynamo map: emergence, the inverse field, the fixed point, reproducibility, the map identity, a dead dynamo staying dead without the floor and reviving with it, episodes and statistics of a hand-made sequence, the calibration to the Holocene statistics, and the outlook.
- **`weather.solar_cycle_cli`:** the annual record means, the moving average, TSI for the three backgrounds and the cosmogenic production, all recomputed from the output. Also checked:
  - the forcings average to zero before 1850, the energy balance is integrated again, and total = solar + volcanic + CO₂;
  - the Maunder Minimum's ordering (high < central < low < 0 in TSI and temperature), the detected grand minima, consecutive cycles from the envelope to the continuation;
  - a future grand minimum only cools, the dynamo is calibrated, and spots vanish in its example;
  - five new invalid requests are rejected.
- **Viewer:** `solar.test.js` covers the long-term validation, windows, the values of a year, epoch bands and table, the dynamo example, the synthetic Holocene and the rejection of broken blocks.

## 8. Limitations

- **Historical cycles:** before the table they are synthesised around a catalogue: illustrative, not a reconstruction. The catalogue's dates are rounded and some are debated, for example the start of the Spörer Minimum.
- **Slow background:** the brightening of the quiet Sun is the main uncertainty of the long-term TSI; the three scenarios span it. There is no spectral model of the background.
- **Cosmogenic production:** a calibrated proxy, with no geomagnetic-field change and no carbon-cycle model (the ¹⁴C in tree rings is the production filtered by the carbon cycle).
- **Comparison forcings:** rounded and incomplete. The volcanic list has 15 eruptions and no aerosols or land use; the CO₂ path after 2025 is illustrative.
- **Regional winter response:** an illustrative parametrisation.
- **Dynamo map:** conceptual and one-dimensional. It has no hemispheres, so it cannot show the Maunder Minimum's southern-hemisphere spots, and no meridional-flow memory. Grand maxima are too short and too rare. Its cycle-to-cycle forecast is climatology.

## References

Beer, Tobias & Weiss 1998; Charbonneau, St-Jean & Zacharias 2005; Eddy 1976 (*The Maunder Minimum*); Feulner & Rahmstorf 2010; Feulner 2011; Ineson et al. 2015; Meehl et al. 2013; Anet et al. 2013; Passos et al. 2014; Ribes & Nesme-Ribes 1993; Schurer et al. 2014; Shindell et al. 2001; Solanki et al. 2004; Svalgaard & Schatten 2016; Usoskin, Solanki & Kovaltsov 2007; Usoskin et al. 2015 (the Maunder Minimum reassessed); Usoskin 2017 (*A history of solar activity over millennia*, Living Reviews).
