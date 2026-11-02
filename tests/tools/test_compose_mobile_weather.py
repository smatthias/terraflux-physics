import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('composer', Path(__file__).resolve().parents[2] / 'tools/compose_mobile_weather.py')
composer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(composer)


class ComposerTest(unittest.TestCase):
    def setUp(self):
        grid = dict(nx=2, ny=2, origin_east_m=0, origin_north_m=0,
                    spacing_east_m=100, spacing_north_m=100,
                    geographic_anchor_longitude_deg=0, geographic_anchor_latitude_deg=0,
                    coordinate_system='local_azimuthal_equidistant_m', array_order='row_major_y_then_x')
        self.forecast = dict(schema_version='terraflux.weather.forecast/v1', grid=grid,
                             start_time_utc='2026-11-01T00:00:00Z', elapsed_seconds=60,
                             observation_updates=[], fields={
                                 name: dict(units=units, values=[value]*4)
                                 for name, units, value in [('air_temperature', 'K', 290),
                                                           ('wind_east', 'm/s', 0), ('wind_north', 'm/s', 0)]})
        self.rain = dict(schema_version='terraflux.weather.gridded_accumulation/v1',
                         grid=copy.deepcopy(grid), precipitation_accumulation=dict(
                             period_start_utc='2026-11-01T00:00:00Z', valid_time_utc='2026-11-01T00:01:00Z',
                             units='mm', method='trapezoidal_rate_integration',
                             values=[0, 2, None, None], quality=['decoded', 'decoded', 'no_data', 'ambiguous']))

    def test_preserves_missing_and_zero(self):
        result = composer.compose(self.forecast, self.rain)
        self.assertEqual(result['precipitation_accumulation']['values'], [0, 2, None, None])

    def test_rejects_grid_time_length_quality_and_nonfinite_errors(self):
        mutations = [
            lambda f, r: r['grid'].update(spacing_east_m=101),
            lambda f, r: r['precipitation_accumulation'].update(valid_time_utc='2026-11-01T00:02:00Z'),
            lambda f, r: r['precipitation_accumulation'].update(period_start_utc='2026-10-31T23:59:00Z'),
            lambda f, r: r['precipitation_accumulation']['values'].pop(),
            lambda f, r: r['precipitation_accumulation']['values'].__setitem__(2, 0),
            lambda f, r: r['precipitation_accumulation']['values'].__setitem__(0, -1),
            lambda f, r: f['fields']['wind_east']['values'].__setitem__(0, float('nan')),
            lambda f, r: f.update(elapsed_seconds=True),
        ]
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                f, r = copy.deepcopy(self.forecast), copy.deepcopy(self.rain)
                mutation(f, r)
                with self.assertRaises(ValueError):
                    composer.compose(f, r)


if __name__ == '__main__':
    unittest.main()
