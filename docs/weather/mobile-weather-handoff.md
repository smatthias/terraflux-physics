# Mobile weather handoff contract

This document defines the JSON boundary for a mobile client consuming the current weather runner. The schemas formalize the file contract; they do not add a mobile UI, network endpoint, or in-process mobile binding.

## Request

Use [temperature-run-request-v1.schema.json](temperature-run-request-v1.schema.json). The request supplies UTC start time, fixed timestep and step count, grid geometry, transport settings, and complete initial temperature and wind arrays.

For a sequence of maps, pass the separate [temperature-map-series-v1.schema.json](temperature-map-series-v1.schema.json) manifest to the runner with `--series`. Each map's sidecar valid time must coincide with a configured timestep boundary.

## Response

Validate the runner's JSON with [temperature-forecast-v1.schema.json](temperature-forecast-v1.schema.json). Grid values are row-major: `index = y * nx + x`. Temperature is in kelvin; east/north winds are in metres per second. The `observation_updates` list reports the valid time and quality counts for every applied map frame.

A mobile view should retain the grid geometry and update timestamps when rendering. Treat missing and ambiguous observations as quality information; they are not numerical zeroes. The forecast field arrays contain the final model state.

## Current integration boundary

The current executable reads local files and writes a local JSON file. It does not expose a mobile API, fetch a live weather provider, or support an evolving wind field. The data examples are synthetic. Provider ingestion requires a licensed sample and verified legend; see [map-source-status](map-source-status.md).

For a mobile build, the next implementation is a thin adapter around the C++ model contract (or a service wrapper) that accepts this request and returns this response without changing units, row ordering, or quality metadata.
