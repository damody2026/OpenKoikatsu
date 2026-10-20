"""Sequential bounded editor processes; no concurrent city-map writers."""
import argparse,json,subprocess,sys,shutil,time
from street_city_localization import ROOT,OUT,sha,language_group_signature

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--force',action='store_true');args=parser.parse_args()
 force=args.force;final=OUT/'native-bindings.json';map_path=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'
 if final.exists():
  prior=json.loads(final.read_text('utf8'))
  if prior.get('status')=='passed_reopened_native_materials_and_runtime_parameter':
   current=prior.get('native_map_sha256')==sha(map_path) and prior.get('models_manifest_sha256')==sha(OUT/'models.json') and prior.get('assets_manifest_sha256')==sha(OUT/'assets.json') and prior.get('auditor_sha256')==sha(ROOT/'Tools/audit_street_city_localization_unreal.py')
   if current and not force:print('NATIVE LANGUAGE BINDINGS CURRENT');return
   # A later model import can reset actor overrides even when PNGs are unchanged.
   force=True
 groups=['campus-all','mall','shops-all'];rows=[]
 for group in groups:
  report=OUT/('unreal-'+group+'.json')
  if report.exists() and not force:
   receipt=json.loads(report.read_text('utf8'))
   if receipt.get('status')=='passed' and receipt.get('inputs_signature')==language_group_signature(group) and receipt.get('installer_sha256')==sha(ROOT/'Tools/install_street_city_localization_unreal.py'):rows.append(receipt);continue
  started=time.time();subprocess.run([str(ROOT/'Tools/lua/lua.exe'),str(ROOT/'Tools/run_street_city_localization_unreal.lua'),'--script','install_street_city_localization_unreal.py','--group',group],cwd=ROOT,check=True)
  assert report.exists() and report.stat().st_mtime>=started,group+' no fresh receipt';receipt=json.loads(report.read_text('utf8'));assert receipt['status']=='passed' and receipt['inputs_signature']==language_group_signature(group);rows.append(receipt)
  (OUT/'native-progress.json').write_text(json.dumps(dict(status='in_progress',completed=len(rows),total=len(groups),groups=[r['group'] for r in rows]),indent=2),'utf8');print(group,'PASSED',len(rows),'/',len(groups),flush=True)
 actors=[r for group in rows for r in group['actors']];assert len(actors)==387 and len({r['actor'] for r in actors})==387,len(actors)
 (OUT/'native-bindings.json').write_text(json.dumps(dict(status='passed_installation_requires_reopen_audit',locales=['zh-Hant','zh-Hans','en'],collection=rows[0]['collection'],actors=actors,groups=[r['group'] for r in rows]),ensure_ascii=False,indent=2),'utf8')
 print('NATIVE BOUND',len(actors),sum(len(r['slots']) for r in actors),flush=True)
 started=time.time();subprocess.run([str(ROOT/'Tools/lua/lua.exe'),str(ROOT/'Tools/run_street_city_localization_unreal.lua'),'--script','audit_street_city_localization_unreal.py','--group','reopen-audit'],cwd=ROOT,check=True)
 task=OUT/'native-reopen-task.json';assert task.exists() and task.stat().st_mtime>=started
 result=json.loads(task.read_text('utf8'));assert result['status']=='passed',result
 print('NATIVE REOPEN AUDIT PASSED',result['actors'],flush=True)
if __name__=='__main__':main()
