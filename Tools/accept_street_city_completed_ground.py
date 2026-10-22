"""Adopt a verified terrain delta: all shared seams, new foundations and real entry slabs."""
import bpy,bmesh,sys,json,math
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remesh_gate import verify,sha
from street_city_remaining_contract import IDS,context,OUT
GROUND=OUT/'ground';OLD=OUT.parent/'world-ground-v1';sources=json.loads((GROUND/'chunk-sources.json').read_text('utf8'));old=json.loads((OLD/'ground-acceptance.json').read_text('utf8'));changes={q['id']:q for q in sources['chunks']};checks=[];issues=[];edges={};models=[]
def check(ok,name,**kw):
 r=dict(name=name,passed=bool(ok),**kw);checks.append(r)
 if not ok:issues.append(r)
for prior in old['chunks']:
 cid=prior['id'];q=changes.get(cid);receipt=verify(Path(q['source']).parent/'remesh') if q else verify(Path(prior['ground']['remesh_report']).parent,prior['ground']['sha256']);bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=receipt['path']);bpy.context.view_layer.update();base=next(o for o in bpy.context.scene.objects if o.type=='MESH' and o.get('ground_layer')=='base');points=[base.matrix_world@v.co for v in base.data.vertices];ox,oy,_=prior['world_origin_m'];seams={}
 for v in points:
  if v.z<-29:continue
  if any(abs(v[k]-e)<.0005 for k in [0,1] for e in [0,275]):seams.setdefault((round(v.x+ox,4),round(v.y+oy,4)),[]).append(v.z)
 edges[cid]={k:sum(v)/len(v) for k,v in seams.items()}
 if not q:continue
 bm=bmesh.new();bm.from_mesh(base.data);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-5);bad=sum(not e.is_manifold for e in bm.edges);bm.free();check(bad==0,cid+' closed terrain solid')
 check(sha(q['source'])==q['source_sha256'],cid+' exact graded source');dg=bpy.context.evaluated_depsgraph_get()
 for portal in sources['portals']:
  if not any(portal['building'].startswith(b+'-') for b in IDS):continue
  p=Vector(portal['door_world_m']);out=Vector([*portal['outward'],0]);t=Vector((-out.y,out.x,0));p+=out
  if not (ox<=p.x<ox+275 and oy<=p.y<oy+275):continue
  for lane in [-.8,0,.8]:
   pt=p+t*lane-Vector((ox,oy,0))+Vector((0,0,.4));hit,loc,normal,face,object_,matrix=bpy.context.scene.ray_cast(dg,pt,Vector((0,0,-1)),distance=.8);asphalt=bool(hit and object_.get('ground_layer')=='routes' and abs(loc.z-p.z-.025)<.003);check(hit and (abs(loc.z-p.z)<.003 or asphalt),portal['building']+' true entrance datum '+str(lane),height_m=loc.z if hit else None,designed_asphalt_threshold_m=.025 if asphalt else 0)
 for bid in IDS:
  c=context(bid)['building'];x,y,z=c['world_position_m']
  if ox<=x<ox+275 and oy<=y<oy+275:
   hit,loc,*_=bpy.context.scene.ray_cast(dg,Vector((x-ox,y-oy,z+.1)),Vector((0,0,-1)),distance=2);check(hit and loc.z<=z-.29,bid+' terrain below physical floor slab',height_m=loc.z if hit else None,floor_datum_m=z)
 models.append(dict(id=cid,kind='ground',source=receipt['path'],source_sha256=receipt['sha256'],folder='/Game/UniversityCity/Ground',position_cm=[oy*100,ox*100,0],yaw_deg=90,collision='BlockAll',remesh=receipt))
for a in old['chunks']:
 for b in old['chunks']:
  if a['id']>=b['id']:continue
  ax,ay,_=a['world_origin_m'];bx,by,_=b['world_origin_m']
  if abs(ax-bx)+abs(ay-by)!=275:continue
  shared=set(edges[a['id']])&set(edges[b['id']]);check(len(shared)==111,a['id']+'/'+b['id']+' full seam samples')
  for k in shared:check(abs(edges[a['id']][k]-edges[b['id']][k])<=.002,a['id']+'/'+b['id']+str(k)+' <=2mm seam')
result=dict(status='failed' if issues else 'passed',checks=len(checks),issues=issues,results=checks,overrides=models,scope='Five terrain chunks updated for the eight new foundations and entry slabs; other eleven ground chunks and all existing water remain unchanged',generator_sha256=sha(Path(__file__).parent/'build_street_city_completed_ground.py'))
(GROUND/'acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8');print(result['status'],len(checks),'checks',len(issues),'issues',flush=True);assert not issues,issues[:12]
