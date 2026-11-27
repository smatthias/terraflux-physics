// Explicit global cell-centred relief contract. Rows run south to north; columns west to east.
// Elevations are metres relative to mean sea level; negative values represent the seafloor.
export function validateGlobalRelief(doc) {
  if (doc?.schema !== 'terraflux.global-relief/v1') throw new Error('Expected terraflux.global-relief/v1')
  if (doc.units !== 'm' || doc.datum !== 'mean_sea_level' || doc.row_order !== 'south_to_north') throw new Error('Relief requires metres, mean sea level and south-to-north rows')
  const { nx, ny } = doc
  if (!Number.isInteger(nx) || !Number.isInteger(ny) || nx < 2 || ny < 2 || nx * ny > 1000000) throw new Error('Invalid relief grid (maximum 1,000,000 cells)')
  if (typeof doc.source !== 'string' || !doc.source.trim()) throw new Error('Relief source is required')
  if (!Array.isArray(doc.elevation_m) || doc.elevation_m.length !== nx * ny || doc.elevation_m.some(v => v !== null && (typeof v !== 'number' || !Number.isFinite(v) || v < -12000 || v > 10000))) throw new Error('Invalid elevation values or grid length')
  return doc
}

// Nearest cell preserves nodata and coast discontinuities. Both grids span [-180,180] x [-90,90].
export function resampleGlobalRelief(doc, nx, ny) {
  validateGlobalRelief(doc)
  if (!Number.isInteger(nx) || !Number.isInteger(ny) || nx < 1 || ny < 1 || nx * ny > 1000000) throw new Error('Invalid target grid')
  return Array.from({ length: nx * ny }, (_, i) => {
    const x = Math.min(doc.nx - 1, Math.floor(((i % nx) + 0.5) * doc.nx / nx))
    const y = Math.min(doc.ny - 1, Math.floor((Math.floor(i / nx) + 0.5) * doc.ny / ny))
    return doc.elevation_m[y * doc.nx + x]
  })
}

// Display-only exaggeration, bounded to keep the sphere and its overlays readable.
export function globeReliefHeight(metres, radius = 9, exaggeration = 20) {
  return Number.isFinite(metres) ? Math.max(-radius * 0.04, Math.min(radius * 0.04, metres / 6371008.8 * radius * exaggeration)) : 0
}
