"""Identify actual coplanar paving at an observed rendered pixel."""
import bpy,ast,sys,json
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from bpy_extras.object_utils import world_to_camera_view
ROOT=Path('C:/portable/OpenKoikatsu');OUT=ROOT/'output/street-social-city-production/campus-v1';bpy.ops.wm.open_mainfile(filepath=str(OUT/'campus-complete-2k.blend'))
# Keep regression camera anchored to the original school buildings.
for ob in bpy.context.scene.objects:
 if ob.get('outdoor_id'):ob.hide_render=True
tree=ast.parse((ROOT/'Tools/render_street_city_campus.py').read_text('utf8'));exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('fit','studio')],type_ignores=[]),'camera-functions','exec'));rows=json.loads((OUT/'world-assembly.json').read_text('utf8'))['buildings'];p=[r['position_m'] for r in rows];target=Vector(tuple(sum(x[k] for x in p)/len(p) for k in range(3)));sc=studio(target,650,(.8,-1.25,1.6));sc.render.resolution_x=2000;sc.render.resolution_y=1600;fit(sc,True);bpy.context.view_layer.update();cam=sc.camera;frame=cam.data.view_frame(scene=sc);direction=cam.matrix_world.to_quaternion()@Vector((0,0,-1));objects=[o for o in sc.objects if o.type=='MESH' and o.get('ground_layer')];trees=[(o,BVHTree.FromPolygons([o.matrix_world@v.co for v in o.data.vertices],[tuple(p.vertices) for p in o.data.polygons])) for o in objects]
results=[]
for px,py in [(1133,939),(1190,971),(1010,860),(1075,912),(1192,965)]:
 u,v=px/2000,1-py/1600;xx=min(p.x for p in frame)+u*(max(p.x for p in frame)-min(p.x for p in frame));yy=min(p.y for p in frame)+v*(max(p.y for p in frame)-min(p.y for p in frame));origin=cam.matrix_world@Vector((xx,yy,0));hits=[]
 for o,bt in trees:
  hit=bt.ray_cast(origin,direction,10000)
  if hit[0] is not None:hits.append((hit[3],o.name,o.get('ground_layer'),list(hit[0]),list(hit[1])))
 hits=sorted(hits);duplicate=bool(len(hits)>1 and abs(hits[0][3][2]-hits[1][3][2])<.002)
 results.append(dict(pixel=[px,py],nearest_surfaces=hits[:8],passed=bool(hits) and not duplicate))
 print('SURFACE_HITS',px,py,hits[:8])
import hashlib
result=dict(status='passed' if all(r['passed'] for r in results) else 'failed',model_sha256=hashlib.sha256((OUT/'campus-complete-2k.blend').read_bytes()).hexdigest(),source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),checks=len(results),results=results)
(OUT/'ground-overlap-acceptance.json').write_text(json.dumps(result,indent=2),'utf8');assert result['status']=='passed',results
