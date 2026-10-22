"""Resume exact-source asset preparation without loading the full city into memory."""
import unreal,json,sys,traceback,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_scene_inventory import ROOT,OUT,read,sha
import street_city_unreal_import as importer
inventory=read(OUT/'inventory.json');bad={q['id'] for q in read(OUT/'native-inventory.json')['issues']};rows=[]
try:
 for q in inventory['rows']:
  if q['id'] not in bad:continue
  assert sha(q['source'])==q['source_sha256'],'Source changed during integration: '+q['id']
  mesh=importer.import_mesh(q['source'],q['folder'],q['id']);bound=importer.ensure_mesh_materials(mesh)
  assert mesh and all(v is not None for v in bound)
  rows.append(dict(id=q['id'],asset=mesh.get_path_name(),source=q['source'],source_sha256=q['source_sha256']))
  (OUT/'prepared-updates.json').write_text(json.dumps(dict(status='in_progress',inventory_sha256=sha(OUT/'inventory.json'),completed=len(rows),total=len(bad),rows=rows),indent=2),'utf8')
  unreal.log('CITY PREPARED '+str(len(rows))+'/'+str(len(bad))+' '+q['id'])
  bound=None;mesh=None;importer.CANONICAL_MATERIALS.clear();importer.MATERIAL_BINDINGS.clear();unreal.SystemLibrary.collect_garbage()
 (OUT/'prepared-updates.json').write_text(json.dumps(dict(status='passed',inventory_sha256=sha(OUT/'inventory.json'),completed=len(rows),total=len(bad),rows=rows),indent=2),'utf8')
except Exception:
 (OUT/'prepared-updates-error.txt').write_text(traceback.format_exc(),'utf8');raise
finally:unreal.SystemLibrary.quit_editor()
