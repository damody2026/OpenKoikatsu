"""Prepare versioned native ground replacements; historical ground sources stay immutable."""
import unreal,sys,json,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remesh_gate import sha,verify
import street_city_unreal_import as importer
OUT=Path(__file__).resolve().parents[1]/'output/street-social-city-production/world-completion-v1/ground'
try:
 acc=json.loads((OUT/'acceptance.json').read_text('utf8'));assert acc['status']=='passed';rows=[]
 for q in acc['overrides']:
  verify(Path(q['remesh']['remesh_report']).parent,q['source_sha256']);mesh=importer.import_mesh(q['source'],q['folder'],q['id']);materials=importer.ensure_mesh_materials(mesh);assert all(materials);rows.append(dict(q,asset=mesh.get_path_name()));mesh=None;materials=None;importer.CANONICAL_MATERIALS.clear();importer.MATERIAL_BINDINGS.clear();unreal.SystemLibrary.collect_garbage()
 (OUT/'native-prepared.json').write_text(json.dumps(dict(status='passed',rows=rows),indent=2),'utf8')
except Exception:(OUT/'native-prepare-error.txt').write_text(traceback.format_exc(),'utf8');raise
finally:unreal.SystemLibrary.quit_editor()
