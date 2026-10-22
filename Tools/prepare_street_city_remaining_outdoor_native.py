"""Prepare certified physical outdoor assets without touching the current map."""
import unreal,sys,json,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
OUT=Path(__file__).resolve().parents[1]/'output/street-social-city-production/world-completion-v1/outdoor'
from street_city_remesh_gate import sha
import street_city_unreal_import as importer
try:
 acc=json.loads((OUT/'acceptance.json').read_text('utf8'));assert acc['status']=='passed' and acc['assembly_sha256']==sha(OUT/'assembly-report.json');programme=json.loads((OUT/'assembly-report.json').read_text('utf8'));rows=[]
 for q in programme['models']:
  assert sha(q['source'])==q['sha256'];label='World_'+q['id'].replace('-','_');folder='/Game/UniversityCity/WorldFacilities/'+q['id'].replace('-','_');mesh=importer.import_mesh(q['source'],folder,label);materials=importer.ensure_mesh_materials(mesh);assert all(materials);x,y,z=q['position_m']
  rows.append(dict(id=label,kind='world_facility',source=q['source'],source_sha256=q['sha256'],asset=mesh.get_path_name(),folder=folder,position_cm=[y*100,x*100,z*100],yaw_deg=90,collision='BlockAll'))
  (OUT/'native-prepared.json').write_text(json.dumps(dict(status='in_progress',rows=rows),indent=2),'utf8');mesh=None;materials=None;importer.CANONICAL_MATERIALS.clear();importer.MATERIAL_BINDINGS.clear();unreal.SystemLibrary.collect_garbage()
 (OUT/'native-prepared.json').write_text(json.dumps(dict(status='passed',rows=rows),indent=2),'utf8')
except Exception:
 (OUT/'native-prepare-error.txt').write_text(traceback.format_exc(),'utf8');raise
finally:unreal.SystemLibrary.quit_editor()
