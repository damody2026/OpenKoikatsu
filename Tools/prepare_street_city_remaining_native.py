"""Prepare new static mesh assets in Entry without loading or changing the live city map."""
import unreal,sys,json,traceback,os
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remaining_contract import context,IDS,OUT
from street_city_remesh_gate import sha
import street_city_unreal_import as importer
group=os.environ.get('STREET_CITY_LANGUAGE_GROUP','remaining-all');selected=IDS if group=='remaining-all' else [group.removeprefix('remaining-')]
target=OUT/('native-prepared-'+group+'.json');rows=[]
try:
 for bid in selected:
  c=context(bid);folder=c['out'];acc=json.loads((folder/'acceptance.json').read_text('utf8'));assert acc['status']=='passed';assert sha(folder/f'{bid}-complete-2k.glb')==acc['sha256'];x,y,z=c['building']['world_position_m']
  for f in c['building']['floors']:
   no=f['floor'];label=f'City_{bid}_F{no:02d}';source=folder/f'{bid}-F{no:02d}-complete-2k.glb';dest=f'/Game/UniversityCity/CityBuildings/{bid}/F{no:02d}'
   mesh=importer.import_mesh(source,dest,label);materials=importer.ensure_mesh_materials(mesh);assert all(materials)
   rows.append(dict(id=label,kind='city_building',building=bid,floor=no,source=str(source),source_sha256=sha(source),asset=mesh.get_path_name(),folder=dest,position_cm=[y*100,x*100,z*100],yaw_deg=90,collision='BlockAll'))
   target.write_text(json.dumps(dict(status='in_progress',rows=rows),indent=2),'utf8');unreal.log('CITY NEW MODEL '+label);mesh=None;materials=None;importer.CANONICAL_MATERIALS.clear();importer.MATERIAL_BINDINGS.clear();unreal.SystemLibrary.collect_garbage()
  source=folder/f'{bid}-water-2k.glb'
  if source.exists() and sum(json.loads((folder/'assembly-report.json').read_text('utf8')).get('water_triangles',{}).values())>0:
   label=f'City_{bid}_Water';dest=f'/Game/UniversityCity/CityBuildings/{bid}/Water';mesh=importer.import_mesh(source,dest,label);importer.ensure_mesh_materials(mesh)
   rows.append(dict(id=label,kind='city_water',building=bid,source=str(source),source_sha256=sha(source),asset=mesh.get_path_name(),folder=dest,position_cm=[y*100,x*100,z*100],yaw_deg=90,collision='NoCollision'))
 target.write_text(json.dumps(dict(status='passed',rows=rows),indent=2),'utf8')
except Exception:
 (OUT/('native-prepared-'+group+'-error.txt')).write_text(traceback.format_exc(),'utf8');raise
finally:unreal.SystemLibrary.quit_editor()
