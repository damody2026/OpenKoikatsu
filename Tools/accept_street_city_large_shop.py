"""Independent exported GLB checks for large shop layouts and variable storey heights."""
import bpy,json,sys,math,hashlib,argparse
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_large_shop_contract import context
from street_city_remesh_gate import verify
p=argparse.ArgumentParser();p.add_argument('--building',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);C=context(a.building);b=C['building'];OUT=C['out'];BID=C['id'];path=OUT/f'{BID}-complete-2k.glb';r=json.loads((OUT/'assembly-report.json').read_text('utf-8'));checks=[];errors=[]
def check(ok,name,**extra):
 checks.append(dict(name=name,passed=bool(ok),**extra))
 if not ok:errors.append(name)
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(path));sc=bpy.context.scene;sc.frame_set(1);bpy.context.view_layer.update();meshes=[o for o in sc.objects if o.type=='MESH']
def points(os):return [o.matrix_world@v.co for o in os for v in o.data.vertices]
pts=points(meshes)
for k,expected in enumerate([*b['footprint_m'],b['floors'][-1]['z_m']+b['floors'][-1]['height_m']+.3]):
 value=max(q[k] for q in pts)-min(q[k] for q in pts);check(abs(value-expected)<.002,f'actual full envelope axis{k}',actual=value,expected=expected)
for aid,source in r['sources'].items():
 try:verify(Path(source['remesh_report']).parent,source['sha256']);check(True,aid+' mandatory remesh version')
 except Exception as error:check(False,aid+' mandatory remesh version',error=str(error))
counts={}
for f in b['floors']:
 no=f['floor'];objects=[o for o in meshes if o.get('floor')==no];total=0
 for o in objects:o.data.calc_loop_triangles();total+=len(o.data.loop_triangles)
 counts[no]=total;check(total<=100000,f'F{no} triangle cap',triangles=total)
for image in bpy.data.images:
 if image.size[0]:check(max(image.size)<=2048,'2K '+image.name,size=list(image.size))
for role in ['floor','wet-wall','paint']:
 materials=[m for m in bpy.data.materials if m.get('finish_role')==role];check(bool(materials),'actual exported PBR finish '+role)
 for mat in materials:
  bs=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
  for socket in ['Base Color','Roughness','Normal']:check(bool(bs.inputs[socket].links),mat.name+' exported '+socket)
expected=[q['id'] for q in C['instances'] if C['assets'][q['asset_id']]['role'] in ('annex_furniture','sanitary_fixture','service_fixture','door')]
actual=[o.get('source_instance_id') for o in sc.objects if o.get('source_instance_id')]
check(sorted(expected)==sorted(actual),'every planned fixture and door has actual geometry',missing=sorted(set(expected)-set(actual)))
for root in [o for o in sc.objects if o.type=='EMPTY' and o.get('source_instance_id')]:
 pp=points([o for o in root.children_recursive if o.type=='MESH']);lo=[min(q[k] for q in pp) for k in range(3)];hi=[max(q[k] for q in pp) for k in range(3)]
 check(lo[0]>=-b['footprint_m'][0]/2-.002 and hi[0]<=b['footprint_m'][0]/2+.002 and lo[1]>=-b['footprint_m'][1]/2-.002 and hi[1]<=b['footprint_m'][1]/2+.002,root.name+' physical footprint',bounds=[lo,hi])
def refresh():bpy.context.view_layer.update();return bpy.context.evaluated_depsgraph_get()
dg=refresh()
def ray(origin,direction,distance):
 hit,point,normal,index,obj,matrix=sc.ray_cast(dg,Vector(origin),Vector(direction),distance=distance);return (point,obj) if hit else (None,None)
routes=[]
for f in b['floors']:
 no=f['floor'];z=f['z_m'];W,D=b['footprint_m'];rows=[dict(id='main',center_m=[0,0,z],size_m=[W-.4,2,f['height_m']])]+f.get('branch_corridors',[])+[dict(id=q['id'],center_m=q['position_m'],size_m=q['size_m']) for q in f.get('service_access_corridors',[])]
 for route in rows:
  x,y,_=route['center_m'];w,d=route['size_m'][:2];w=max(2,w-.4) if w<d else w;d=max(2,d-.4) if d<w else d
  routes.append(dict(floor=no,id=route['id'],bounds_m=[x-w/2,y-d/2,x+w/2,y+d/2]))
  along=0 if w>d else 1;length=w if along==0 else d;width=d if along==0 else w;steps=max(2,math.ceil(length/.5))
  for j in range(1,steps):
   at=-length/2+length*j/steps
   for offset in [-.9,0,.9]:
    xx,yy=(x+at,y+offset) if along==0 else (x+offset,y+at)
    pt,obj=ray((xx,yy,z+.02),(0,0,-1),.08);check(pt is not None and abs(pt.z-z)<.002,f'F{no} {route["id"]} floor{j}/{offset}',hit=obj.name if obj else None)
    pt,obj=ray((xx,yy,z+.025),(0,0,1),2.175);check(pt is None,f'F{no} {route["id"]} headroom{j}/{offset}',hit=obj.name if obj else None)
   for height in [.15,1,2.19]:
    origin=(x+at,y-width/2+.001,z+height) if along==0 else (x-width/2+.001,y+at,z+height);direction=(0,1,0) if along==0 else (1,0,0)
    pt,obj=ray(origin,direction,width-.002);check(pt is None,f'F{no} {route["id"]} clear width{j}@{height}',hit=obj.name if obj else None)
 for core in [c for c in f['vertical_circulation'] if c['kind']=='stairs' and no<len(b['floors'])]:
  cx,cy,_=core['position_m'];g=core['walkable_geometry'];run=g['flight_run_m'];rise=f['height_m']/g['riser_count'];start=cy-run/2;end=cy+run/2
  per=g['riser_count']//2
  samples=[(cx-.9,start+(j+.5)*g['tread_depth_m'],z+(j+1)*rise) for j in range(per-1)]+[(cx,cy+run/2+.75,z+f['height_m']/2)]+[(cx+.9,end-(j+.5)*g['tread_depth_m'],z+(per+j+1)*rise) for j in range(per-1)]+[(cx+.9,start-.5,z+f['height_m'])]
  for j,(xx,yy,zz) in enumerate(samples):
   for offset in [-.7,0,.7]:
    pt,obj=ray((xx+offset,yy,zz+.02),(0,0,-1),.08);check(pt is not None and abs(pt.z-zz)<.002,f'F{no} stair tread{j}/{offset}',hit=obj.name if obj else None)
    pt,obj=ray((xx+offset,yy,zz+.01),(0,0,1),2.19);check(pt is None,f'F{no} stair headroom{j}/{offset}',hit=obj.name if obj else None)
