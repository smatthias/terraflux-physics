"""Check the built generator's hydrology configuration boundary."""
import copy
import json
from pathlib import Path
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parents[2]
default=json.loads((root/'examples/hydrology-scenario.json').read_text())
with tempfile.TemporaryDirectory() as temporary:
    directory=Path(temporary)
    config=directory/'scenario.json'
    mutations=[lambda d:d.update(timestep_seconds=0),lambda d:d.update(timestep_seconds=7),
               lambda d:d.update(infiltration_rate_m_s=-1),lambda d:d.update(routing_speed_m_s=True),
               lambda d:d.update(boundary='open'),lambda d:d.update(unknown=1),
               lambda d:d.pop('soil_storage_capacity_m'),lambda d:d.update(evaporation_rate_m_s='0')]
    for i,mutation in enumerate(mutations):
        invalid=copy.deepcopy(default);mutation(invalid);config.write_text(json.dumps(invalid))
        output=directory/f'invalid-{i}'
        result=subprocess.run([sys.argv[1],str(output),str(config)],capture_output=True,text=True)
        assert result.returncode!=0,result.stdout
        assert not output.exists(),'invalid config created outputs'
    custom=copy.deepcopy(default)
    custom.update(timestep_seconds=60,routing_speed_m_s=0,infiltration_rate_m_s=0,evaporation_rate_m_s=0,drainage_rate_m_s=0)
    config.write_text(json.dumps(custom));output=directory/'valid'
    subprocess.run([sys.argv[1],str(output),str(config)],check=True)
    water=json.loads((output/'surface-water.json').read_text())
    assert water['timestep_seconds']==60
    assert all(v==0 for v in water['soil_volume_m3'])
    assert all(v==0 for v in water['cumulative_evaporation_volume_m3'])
    assert all(v==0 for v in water['cumulative_drainage_volume_m3'])
    assert all(abs(v-r)<1e-6 for v,r in zip(water['stored_volume_m3'],water['cumulative_rainfall_volume_m3']))
    assert json.loads((output/'water-scenario.json').read_text())==custom
print('hydrology scenario validation and custom-run checks passed')
