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
    outlets=json.loads((root/'examples/hydrology-outlet-scenario.json').read_text())
    config.write_text(json.dumps(outlets));output=directory/'outlets'
    subprocess.run([sys.argv[1],str(output),str(config)],check=True)
    w=json.loads((output/'surface-water.json').read_text())
    assert w['cumulative_discharge_volume_m3'][-1]>0
    for i,rain in enumerate(w['cumulative_rainfall_volume_m3']):
        total=w['stored_volume_m3'][i]+w['soil_volume_m3'][i]+w['cumulative_evaporation_volume_m3'][i]+w['cumulative_drainage_volume_m3'][i]+w['cumulative_discharge_volume_m3'][i]
        assert abs(total-rain)<1e-7
    for i,mutate in enumerate([lambda d:d['outlets'][0].update(x=5,y=5),lambda d:d['outlets'].append(dict(d['outlets'][0])),lambda d:d['outlets'][0].update(x=0,y=9)]):
        invalid=copy.deepcopy(outlets);mutate(invalid);config.write_text(json.dumps(invalid));output=directory/f'bad-outlet-{i}'
        assert subprocess.run([sys.argv[1],str(output),str(config)],capture_output=True).returncode!=0
        assert not output.exists()
print('hydrology scenario validation, custom run and outlet budget checks passed')
