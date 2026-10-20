"""Measure adopted ground triangles before designing a mall entrance apron."""
import bpy,sys,json
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_mall import OUT,ROOT
bpy.ops.wm.read_factory_settings(use_empty=True);g=json.loads((ROOT/'output/street-social-city-production/world-ground-v1/ground-acceptance.json').read_text('utf8'));rows=[]
for chunk in g['chunks']:
 if chunk['id']!='G07':continue
 before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=chunk['ground']['path']);new=set(bpy.data.objects)-before;x,y,z=chunk['world_origin_m'];vv=[];ff=[]
 for o in new:
  if o.type!='MESH':continue
  start=len(vv);vv.extend(o.matrix_world@v.co+Vector((x,y,z)) for v in o.data.vertices);ff.extend([start+i for i in f.vertices] for f in o.data.polygons)
 tree=BVHTree.FromPolygons(vv,ff)
 for xx,yy in [(655,yy) for yy in [505,509,510,511,512,514,516,520,525,530,540]]+[(579,460),(731,460),(655,409),(731,490)]:
  hit=tree.ray_cast(Vector((xx,yy,100)),Vector((0,0,-1)),200)[0];rows.append(dict(x=xx,y=yy,z=hit.z if hit else None))
(OUT/'ground-probe.json').write_text(json.dumps(rows,indent=2),'utf8');print(rows)
