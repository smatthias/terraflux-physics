// Minimal engine frames for the station report tests (station, aviation and sailing): a 3 x 3 grid of 100 km cells
// anchored at 45 N 10 E, with every field overridable. Test support only; the viewer does not import it.
export const grid = (over = {}) => ({ nx: 3, ny: 3, origin_east_m: -150000, origin_north_m: -150000, spacing_east_m: 100000, spacing_north_m: 100000, geographic_anchor_longitude_deg: 10, geographic_anchor_latitude_deg: 45, coordinate_system: 'local_azimuthal_equidistant_m', array_order: 'row_major_y_then_x', ...over })
// A frame with one value (or a function of the cell index) per field.
export function frameOf({ g = grid(), t = 288.15, u = 0, v = 0, elapsed = 0, ...extra } = {}) {
  const n = g.nx * g.ny, fill = x => Array.from({ length: n }, (_, i) => (typeof x === 'function' ? x(i) : x))
  return { forecast: { grid: g, start_time_utc: '2026-11-01T12:00:00Z', elapsed_seconds: elapsed, fields: { air_temperature: { values: fill(t) }, wind_east: { values: fill(u) }, wind_north: { values: fill(v) } } }, ...extra }
}

