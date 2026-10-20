"""Measured merchandising LODs derived from the accepted Tripo meshes.

Only decorative mesh detail is simplified. Raw/hero assets and architectural
5mm gates stay intact. Every resulting LOD must subsequently pass Rust remesh.
"""
import bpy,bmesh,sys,json,math
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_mall_merchandise import OUT,folder_for
from street_city_remesh_gate import verify,sha
TARGETS={'perfume':(150,.008),'lipstick':(130,.004),'skincare':(150,.006),'shirt':(300,.015),'blazer':(350,.015),'sportswear':(350,.015),'jeans':(300,.015),'dress':(350,.015),'lingerie':(350,.015),'books':(220,.012)}
TARGETS['folded-shirt']=(180,.006)
data=json.loads((OUT/'catalogue.json').read_text('utf8'));rows={q['id']:q for q in data['components']};results={};declined={}
for aid,(target,tolerance) in TARGETS.items():
 row=rows[aid];source=verify(folder_for(row)/'remesh');folder=OUT/'lods'/aid;folder.mkdir(parents=True,exist_ok=True);receipt=folder/'source.json'
 if receipt.exists():
  old=json.loads(receipt.read_text('utf8'))
  if old['hero_sha256']==source['sha256'] and old.get('generator_sha256')==sha(__file__) and (folder/'normalized-source.glb').exists():results[aid]=old;continue
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=source['path']);bpy.context.view_layer.update();obs=[o for o in bpy.context.scene.objects if o.type=='MESH'];snapshots=[];allv=[];allf=[]
 for o in obs:
  o.data.transform(o.matrix_world);o.parent=None;o.matrix_world.identity()
  # glTF splits vertices along normal/UV seams. Without metric welding,
  # Collapse decimation drops disconnected triangles instead of simplifying
  # the connected product surface. Loop UV coordinates are preserved.
  bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6);bm.to_mesh(o.data);bm.free();o.data.calc_loop_triangles();mesh=o.data.copy();mesh.calc_loop_triangles();snapshots.append((o,mesh));start=len(allv);allv.extend(v.co.copy() for v in mesh.vertices);allf.extend([i+start for i in t.vertices] for t in mesh.loop_triangles)
 raw_tree=BVHTree.FromPolygons(allv,allf,all_triangles=True);limits=[(min(v[k] for v in allv),max(v[k] for v in allv)) for k in range(3)];original_triangles=len(allf);success=False;print('RAW SELF',aid,max(raw_tree.find_nearest(v)[3] for v in allv),len(allv),len({i for f in allf for i in f}),flush=True)
 for desired in [target,round(target*1.5),round(target*2.2),round(original_triangles*.8)]:
  for o,mesh in snapshots:
   o.data=mesh.copy();o.data.calc_loop_triangles();fraction=len(o.data.loop_triangles)/original_triangles;budget=max(16,round(desired*fraction));bpy.context.view_layer.objects.active=o
   for group in list(o.vertex_groups):o.vertex_groups.remove(group)
   garment=aid in ('shirt','blazer','sportswear','jeans','dress','lingerie')
   if garment:
    group=o.vertex_groups.new(name='Preserve real hanger geometry');group.add(list(range(len(o.data.vertices))),0,'REPLACE');top=[v.index for v in o.data.vertices if v.co.z>limits[2][1]-.06];group.add(top,1,'REPLACE')
   mod=o.modifiers.new('Retail-distance source LOD','DECIMATE');mod.ratio=min(1,budget/len(o.data.loop_triangles));mod.use_collapse_triangulate=True
   if garment:mod.vertex_group=group.name;mod.vertex_group_factor=1000
   bpy.ops.object.modifier_apply(modifier=mod.name)
  pts=[v.co for o in obs for v in o.data.vertices];lo=Vector(tuple(min(v[k] for v in pts) for k in range(3)));hi=Vector(tuple(max(v[k] for v in pts) for k in range(3)))
  for o in obs:
   for v in o.data.vertices:v.co=Vector(tuple(limits[k][0]+(v.co[k]-lo[k])*(limits[k][1]-limits[k][0])/(hi[k]-lo[k]) for k in range(3)))
  verts=[];faces=[]
  for o in obs:
   o.data.calc_loop_triangles();start=len(verts);verts.extend(v.co.copy() for v in o.data.vertices);faces.extend([i+start for i in t.vertices] for t in o.data.loop_triangles)
  tree=BVHTree.FromPolygons(verts,faces,all_triangles=True)
  def samples(vv,ff):
   return vv+[sum((vv[i] for i in f),Vector())/3 for f in ff]+[(vv[f[0]]+vv[f[1]])/2 for f in ff]
  deviation=max(max(tree.find_nearest(v)[3] for v in samples(allv,allf)),max(raw_tree.find_nearest(v)[3] for v in samples(verts,faces)))
  print('LOD attempt',aid,desired,len(faces),deviation,flush=True)
  if deviation<=tolerance:success=True;break
 if not success or len(faces)>=original_triangles*.85:
  declined[aid]=dict(reason='No sufficiently reduced mesh passes the explicit silhouette tolerance; retain the accepted hero instead',best_deviation_m=deviation,tolerance_m=tolerance,hero_triangles=original_triangles,attempt_triangles=len(faces));print('Hero retained',aid,flush=True);continue
 bpy.ops.export_scene.gltf(filepath=str(folder/'normalized-source.glb'),export_format='GLB',export_extras=True)
 record=dict(id=aid,hero_sha256=source['sha256'],generator_sha256=sha(__file__),triangles=len(faces),triangle_cap=len(faces),sampled_surface_deviation_m=deviation,decoration_lod_tolerance_m=tolerance,architecture_unchanged=True,mode='derived LOD of the same product, not an additional product type',normalized_source_sha256=sha(folder/'normalized-source.glb'))
 receipt.write_text(json.dumps(record,indent=2),'utf8');results[aid]=record;print(aid,len(faces),deviation,flush=True)
(OUT/'lods/sources.json').write_text(json.dumps(dict(status='prepared_requires_remesh',products=results,retained_heroes=declined),indent=2),'utf8')