from street_city_shop_wall_styles import verify_export as verify_wall_export
verify_wall_export(sc,BID,r,check)
for pool in [o for o in sc.objects if o.type=='EMPTY' and o.get('asset_id')=='large-spa-basin-v2']:
 for k in [0,1]:check(bpy.data.objects.get(pool.name+f'-relax-{k}') is not None,pool.name+f' relaxation lounger{k}')
 treads=[(-1.4+j*.2,.15*(j+1)) for j in range(6)]+[(-.05+j*.2,.744-j*.156) for j in range(4)]+[(.85,.12)]
 for j,(yy,zz) in enumerate(treads):
  for offset in [-.35,0,.35]:
   origin=pool.matrix_world@Vector((-.65+offset,yy,zz+.02));pt,obj=ray(origin,(0,0,-1),.08);expected=pool.matrix_world@Vector((-.65+offset,yy,zz));check(pt is not None and (pt-expected).length<.002,pool.name+f' physical pool access step{j}/{offset}',hit=obj.name if obj else None)
 check(all(abs(treads[j][1]-treads[j-1][1])<=.17 for j in range(1,len(treads))),pool.name+' pool access risers <=0.17m')
for door in r['doors']:
 no=door['floor'];sc.frame_set([1,80,160][no-1]);dg=refresh();x,y,z=door['position_m'];yaw=math.radians(door['yaw_deg']);axis=(math.cos(yaw),math.sin(yaw),0);width=door['width_m']
 for height in [.1,1,door['height_m']-.02]:
  origin=(x-axis[0]*(width/2-.001),y-axis[1]*(width/2-.001),z+height);pt,obj=ray(origin,axis,width-.002);check(pt is None,door['id']+f' true clear opening@{height}',hit=obj.name if obj else None)
cab=[o for o in sc.objects if o.type=='EMPTY' and o.get('asset_id')=='large-lift-car'];check(len(cab)==1,'one actual lift cab')
cx,cy=r['lift']['door_center_m']
for no,z in enumerate(r['lift']['stops'],1):
 sc.frame_set([1,80,160][no-1]);dg=refresh()
 for dy in [.1,0,-.2,-.5,-1,-2.2]:
  pt,obj=ray((cx,cy+dy,z+.02),(0,0,-1),.08);check(pt is not None and abs(pt.z-z)<.002,f'lift stop{no} boarding floor{dy}',hit=obj.name if obj else None)
 pt,obj=ray((cx,cy-1,z+.02),(0,0,1),3);check(pt is not None and pt.z-z>=2.39,f'lift stop{no} headroom',actual=pt.z-z if pt else None)
for frame in [40,120]:
 sc.frame_set(frame);dg=refresh();check(r['lift']['stops'][0]<cab[0].matrix_world.translation.z<r['lift']['stops'][-1],f'lift interpolated movement{frame}')
 for no,z in enumerate(r['lift']['stops'],1):
  pt,obj=ray((cx-.599,cy,z+1),(1,0,0),1.198);check(pt is not None and 'landing-door-panel' in obj.name,f'lift travel landing door closed F{no}@{frame}',hit=obj.name if obj else None)
result=dict(status='passed' if not errors else 'failed',source=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),checks=len(checks),errors=errors,triangles=sum(counts.values()),floor_triangles=counts,results=checks,scope='Actual exported full GLB reimport; variable storey heights, remesh receipts, corridors, stairs, doors and lift')
(OUT/'acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf-8');(OUT/'routes.json').write_text(json.dumps(routes,indent=2),'utf-8')
print(BID,result['status'],len(errors),result['triangles'])
if errors:raise RuntimeError(f'{len(errors)} actual GLB checks failed')
