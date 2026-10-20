"""Independent GLB reimport acceptance of dimensions, portals and stair routes."""
import argparse,json,sys,math,hashlib
from pathlib import Path
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1];BASE=ROOT/'output/street-social-city-production/S01-modular-components-v1';OUT=BASE/'assembly-f02'
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(a.source.resolve()));sc=bpy.context.scene;bpy.context.view_layer.update()
m=json.loads((ROOT/'Docs/StreetSocialCity/production/assembly-manifest.json').read_text('utf-8'));b=next(x for x in m['layouts'] if x.get('kind')=='building' and x['building']=='S01');f=b['small_shop_design']['floors'][1];z=f['z_m'];g=f['stair_geometry'];checks=[];errors=[]
def check(ok,name,**values):
    checks.append(dict(name=name,passed=bool(ok),**values))
    if not ok:errors.append(name)
def points(objects):return [o.matrix_world@v.co for o in objects for v in o.data.vertices]
meshes=[o for o in sc.objects if o.type=='MESH'];pts=points(meshes)
for k,size in [(0,12),(1,18)]:
    actual=max(q[k] for q in pts)-min(q[k] for q in pts);check(abs(actual-size)<.002,f'actual footprint axis {k}',metres=actual)
triangles=0
for o in meshes:o.data.calc_loop_triangles();triangles+=len(o.data.loop_triangles)
check(triangles<=100000,'actual instanced triangle budget',triangles=triangles)
for aid,count in [('table',4),('chair',16),('bookshelf',6),('book',420),('toilet',1),('sink',1),('lift',1)]:
    roots=[o for o in sc.objects if o.type=='EMPTY' and o.get('asset_id')==aid]
    check(len(roots)==count,aid+' instance count',actual=len(roots),expected=count)
    for root in roots:
        local=[root.matrix_world.inverted()@q for q in points([c for c in root.children_recursive if c.type=='MESH'])]
        if not local:check(False,root.name+' has geometry');continue
        size=[max(q[k] for q in local)-min(q[k] for q in local) for k in range(3)]
        target=next(c['size'] for c in json.loads((BASE/'components.json').read_text('utf-8'))['components'] if c['id']==aid)
        check(max(abs(x-y) for x,y in zip(size,target))<.002,root.name+' actual local dimensions',metres=size)
        bottom=min(q.z for q in points([c for c in root.children_recursive if c.type=='MESH']))
        if aid in ('chair','table','bookshelf','toilet','sink'):check(abs(bottom-z)<.002,root.name+' floor contact',bottom_z=bottom)
for image in bpy.data.images:
    if image.size[0]:check(max(image.size)<=2048,'texture 2K '+image.name,pixels=list(image.size))
for aid,source in json.loads((OUT/'assembly-report.json').read_text('utf-8'))['sources'].items():
    report=json.loads(Path(source['remesh_report']).read_text('utf-8'))
    check(report['status']=='passed' and Path(source['path']).exists(),aid+' mandatory remesh gate')
    check(hashlib.sha256(Path(source['path']).read_bytes()).hexdigest()==source['sha256'],aid+' assembly source hash')
def refresh():
    bpy.context.view_layer.update();return bpy.context.evaluated_depsgraph_get()
dg=refresh()
def ray(origin,direction,distance=20):
    hit,loc,normal,index,obj,matrix=sc.ray_cast(dg,Vector(origin),Vector(direction),distance=distance)
    return (loc,obj) if hit else (None,None)
routes=[('internal',f['internal_route_bounds_m'])]+[(c['id'],c['bounds_m']) for c in f['cells'] if c['kind']=='corridor']
for name,(x0,y0,x1,y1) in routes:
    blockers=[]
    for obj in meshes:
        vv=[obj.matrix_world@v.co for v in obj.data.vertices]
        if max(q.z for q in vv)<=z+.02 or min(q.z for q in vv)>=z+2.2:continue
        if min(max(q.x for q in vv),x1)-max(min(q.x for q in vv),x0)>.001 and min(max(q.y for q in vv),y1)-max(min(q.y for q in vv),y0)>.001:blockers.append(obj.name)
    check(not blockers,name+' protected 2.2m standing corridor volume',blockers=blockers)
    axis=0 if x1-x0<y1-y0 else 1;cross=(x1-x0) if axis==0 else (y1-y0)
    check(cross>=2-1e-6,name+' planned width >=2m',width=cross)
    length=(y1-y0) if axis==0 else (x1-x0);steps=math.ceil(length/.25)
    for i in range(steps+1):
        t=(i+.001)/(steps+.002);x=(x0+x1)/2 if axis==0 else x0+(x1-x0)*t;y=y0+(y1-y0)*t if axis==0 else (y0+y1)/2
        floor,obj=ray((x,y,z+.02),(0,0,-1),.1);check(floor is not None and abs(floor.z-z)<.002,f'{name} floor continuity {i}')
        for height in [.15,1.0,1.8,2.19]:
            start=(x0+.001,y,z+height) if axis==0 else (x,y0+.001,z+height)
            direction=(1,0,0) if axis==0 else (0,1,0)
            hit,obj=ray(start,direction,cross-.002)
            check(hit is None,f'{name} actual full-width clearance {i} at {height}',hit_object=obj.name if obj else None)
