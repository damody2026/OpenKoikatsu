"""Actual remeshed GLB verification, including all shared boundaries and portals."""
import bpy,bmesh,sys,json,math
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remesh_gate import verify,sha
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production/world-ground-v1';plan=json.loads((OUT/'ground-plan.json').read_text('utf-8'));sources=json.loads((OUT/'chunk-sources.json').read_text('utf-8'));ledger=json.loads((OUT/'chunk-remesh.json').read_text('utf-8'));assert ledger['status']=='passed' and len(sources['chunks'])==16;checks=[];errors=[];edges={};rows=[]
def check(ok,name,**kw):
 checks.append(dict(name=name,passed=bool(ok),**kw))
 if not ok:errors.append(name)
check(sha(OUT/'campus-foundations.json')==sources['campus_foundations_sha256'],'current dimensioned campus foundations')
for kind in plan['ground_kinds']:
 folder=OUT/'assets'/kind;s=json.loads((folder/'surface.json').read_text('utf-8'));check(s['source']['sha256']==verify(folder/'remesh')['sha256'],kind+' current Tripo remesh version')
 for name,m in s['maps'].items():check(sha(m['path'])==m['sha256'],kind+'/'+name+' current baked map')
for chunk in sources['chunks']:
 cid=chunk['id'];folder=Path(chunk['source']).parent;receipt=verify(folder/'remesh');check(sha(chunk['source'])==chunk['source_sha256'],cid+' exact source');check(sha(OUT/'world-field.npz')==chunk['world_field_sha256'],cid+' current geographic field');bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=receipt['path']);bpy.context.view_layer.update();sc=bpy.context.scene;meshes=[o for o in sc.objects if o.type=='MESH'];base=next(o for o in meshes if o.get('ground_layer')=='base');points=[base.matrix_world@v.co for v in base.data.vertices];ox,oy,_=chunk['world_origin_m'];seams={}
 for axis in (0,1):check(abs(min(v[axis] for v in points))<.002 and abs(max(v[axis] for v in points)-275)<.002,cid+' exact 275m bounds axis'+str(axis))
 for v in points:
  if v.z<-29:continue
  if any(abs(v[axis]-edge)<.0005 for axis in (0,1) for edge in (0,275)):
   key=(round(v.x+ox,4),round(v.y+oy,4));seams.setdefault(key,[]).append(v.z)
 edges[cid]={key:sum(values)/len(values) for key,values in seams.items()};bm=bmesh.new();bm.from_mesh(base.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-5);bad=sum(not e.is_manifold for e in bm.edges);bm.free();check(bad==0,cid+' closed terrain solid',nonmanifold_edges=bad)
 triangles=0
 for o in meshes:o.data.calc_loop_triangles();triangles+=len(o.data.loop_triangles)
 report=json.loads((folder/'remesh/remesh-report.json').read_text('utf-8'));check(triangles==report['triangles'] and triangles<=100000,cid+' actual triangle cap',triangles=triangles)
 for im in bpy.data.images:
  if im.size[0]:check(max(im.size)<=2048,cid+' 2K '+im.name)
 dg=bpy.context.evaluated_depsgraph_get();center=Vector((137.5,137.5,200));hit,loc,*_=sc.ray_cast(dg,center,Vector((0,0,-1)),distance=300);check(hit,cid+' ground collision sample exists');center_height=loc.z if hit else None
 for portal in sources['portals']:
  point=Vector(portal['door_world_m']);out=Vector([*portal['outward'],0]);tangent=Vector((-out.y,out.x,0));point+=out
  if not(ox<=point.x<ox+275 and oy<=point.y<oy+275):continue
  for lane in (-.8,0,.8):
   q=point+tangent*lane-Vector((ox,oy,0))+Vector((0,0,1));hit,loc,*_=sc.ray_cast(dg,q,Vector((0,0,-1)),distance=2);check(hit and abs(loc.z-point.z)<=.002,portal['building']+f' entry support lane{lane}',actual_height=loc.z if hit else None)
 water=None
 if chunk.get('water_source'):
  water=verify(folder/'water-remesh');wr=json.loads((folder/'water-remesh/remesh-report.json').read_text('utf-8'));check(triangles+wr['triangles']<=100000,cid+' combined terrain/water cap');triangles+=wr['triangles']
 rows.append(dict(id=cid,world_origin_m=chunk['world_origin_m'],size_m=[275,275],ground=receipt,water=water,triangles=triangles,center_height_m=center_height));print(cid,'verified',triangles,flush=True)
for i,a in enumerate(plan['chunks']):
 for b in plan['chunks'][i+1:]:
  ax,ay,_=a['world_origin_m'];bx,by,_=b['world_origin_m']
  if abs(ax-bx)+abs(ay-by)!=275:continue
  shared=set(edges[a['id']])&set(edges[b['id']]);check(len(shared)==111,a['id']+'/'+b['id']+' complete shared edge samples',samples=len(shared))
  for key in shared:check(abs(edges[a['id']][key]-edges[b['id']][key])<=.002,a['id']+'/'+b['id']+str(key)+' seam <=2mm')
result=dict(status='passed' if not errors else 'failed',chunk_count=len(rows),area_m2=1100*1100,checks=len(checks),errors=errors,chunks=rows,results=checks,ground_plan_sha256=sha(OUT/'ground-plan.json'));(OUT/'ground-acceptance.json').write_text(json.dumps(result,indent=2),'utf-8');print(result['status'],len(checks),len(errors))
if errors:raise RuntimeError(str(errors[:12]))
