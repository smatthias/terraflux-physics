# Controlled basin regression

`weather.surface_water_basin` exercises the existing C++ routing kernel on a 5 × 3 metric grid. Two zero-elevation upstream cells are separated from two downstream cells by a 1 m spill cell. The side walls are 5 m high, all cells are active, and the outer domain remains closed. Cell spacing is 1 m, routing speed is 0.1 m/s and timestep is 0.1 s. No infiltration or losses are applied.

| Case | Expected behavior |
| --- | --- |
| Initial upstream depth 0.5 m | Stable pond; spill cell and downstream cells stay dry |
| Initial upstream depth 1 m | No transfer at equal head with the dry crest |
| Initial upstream depth 1.2 m | Water crosses the crest and reaches downstream storage |
| Crest raised to 1.5 m | The 1.2 m pond remains retained |
| Repeated upstream rain pulses | Pond fills below crest, then begins internal overflow |

Every step checks nonnegative finite storage, dry high side walls, total stored water against initial water plus rain, and the step mass residual. The above-crest case also checks that the upstream pond does not drain below the spill elevation at this resolved timestep. Decimal equality around the rain-filled threshold is not used as an exact floating-point assertion.

```sh
ctest --test-dir build -R surface_water_basin --output-on-failure
```

This is internal overflow into another part of the same closed domain, not external discharge or a new open-boundary condition. The test confirms qualitative behavior of this controlled routing case; it does not establish spillway discharge accuracy, hydraulic momentum, convergence or correctness at arbitrary timesteps. Large explicit timesteps can still over-redistribute water despite donor limiting. No model parameters or production kernel behavior were changed for this regression.
