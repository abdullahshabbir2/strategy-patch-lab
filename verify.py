"""Build the owned binary; analyze, patch, execute and roll it back; retain evidence."""
import csv
import io
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
from patchlab.patch import apply,inspect,make_manifest,rollback

ROOT=Path(__file__).parent.resolve()
os.chdir(ROOT)
if os.name!='nt': raise SystemExit('This executable-patching profile requires Windows x86-64 and MinGW g++.')
for folder in ('build','evidence','scenarios'): Path(folder).mkdir(exist_ok=True)
flags=['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-pedantic','-fno-lto','-static','-Wl,--no-insert-timestamp','-Ifirmware']
core=['firmware/strategy.cpp','firmware/calibration.cpp']
subprocess.run(flags+core+['firmware/feature_gate.cpp','firmware/main.cpp','-o','build/strategy_baseline.exe'],check=True)
subprocess.run(flags+core+['tests/test_strategy.cpp','-o','build/strategy_tests.exe'],check=True)
test=subprocess.run(['build/strategy_tests.exe'],capture_output=True,text=True)
Path('evidence/strategy_tests.txt').write_text(test.stdout+test.stderr)
if test.returncode: raise SystemExit(test.stdout+test.stderr)
baseline=Path('build/strategy_baseline.exe').read_bytes()
manifest=make_manifest(baseline)
candidate,receipt=apply(baseline,manifest)
Path('build/strategy_patched.exe').write_bytes(candidate)
restored=rollback(candidate,receipt)
assert restored==baseline
Path('build/strategy_restored.exe').write_bytes(restored)
for filename,data in [('baseline_analysis.json',inspect(baseline)),('patch_manifest.json',manifest),('patch_receipt.json',receipt)]:
    Path('evidence',filename).write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
for name in ('baseline','patched'):
    disassembly=subprocess.run(['objdump','-d','-j','.patch',f'build/strategy_{name}.exe'],capture_output=True,text=True,check=True)
    Path(f'evidence/{name}_disassembly.txt').write_text(disassembly.stdout,encoding='utf-8')
symbols=subprocess.run(['nm','-C','build/strategy_baseline.exe'],capture_output=True,text=True,check=True)
Path('evidence/relevant_symbols.txt').write_text('\n'.join(line for line in symbols.stdout.splitlines() if 'lab::' in line or 'strategy_features' in line)+'\n')
calibration_dump=subprocess.run(['objdump','-s','-j','.cal','build/strategy_baseline.exe'],capture_output=True,text=True,check=True)
Path('evidence/calibration_hex.txt').write_text(calibration_dump.stdout)
with Path('scenarios/integration.csv').open('w',newline='') as f:
    writer=csv.writer(f)
    writer.writerow(['ms','rpm','pedal','speed','coolant','ethanol','ethanol_age','measured_torque','map','brake','clutch','launch'])
    for ms in range(0,8401,20):
        rpm,pedal,speed,brake= (1000,0,0,1) if ms<=600 else (3000,70,40,0)
        clutch,launch,coolant,age,measured=0,0,90,0,0
        if 3000<=ms<3300: pedal,clutch=100,1
        if 4000<=ms<6500: rpm,pedal,speed,brake,launch=3500,100,0,1,1
        if 6600<=ms<7000: coolant=135
        if 7200<=ms<7600: age=500
        if ms>=7800: measured=300
        writer.writerow([ms,rpm,pedal,speed,coolant,85 if ms>=1800 else 10,age,measured,2,brake,clutch,launch])
outputs={}
for name in ('baseline','patched','restored'):
    run=subprocess.run([f'build/strategy_{name}.exe','scenarios/integration.csv'],capture_output=True,text=True,check=True)
    Path(f'evidence/{name}_trace.csv').write_text(run.stdout,encoding='utf-8')
    outputs[name]=list(csv.DictReader(io.StringIO(run.stdout)))
assert outputs['baseline']==outputs['restored']
assert all(row['features']=='0' and row['map']=='0' and float(row['fuel_multiplier'])==1 for row in outputs['baseline'])
patched=outputs['patched']
assert any(row['map']=='2' for row in patched)
assert max(float(row['fuel_multiplier']) for row in patched)>1.4
assert any(row['launch']=='1' for row in patched) and any(row['shift']=='1' for row in patched)
assert all(float(row['commanded_nm'])<=float(row['limit_nm']) for row in patched)
assert all(float(row['commanded_nm'])==0 for row in patched if 6600<=int(row['ms'])<7000)
assert all(row['limp']=='1' for row in patched if 7200<=int(row['ms'])<7600)
assert all(int(row['dtcs'])&2 and row['limp']=='1' for row in patched if int(row['ms'])>=8000)
assert all(float(row['commanded_nm'])<=60 for row in patched if row['shift']=='1')
assert all(float(row['commanded_nm'])<=80 for row in patched if row['launch']=='1')
tests=subprocess.run([sys.executable,'-m','unittest','discover','-s','tests','-p','test_*.py','-v'],capture_output=True,text=True)
Path('evidence/patch_tests.txt').write_text(tests.stdout+tests.stderr,encoding='utf-8')
if tests.returncode: raise SystemExit(tests.stdout+tests.stderr)
summary={'environment':platform.platform(),'compiler':subprocess.run(['g++','--version'],capture_output=True,text=True).stdout.splitlines()[0],
         'strategy_test_result':test.stdout.strip(),'patch_tests_passed':True,
         'replay_rows_per_binary':len(patched),'exact_binary_rollback':True,'baseline_restored_behavior_identical':True,
         'protected_bytes_identical':True,'features_exercised':['map selection','ethanol fuel compensation','launch torque limit','flat-shift torque limit'],
         'protections_exercised':['thermal override','stale fuel sensor','torque tracking latch'],
         'validation_scope':'Owned x86-64 host model; no ECU hardware or OEM strategy validation'}
Path('evidence/validation_summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
