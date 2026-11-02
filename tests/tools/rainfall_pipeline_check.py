"""End-to-end adapter -> composer check; pass the built gridding executable."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools'))
from compose_mobile_weather import compose

spec = importlib.util.spec_from_file_location('fixtures', Path(__file__).with_name('test_compose_mobile_weather.py'))
fixtures = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixtures)
fixture = fixtures.ComposerTest()
fixture.setUp()
forecast = fixture.forecast
forecast['grid'].update(origin_east_m=-100, origin_north_m=-100)
record = dict(schema_version='terraflux.weather.observation/v1',
              source=dict(source_id='synthetic', source_kind='user_supplied',
                          interpretation='derived_temporal_integration'),
              valid_time_utc='2026-11-01T00:01:00Z', period_start_utc='2026-11-01T00:00:00Z',
              variable='precipitation_accumulation', value=2.5, units='mm',
              location=dict(crs='EPSG:4326', coordinates=[0.00045, 0.00045]),
              vertical_reference='surface', quality=dict(status='decoded'))
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    f, observations, output = [directory / n for n in ('forecast.json', 'observations.json', 'gridded.json')]
    f.write_text(json.dumps(forecast))
    command = [sys.argv[1], str(f), str(observations), str(output)]
    for records, expected in [([record], [None, None, None, 2.5]),
                               ([record, record], [None]*4)]:
        observations.write_text(json.dumps(records))
        subprocess.run(command, check=True)
        result = compose(forecast, json.loads(output.read_text()))
        assert result['precipitation_accumulation']['values'] == expected
        if len(records) == 2:
            assert result['precipitation_accumulation']['quality'][3] == 'ambiguous'
    before = output.read_bytes()
    record['valid_time_utc'] = '2026-11-01T00:02:00Z'
    observations.write_text(json.dumps([record]))
    assert subprocess.run(command).returncode != 0
    assert output.read_bytes() == before
print('adapter -> composer pipeline checks passed')
