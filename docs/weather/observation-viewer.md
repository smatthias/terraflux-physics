# Radar observation workspace

Choose **Observations** in the viewer header, then **Open decoded observation JSON**. The workspace accepts the array written by `tools/decode_noaa_mrms.py` (canonical `terraflux.weather.observation/v1` records with radar reflectivity in dBZ). Observation state is independent of the Observatory simulation and Coupled Earth: loading a file does not alter either model or timeline.

## NOAA workflow

```sh
python3 tools/fetch_noaa_mrms.py /tmp/terraflux-mrms
python3 tools/decode_noaa_mrms.py \
  /tmp/terraflux-mrms/MRMS_MergedReflectivityComposite.latest.grib2 \
  /tmp/mrms-observations.json --bbox=-123,37,-122.8,37.2 --max-points 100000
```

The GRIB2 decoder requires `wgrib2` and MRMS tables. Open its resulting JSON in the Observations workspace. Fetching is an explicit command; the viewer does not fetch NOAA or refresh the imported file automatically. Bounds and limits are deliberate: maximum 64 MB per file and 100,000 records. Keep crops small.

## Display semantics

- Records must share a source, product, interpretation and vertical reference. Mixed products, duplicate coordinates at a valid time, invalid geographic coordinates and inconsistent quality/value combinations are rejected.
- UTC valid times are kept independently of the simulation clock, sorted into frames, with a valid-time slider. This first importer accepts second-resolution UTC timestamps in `YYYY-MM-DDTHH:mm:ssZ`, as emitted by the decoder.
- Decoded numeric zero is retained. `no_data` and `ambiguous` records remain null with separate counts and their reason in the point inspector. Unreported locations have unknown coverage, not zero reflectivity.
- The longitude/latitude scatter preview has north up and east right, with axis bounds beneath it. It is a preview without a cartographic basemap or corrected geographic aspect ratio. At most 5,000 points are drawn; when sampled, this is stated and counts still include all records. No interpolation fills gaps.
- Age is computed against the current clock and refreshed every 30 seconds. Older than 15 minutes, within 15 minutes, and future timestamps are labelled. This is a display threshold, not a NOAA freshness guarantee. It does not assert that an imported file is live.
- Source ID, product and vertical reference remain visible. Synthetic fixtures carry the `SYNTHETIC FIXTURE` badge; other sources carry `IMPORTED OBSERVATIONS`.
- Reflectivity is not surface rainfall. Any empirical rain-rate estimate remains a separate derived product, not substituted into this view.

`viewer/public/observations/mrms-synthetic.json` is a two-frame fixture produced using the decoder's observation constructor and explicitly marked synthetic. The example button loads this fixture, not live NOAA data.

## Verification and remaining work

Tests cover decoder-format acceptance, time ordering, quality counts, zero/no-data distinction, invalid dates and locations, mixed sources, duplicate samples, display decimation and observation-age labels. Production build verification covers the Vue integration. Real MRMS acquisition-to-GRIB2-decoding-to-viewer verification remains pending: `wgrib2` is unavailable in the current environment. Rendered browser inspection also remains pending due to unavailable browser binaries. There is no assimilation, automatic polling, regridding to the simulation or live-source authentication in this workspace.
