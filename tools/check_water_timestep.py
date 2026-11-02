#!/usr/bin/env python3
"""Measure synthetic hydrology timestep sensitivity; not an accuracy proof."""
import argparse
import json
import math
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
def require(ok,message):
    if not ok: raise ValueError(message)

def measure(executable,scenario):
    runs=[]
    with tempfile.TemporaryDirectory(prefix='terraflux-timestep-') as temporary:
        directory=Path(temporary)
        for timestep in (15,30,60):
            config=dict(scenario,timestep_seconds=timestep)
            config_path=directory/f'scenario-{timestep}.json'
            config_path.write_text(json.dumps(config))
            output=directory/f'run-{timestep}'
            subprocess.run([str(executable.resolve()),str(output),str(config_path)],check=True,capture_output=True,text=True)
            water=json.loads((output/'surface-water.json').read_text())
            forecast=json.loads((output/'forecast.json').read_text())
            grid=forecast['grid'];area=grid['spacing_east_m']*grid['spacing_north_m']
            n=grid['nx']*grid['ny'];capacity=config['soil_storage_capacity_m']
            count=len(water['values_by_frame']);require(count>1,'multiple snapshots required')
            initial_mask=[v is None for v in water['values_by_frame'][0]]
            for i in range(count):
                depth=water['values_by_frame'][i];soil=water['soil_values_by_frame'][i]
                require(len(depth)==len(soil)==n,'array length mismatch')
                require([v is None for v in depth]==[v is None for v in soil]==initial_mask,'coverage changed')
                require(all(v is None or math.isfinite(v) and v>=0 for v in depth),'invalid surface storage')
                require(all(v is None or math.isfinite(v) and 0<=v<=capacity+1e-12 for v in soil),'invalid soil storage')
                stored=sum(v or 0 for v in depth)*area;soil_volume=sum(v or 0 for v in soil)*area
                rain=water['cumulative_rainfall_volume_m3'][i]
                evaporation=water['cumulative_evaporation_volume_m3'][i];drainage=water['cumulative_drainage_volume_m3'][i]
                tol=1e-8*max(1,rain)
                require(all(math.isfinite(v) and v>=0 for v in (rain,evaporation,drainage)),'invalid input/loss volume')
                require(abs(stored+soil_volume+evaporation+drainage-rain)<=tol,'mass balance failed')
                require(abs(stored-water['stored_volume_m3'][i])<=tol and abs(soil_volume-water['soil_volume_m3'][i])<=tol,'serialized budget disagrees with cells')
            runs.append((water,forecast))
    reference,reference_forecast=runs[0]
    results=[]
    for (water,forecast),timestep in zip(runs,(15,30,60)):
        require(forecast['grid']==reference_forecast['grid'] and forecast['start_time_utc']==reference_forecast['start_time_utc'] and forecast['elapsed_seconds']==reference_forecast['elapsed_seconds'],'run geometry or duration differs')
        require(forecast['fields']==reference_forecast['fields'],'atmospheric forcing differs')
        require(water['cumulative_rainfall_volume_m3']==reference['cumulative_rainfall_volume_m3'] or all(abs(a-b)<1e-6 for a,b in zip(water['cumulative_rainfall_volume_m3'],reference['cumulative_rainfall_volume_m3'])),'rain forcing differs')
        require(len(water['values_by_frame'])==len(reference['values_by_frame']),'snapshot counts differ')
        area=forecast['grid']['spacing_east_m']*forecast['grid']['spacing_north_m']
        final=water['values_by_frame'][-1];ref=reference['values_by_frame'][-1]
        require([v is None for v in final]==[v is None for v in ref],'run coverage differs')
        l1=sum(abs(a-b) for a,b in zip(final,ref) if a is not None)*area
        rain=water['cumulative_rainfall_volume_m3'][-1]
        results.append(dict(timestep_seconds=timestep,frames_checked=len(water['values_by_frame']),
            surface_volume_m3=water['stored_volume_m3'][-1],soil_volume_m3=water['soil_volume_m3'][-1],
            evaporated_volume_m3=water['cumulative_evaporation_volume_m3'][-1],drained_volume_m3=water['cumulative_drainage_volume_m3'][-1],
            rainfall_volume_m3=rain,max_abs_balance_error_m3=max(abs(v) for v in water['balance_error_m3']),
            peak_final_depth_mm=max(v for v in final if v is not None)*1000,
            final_surface_l1_difference_m3=l1,final_surface_l1_percent_of_rain=100*l1/rain if rain else 0))
    return dict(schema_version='terraflux.water.timestep_sensitivity/v1',case_id='synthetic_wet_taper_dry',
                reference_timestep_seconds=15,scenario=scenario,results=results,
                interpretation='Timestep dependence of this synthetic routing case; not a convergence proof or physical calibration.')

def equivalent(a,b):
    if type(a) in (int,float) and type(b) in (int,float): return abs(a-b)<=1e-8*max(1,abs(a),abs(b))
    if isinstance(a,dict) and isinstance(b,dict):return a.keys()==b.keys() and all(equivalent(a[k],b[k]) for k in a)
    if isinstance(a,list) and isinstance(b,list):return len(a)==len(b) and all(equivalent(x,y) for x,y in zip(a,b))
    return a==b

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build')
    parser.add_argument('--scenario',type=Path,default=ROOT/'examples/hydrology-scenario.json')
    parser.add_argument('--output',type=Path,default=ROOT/'viewer/public/examples/water-timestep-sensitivity.json')
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args()
    report=measure(args.build_dir/'terraflux-generate-viewer-example',json.loads(args.scenario.read_text()))
    if args.check:
        require(equivalent(json.loads(args.output.read_text()),report),'sensitivity report differs; regenerate and inspect changes')
        print('Timestep sensitivity invariants and report regeneration passed')
    else:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
        for row in report['results']:print(f"{row['timestep_seconds']} s: final surface L1 difference vs 15 s = {row['final_surface_l1_percent_of_rain']:.4f}% of rain; max balance error {row['max_abs_balance_error_m3']:.3g} m3")
if __name__=='__main__':main()
