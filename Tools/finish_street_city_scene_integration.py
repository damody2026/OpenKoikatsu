"""Bind prepared scene updates, restore localization, and independently reopen all evidence."""
import json,subprocess,time,sys
from pathlib import Path
from street_city_scene_inventory import ROOT,OUT,read,sha
BASE=ROOT/'output/street-social-city-production';LANG=BASE/'localization-v1'
def native(script,group,receipt,expected='passed'):
 started=time.time()
 subprocess.run([str(ROOT/'Tools/lua/lua.exe'),str(ROOT/'Tools/run_street_city_localization_unreal.lua'),'--script',script,'--group',group],cwd=ROOT,check=True)
 assert receipt.exists() and receipt.stat().st_mtime>=started,('No fresh receipt',receipt)
 result=read(receipt)
 if expected is not None:assert result['status']==expected,(receipt,result.get('status'))
 else:assert result['status'] in ['passed','needs_sync'],(receipt,result.get('status'))
 return result

def main():
 p=read(OUT/'prepared-updates.json');assert p['status']=='passed' and p['inventory_sha256']==sha(OUT/'inventory.json')
 native('sync_street_city_completed_native.py','scene-sync',OUT/'sync.json')
 changed=set(read(OUT/'sync.json')['changed'])
 kinds={q['kind'] for q in read(OUT/'inventory.json')['rows'] if q['id'] in changed}
 for kind,group in [('shop','shops-all'),('campus','campus-all'),('mall','mall'),('city_building','remaining-all')]:
  if kind in kinds:native('install_street_city_localization_unreal.py',group,LANG/('unreal-'+group+'.json'))
 walls=BASE/'world-wall-grok-v1'
 if (walls/'activation.json').exists():native('install_street_city_world_walls_unreal.py','world-walls',walls/'native-install.json')
 native('audit_street_city_scene_unreal.py','scene-inventory',OUT/'native-inventory.json')
 native('audit_street_city_campus_signs_unreal.py','campus-signs',LANG/'campus-signs/acceptance.json')
 if (walls/'activation.json').exists():
  native('audit_street_city_world_walls_unreal.py','walls-audit',walls/'native-acceptance.json')
  subprocess.run([sys.executable,str(ROOT/'Tools/run_street_city_world_wall_native_render.py')],cwd=ROOT,check=True)
 print('CURRENT SCENE AND ALL LOCALIZED MATERIALS REOPENED',flush=True)
if __name__=='__main__':main()
