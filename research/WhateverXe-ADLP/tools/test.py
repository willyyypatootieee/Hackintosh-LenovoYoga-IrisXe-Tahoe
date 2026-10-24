#!/usr/bin/env python3
"""Run standalone CPU/mock tests; optional exact-donor replay."""
import argparse,concurrent.futures,json,subprocess,sys
from pathlib import Path
R=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--donor',type=Path);p.add_argument('--accelerator',type=Path);a=p.parse_args()
O=R/'build/tests';O.mkdir(parents=True,exist_ok=True)
python_tests=['xelp_forcewake_irq','boot_arguments','tgl_display_observers','tgl_aux_live','tgl_bootstrap_host','tgl_clock_order','provider_binding','tgl_start_order','tgl_hdmi_wiring','tgl_bootstrap_aux','tgl_copy_routing','tgl_training_leases','tgl_wiring','tgl_boot_readout','tgl_pcode_gate','tgl_port_use','tgl_tc_cold','tgl_fia','tgl_port_map','tgl_probe','dvmt','host_publication']
jobs=[(p.stem,['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(R/'WhateverXe'),str(p),'-o',str(O/p.stem)],[str(O/p.stem)]) for p in sorted((R/'tests').glob('*.cpp')) if not p.stem.endswith('_fixture')]
jobs += [(n,[sys.executable,str(R/'tests'/(n+'.py'))],None) for n in python_tests]
if a.donor:jobs += [(n,[sys.executable,str(R/'tests'/(n+'.py')),str(a.donor.resolve())],None) for n in ['tgl_tc_native','tgl_tc_mode','tgl_mode_native']]
if a.accelerator:jobs += [('xelp_forcewake_irq_native',[sys.executable,str(R/'tests/xelp_forcewake_irq.py'),'--donor',str(a.accelerator.resolve())],None)]
def check(job):
 n,compile,execute=job
 with (O/(n+'.log')).open('w') as log:
  rc=subprocess.run(compile,stdout=log,stderr=log).returncode
  if rc==0 and execute:rc=subprocess.run(execute,stdout=log,stderr=log).returncode
 return n,rc
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:results=dict(pool.map(check,jobs))
(O/'results.json').write_text(json.dumps(results,indent=2)+'\n')
failed={n:rc for n,rc in results.items() if rc};print(json.dumps({'passed':len(results)-len(failed),'failed':failed}))
sys.exit(bool(failed))
