# Water timestep sensitivity regression

Run:

```sh
python3 tools/check_water_timestep.py --build-dir build
python3 tools/check_water_timestep.py --build-dir build --check
ctest --test-dir build -R water_timestep_sensitivity --output-on-failure
```

The check runs the production C++ example generator at 15, 30 and 60 second hydrology timesteps, retaining identical terrain, coverage, atmospheric state, rainfall, process rates, capacities and output times. Atmospheric transport remains at 30 seconds. Each run uses the same wet/taper/dry forcing over 130 minutes, with 14 saved snapshots.

For every snapshot it verifies nonnegative finite surface storage, bounded soil storage, unchanged coverage, cell-derived storage volumes and the complete surface + soil + evaporation + drainage budget against rainfall. It records final volume partitions, peak final cell depth, maximum budget residual and the final surface-depth L1 difference from the 15-second reference. L1 is the sum of absolute per-cell depth differences multiplied by cell area, expressed in m³ and as a percentage of covered cumulative rain input. This measures spatial redistribution differences even when total volume differences cancel.

The committed synthetic case currently measures approximately 0.208% of rainfall at 30 seconds and 0.611% at 60 seconds relative to 15 seconds. Maximum stored mass residuals are around 1e-9 m³. Full precision values and applied scenario parameters are in `viewer/public/examples/water-timestep-sensitivity.json`.

CTest regenerates the report and compares numbers with a relative tolerance of 1e-8 (absolute floor 1e-8), plus the independent per-snapshot mass checks. A changed report requires inspection and explicit regeneration. There is no arbitrary physical-accuracy pass threshold. The 15-second run is a comparison reference, not ground truth; three timesteps on one synthetic case do not establish convergence, timestep independence, calibrated runoff behavior or validity at other grid scales. Donor limiting preserves positivity but does not establish time-integration accuracy.

`--scenario` and `--output` allow a separate report for another prescribed configuration without replacing the committed baseline report.
