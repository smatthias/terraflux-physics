# Timestamped precipitation accumulation

`terraflux-accumulate-rainfall` integrates estimated precipitation-rate observations over time. For each exact CRS/coordinate location, it applies the trapezoidal rule between adjacent valid-time samples:

[
P = sum_i rac{R_i + R_{i+1}}{2} Delta t_i
]

where (R) is estimated rate in mm/h and (Delta t) is elapsed time in hours. The output is an accumulation in millimetres, with `period_start_utc` marking the interval start and `valid_time_utc` marking the end.

## Run

First decode a sequence of MRMS frames and apply the dBZ-to-rate conversion to each frame. Concatenate the canonical precipitation-rate observation arrays into a single JSON array, then run:

~~~sh
build/terraflux-accumulate-rainfall \
  /tmp/mrms-rates-series.json \
  /tmp/mrms-accumulation.json
~~~

The input must contain at least two distinct UTC timestamps. All samples must use `precipitation_rate` in `mm/h` and share source and derivation metadata. Locations are matched by exact CRS and coordinate values. The output contains one accumulation observation for every location seen in any frame.

If any location has a missing or no-data sample during the interval, its accumulation is marked `no_data`; ambiguous or duplicate samples make it `ambiguous`. The accumulator never fills gaps or silently bridges missing frames. It requires a sample at each timestamp for every location.

## Interpretation limits

The trapezoidal rule assumes rates vary linearly between the supplied samples. It accumulates the upstream empirical radar rate estimates; it does not improve their accuracy, correct radar sampling height, classify rain versus snow/hail, or model storm motion. Long gaps can make the linear-rate assumption poor. Accumulations remain separate from the current dry temperature transport model.
