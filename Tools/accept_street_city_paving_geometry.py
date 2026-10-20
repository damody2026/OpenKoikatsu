"""Test the actual certified terrain at observed road-overlap locations."""
import bpy,sys,json,hashlib
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path('C:/portable/OpenKoikatsu');sys.path.insert(0,str(ROOT/'Tools'))
from street_city_remesh_gate import verify
OUT=ROOT/'output/street-social-city-production/campus-v1';GROUND=OUT.parent/'world-ground-v1';g=json.loads((GROUND/'ground-acceptance.json').read_text('utf8'));assert g['status']=='passed'
bpy.ops.wm.read_factory_settings(use_empty=True);sources=[];trees=[]
for row in g['chunks']:
 if row['id'] not in ('G10','G11'):continue
 receipt=verify(Path(row['ground']['remesh_report']).parent,row['ground']['sha256']);sources.append(dict(id=row['id'],**receipt));before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=receipt['path']);new=set(bpy.data.objects)-before
 for o in new:
  if not o.parent:o.location+=Vector(row['world_origin_m'])
 bpy.context.view_layer.update()
 for o in new:
  if o.type=='MESH' and o.get('ground_layer') in ('routes','entry'):trees.append((o,BVHTree.FromPolygons([o.matrix_world@v.co for v in o.data.vertices],[tuple(p.vertices) for p in o.data.polygons])))
results=[]
for x,y in [(537.8743,734.5587),(561.8179,732.5387),(514.879,734.4761),(560.9053,735.2065)]:
 hits=[]
 for o,tree in trees:
  hit=tree.ray_cast(Vector((x,y,100)),Vector((0,0,-1)),200)
  if hit[0] is not None:hits.append(dict(object=o.name,z_m=hit[0].z,normal_z=hit[1].z))
 hits.sort(key=lambda q:-q['z_m']);ok=bool(hits) and hits[0]['normal_z']>.99 and (len(hits)==1 or abs(hits[0]['z_m']-hits[1]['z_m'])>.002);results.append(dict(position_m=[x,y],surfaces=hits,passed=ok))
r=dict(status='passed' if all(q['passed'] for q in results) else 'failed',checks=len(results),sources=sources,results=results,source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest());(OUT/'terrain-paving-acceptance.json').write_text(json.dumps(r,indent=2),'utf8');print(r['status'],results);assert r['status']=='passed'