for door in f['doors']:
    x,y=door['center_m'];axis=0 if door['outward_side']=='south' else 1
    if axis==0:y+=.1
    else:x-=.1
    for height in [.1,1.0,2.2,2.39]:
        start=(x-.6+.001,y,z+height) if axis==0 else (x,y-.6+.001,z+height)
        direction=(1,0,0) if axis==0 else (0,1,0)
        hit,obj=ray(start,direction,1.198)
        check(hit is None,door['id']+f' actual 1.2m aperture at {height}',hit_object=obj.name if obj else None)
# Inspect lift threshold/cab actual walking surface and 2.4m headroom.
for i,x in enumerate([-.19,-.35,-.5,-.7,-1.0,-1.5,-2.2]):
    floor,obj=ray((x,7.1,z+.02),(0,0,-1),.1);check(floor is not None and abs(floor.z-z)<.002,f'lift approach floor {i}')
hit,obj=ray((-1.575,7.1,z+.01),(0,0,1),3);check(hit is not None and hit.z-z>=2.39,'lift actual standing headroom',height=hit.z-z if hit else None)
# Add a reusable stair-only incoming F01 context; never claim full F01 assembly.
for o in list(meshes):
    if o.get('part_kind') not in ('stair_tread','stair_landing','handrail'):continue
    q=o.copy();q.data=o.data;q.parent=None;q.matrix_world=o.matrix_world.copy();q.location.z-=3.8;q.name='F01-connection-context/'+o.name;sc.collection.objects.link(q);q['connection_context_only']=True
dg=refresh();core=next(c for c in f['cells'] if c['kind']=='stairs')['bounds_m'];cx=(core[0]+core[2])/2;cy=(core[1]+core[3])/2;start=cy-g['flight_run_m']/2;end=cy+g['flight_run_m']/2;r=g['riser_height_m'];half=g['riser_count']//2
for floorno,bottom in [(1,0),(2,3.8)]:
    samples=[(cx-.9,start+(j+.5)*.28,bottom+(j+1)*r) for j in range(half-1)]
    samples.append((cx,(end+core[3]-.2)/2,bottom+1.9))
    samples += [(cx+.9,end-(j+.5)*.28,bottom+(half+j+1)*r) for j in range(half-1)]
    samples.append((cx+.9,start-.5,bottom+3.8))
    for i,(x,y,expected) in enumerate(samples):
        for offset in [-.35,0,.35]:
            loc,obj=ray((x+offset,y,expected+.02),(0,0,-1),.1)
            check(loc is not None and abs(loc.z-expected)<.002,f'F{floorno} stair tread {i} body lane {offset}',actual_z=loc.z if loc else None,expected_z=expected)
            loc,obj=ray((x+offset,y,expected+.01),(0,0,1),2.19)
            check(loc is None,f'F{floorno} stair headroom {i} body lane {offset}',hit_object=obj.name if obj else None)
    loc,obj=ray((cx+.9,start-.5,bottom+3.82),(0,0,-1),.1)
    check(loc is not None and abs(loc.z-(bottom+3.8))<.002,f'F{floorno} arrival matches next floor',actual_z=loc.z if loc else None)
report=dict(status='passed' if not errors else 'failed',scope='exported S01 F02 geometry; F01 stair-only compatibility context',source=str(a.source.resolve()),sha256=hashlib.sha256(a.source.read_bytes()).hexdigest(),checks=len(checks),triangles=triangles,errors=errors,results=checks,limitations=['No game runtime door/lift controller or character navigation test','F01 context reuses compatible remeshed stair geometry; not the whole F01 model'])
(OUT/'acceptance.json').write_text(json.dumps(report,indent=2),'utf-8')
print(json.dumps(dict(status=report['status'],checks=len(checks),errors=errors[:20])))
if errors:raise RuntimeError(f'{len(errors)} exported geometry checks failed')
