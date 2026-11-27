import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

AVAILABLE = importlib.util.find_spec('netCDF4') is not None
if AVAILABLE:
    import numpy as np
    from netCDF4 import Dataset
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
    from gebco_to_ocean import sample, ocean_request, cell_coordinates


@unittest.skipUnless(AVAILABLE, 'optional netCDF4 dependency not installed')
class RegionalTest(unittest.TestCase):
    def request(self):
        return dict(schema_version='terraflux.ocean.circulation-run/v1', grid=dict(nx=2, ny=2,
                    origin_east_m=-111195., origin_north_m=-111195., spacing_east_m=111195., spacing_north_m=111195.,
                    geographic_anchor_latitude_deg=0., geographic_anchor_longitude_deg=0.), basin={'ocean_mask': [1]*4})

    def fixture(self, path, missing=False, descending=False):
        with Dataset(path, 'w') as ds:
            ds.createDimension('lat', 2); ds.createDimension('lon', 2)
            lat=ds.createVariable('lat','f8',('lat',));lat.units='degrees_north';lat[:]=[.5,-.5] if descending else [-.5,.5]
            lon=ds.createVariable('lon','f8',('lon',));lon.units='degrees_east';lon[:]=[-.5,.5]
            z=ds.createVariable('elevation','f8',('lat','lon'),fill_value=-32768);z.units='m'
            values=np.array([[-40, -32768 if missing else 0], [20,-10]])
            z[:]=values[::-1] if descending else values

    def test_coordinates_depths_and_request(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'grid.nc';self.fixture(path,descending=True)
            req=self.request();b=sample(path,req,'Synthetic')
            self.assertEqual(b['elevation_m'],[-40,0,20,-10])
            self.assertEqual(b['depth_m'],[40,0,0,10])
            self.assertEqual(b['latitude_deg'],[-.5,-.5,.5,.5])
            result=ocean_request(req,b)
            self.assertNotIn('ocean_mask',result['basin'])
            self.assertEqual(result['basin']['terrain']['values'],[-40,0,20,-10])
            self.assertIn('ocean_mask',req['basin'])

    def test_missing_and_outside_are_unknown_not_land_or_water(self):
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'grid.nc';self.fixture(path,missing=True)
            req=self.request();b=sample(path,req,'Synthetic')
            self.assertEqual(b['coverage_mask'],[1,0,1,1])
            self.assertIsNone(b['depth_m'][1]);self.assertIsNone(b['ocean_mask'][1])
            with self.assertRaises(ValueError):ocean_request(req,b)
            req['grid']['origin_east_m']=1111950
            outside=sample(path,req,'Synthetic')
            self.assertEqual(outside['coverage_mask'],[0]*4)

    def test_wrapping_and_invalid_grid(self):
        req=self.request();req['grid']['geographic_anchor_longitude_deg']=180
        _,lon=cell_coordinates(req['grid']);self.assertTrue(all(-180<=v<180 for v in lon))
        req['grid']['spacing_east_m']=-1
        with self.assertRaises(ValueError):cell_coordinates(req['grid'])
