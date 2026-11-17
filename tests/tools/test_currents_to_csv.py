"""Tests of tools/currents_to_csv.py: CF time decoding, a NetCDF current field (NetCDF-3 written with scipy when it
is installed) to the viewer's CSV, and NOAA CO-OPS predictions merged with the station position."""
import datetime as dt
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('currents_to_csv', ROOT / 'tools' / 'currents_to_csv.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


class CurrentsToCsvTest(unittest.TestCase):
    def test_cf_times(self):
        t = tool.cf_times([0, 1.5], 'hours since 2026-06-05 00:00:00')
        self.assertEqual(t[1], dt.datetime(2026, 6, 5, 1, 30, tzinfo=dt.timezone.utc))
        self.assertEqual(tool.cf_times([86400], 'seconds since 1970-01-01')[0].date(), dt.date(1970, 1, 2))
        with self.assertRaises(ValueError):
            tool.cf_times([0], 'fortnights since 2026-01-01')

    def test_netcdf_rows_and_csv(self):
        try:
            from scipy.io import netcdf_file
        except ImportError:
            self.skipTest('scipy not installed')
        import numpy as np
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'cmems.nc'
            f = netcdf_file(path, 'w')
            for name, n in (('time', 2), ('depth', 1), ('latitude', 2), ('longitude', 3)):
                f.createDimension(name, n)
            t = f.createVariable('time', 'd', ('time',)); t[:] = [0, 1]; t.units = b'hours since 2026-06-05 00:00:00'
            f.createVariable('depth', 'f', ('depth',))[:] = [0.5]
            f.createVariable('latitude', 'f', ('latitude',))[:] = [54.5, 55.0]
            f.createVariable('longitude', 'f', ('longitude',))[:] = [10.0, 11.0, 370.0]
            u = f.createVariable('uo', 'f', ('time', 'depth', 'latitude', 'longitude')); u.units = b'm s-1'; u._FillValue = np.float32(-999)
            v = f.createVariable('vo', 'f', ('time', 'depth', 'latitude', 'longitude')); v.units = b'm s-1'; v._FillValue = np.float32(-999)
            data = np.full((2, 1, 2, 3), 0.25, dtype='f'); data[0, 0, 0, 0] = -999
            u[:] = data; v[:] = -data
            f.close()
            rows = list(tool.netcdf_to_rows(tool.open_netcdf(path)))
            self.assertEqual(len(rows), 11, 'the fill value is skipped')
            when, lat, lon, uu, vv = rows[-1]
            self.assertEqual((when.hour, lat, lon), (1, 55.0, 10.0), '370 E wraps to 10 E')
            self.assertAlmostEqual(uu, 0.25, places=6); self.assertAlmostEqual(vv, -0.25, places=6)
            self.assertEqual(len(list(tool.netcdf_to_rows(tool.open_netcdf(path), bbox=(10.5, 54, 12, 56)))), 4)
            out = Path(d) / 'out.csv'
            self.assertEqual(tool.main(['netcdf', str(path), str(out), '--stride', '1']), 0)
            text = out.read_text().splitlines()
            self.assertEqual(text[:3], ['# kind: total', '# source: E.U. Copernicus Marine Service Information', 'time_utc,lat,lon,u,v'])
            self.assertEqual(text[3], '2026-06-05T00:00Z,54.50000,11.00000,0.2500,-0.2500')

    def test_noaa_merge_and_urls(self):
        predictions = {'current_predictions': {'units': 'knots', 'cp': [{'Time': '2026-06-05 00:00', 'Velocity_Major': 1.2, 'meanFloodDir': 40, 'meanEbbDir': 220, 'Bin': '1'}]}}
        merged = tool.noaa_merge(predictions, {'stations': [{'id': 'SFB1201', 'name': 'Golden Gate Bridge', 'lat': 37.8113, 'lng': -122.4766}]})
        self.assertEqual((merged['lat'], merged['lng'], merged['station']['id']), (37.8113, -122.4766, 'SFB1201'))
        self.assertEqual(merged['current_predictions'], predictions['current_predictions'])
        with self.assertRaises(ValueError):
            tool.noaa_merge({'foo': 1}, {})
        urls = tool.noaa_urls('SFB1201', '20260605', '20260608', '1', 'MAX_SLACK')
        self.assertIn('product=currents_predictions', urls[0]); self.assertIn('time_zone=gmt', urls[0]); self.assertIn('interval=MAX_SLACK', urls[0]); self.assertIn('&bin=1', urls[0])
        self.assertTrue(urls[1].endswith('/stations/SFB1201.json'))


if __name__ == '__main__':
    unittest.main()
