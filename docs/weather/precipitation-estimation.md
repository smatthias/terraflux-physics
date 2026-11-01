# Radar reflectivity to rain-rate estimate

The precipitation estimator is a separate observation transformation; it does not change the dry air-temperature transport model.

For each decoded dBZ value it first computes the linear radar reflectivity factor:

\[
Z = 10^{dBZ/10}
\]

It then applies a configurable empirical power law:

\[
Z = a R^b, \qquad R = (Z/a)^{1/b}
\]

where (Z) is in mm⁶ m⁻³ and estimated rate (R) is in mm/h. The default (a=300, b=1.4) is the WSR-88D default Z-R relation described by the National Weather Service. The equation is not universal: drop-size distributions, hail, snow, beam height, and attenuation can make radar-derived rates diverge from surface rainfall. Treat these outputs as estimates, not measured rainfall. The model does not apply a hail cap or a snow-specific relation.

## Run

After decoding an MRMS crop to canonical observations:

~~~sh
build/terraflux-estimate-rain-rate \
  /tmp/mrms-observations.json \
  /tmp/mrms-rain-rate.json
~~~

To use a different empirically selected relation (Z=aR^b), pass its positive coefficient and exponent after the paths:

~~~sh
build/terraflux-estimate-rain-rate input.json output.json 250 1.2
~~~

The output keeps source, product, timestamp, location, and quality status. It changes the variable to `precipitation_rate`, units to `mm/h`, and records the selected relation in the derivation metadata. No-data and ambiguous inputs remain nonnumeric with their status preserved. The result is not consumed by the current temperature solver.

## Scope and limitations

- This is a pointwise Z-R conversion, not a precipitation accumulation or storm-motion forecast.
- MRMS composite reflectivity can include vertically elevated echoes and multiple radars; no ground correction or vertical profile correction is applied.
- No phase classifier distinguishes rain, snow, hail, or mixed precipitation.
- The coefficients are explicit so a later region-specific or event-specific calibration can be supplied without rewriting the conversion.
