"""Rebuild, install and independently verify every campus entrance sign."""
import json,subprocess,sys,time
from street_city_localization import ROOT,OUT

def run(*args):subprocess.run(args,cwd=ROOT,check=True)

def main():
 run(sys.executable,'Tools/build_street_city_campus_signs.py')
 run(sys.executable,'Tools/audit_street_city_campus_signs.py')
 for script,receipt in [('install_street_city_campus_signs_unreal.py','unreal.json'),('audit_street_city_campus_signs_unreal.py','acceptance.json')]:
  start=time.time()
  run(str(ROOT/'Tools/lua/lua.exe'),'Tools/run_street_city_localization_unreal.lua','--script',script,'--group','campus-signs')
  path=OUT/'campus-signs'/receipt
  assert path.stat().st_mtime>=start,('No fresh native receipt',receipt)
  assert json.loads(path.read_text('utf8'))['status'] in ('passed_saved','passed'),receipt
 run(sys.executable,'Tools/publish_street_city_campus_sign_link.py')
 print('CAMPUS SIGN LOCALIZATION VERIFIED')

if __name__=='__main__':main()
