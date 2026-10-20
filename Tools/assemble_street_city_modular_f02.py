"""Assemble only remesh-passed assets. Explicit draft mode omits missing assets.

No placeholders, no unremeshed geometry, no false final acceptance.
"""
import argparse,json,math,sys,hashlib
from pathlib import Path
import bpy
from mathutils import Vector,Matrix
ROOT=Path(__file__).resolve().parents[1];BASE=ROOT/'output/street-social-city-production/S01-modular-components-v1';OUT=BASE/'assembly-f02';OUT.mkdir(exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--draft',action='store_true');a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
m=json.loads((ROOT/'Docs/StreetSocialCity/production/assembly-manifest.json').read_text('utf-8'));b=next(x for x in m['layouts'] if x.get('kind')=='building' and x['building']=='S01');f=b['small_shop_design']['floors'][1];z=f['z_m']
bpy.ops.wm.read_factory_settings(use_empty=True);cache={};sources={};missing=[];instances=[]
def asset_path(aid):
    if aid=='structure':return OUT/'structure/remesh'
    if aid=='bookshelf':return BASE/'board-bookshelf/remesh-box'
    if aid in ('chair','toilet','sink'):return BASE/f'generated-components/{aid}/remesh-production'
    return BASE/f'local-components/{aid}/remesh'
def load(aid):
    if aid in cache:return cache[aid]
    folder=asset_path(aid);rp=folder/'remesh-report.json';glb=folder/'remeshed-candidate.glb'
    report=json.loads(rp.read_text('utf-8')) if rp.exists() else {}
    source=Path(report.get('source',''))
    if not source.is_absolute():source=ROOT/source
    valid_source=source.is_file() and hashlib.sha256(source.read_bytes()).hexdigest()==report.get('source_sha256')
    if not rp.exists() or not glb.exists() or report.get('status')!='passed' or not valid_source:
        if aid not in missing:missing.append(aid)
        cache[aid]=None;return None
    old=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(glb));new=list(set(bpy.data.objects)-old);meshes=[o for o in new if o.type=='MESH'];bpy.context.view_layer.update()
    # Copy only owned scalar metadata. IDPropertyArray/Group values borrow the
    # imported object's memory and become invalid after that object is removed.
    cache[aid]=[(o.data,o.matrix_world.copy(),o.name,{k:o.get(k) for k in ('part_kind','source_route','remesh_tool','remesh_part_passed') if k in o}) for o in meshes]
    for o in meshes:o.data.use_fake_user=True
    for o in new:bpy.data.objects.remove(o,do_unlink=True)
    sources[aid]=dict(path=str(glb),sha256=hashlib.sha256(glb.read_bytes()).hexdigest(),remesh_report=str(rp))
    return cache[aid]
def place(aid,name,pos=(0,0,0),yaw=0,scale=(1,1,1),parent=None):
    template=load(aid)
    if template is None:return None
    root=bpy.data.objects.new(name,None);bpy.context.collection.objects.link(root);root.parent=parent;root.location=pos;root.rotation_euler.z=yaw;root.scale=scale;root['asset_id']=aid
    for data,mat,n,props in template:
        o=bpy.data.objects.new(name+'/'+n,data);bpy.context.collection.objects.link(o);o.parent=root;o.matrix_parent_inverse=Matrix.Identity(4);o.matrix_basis=mat
        for k,v in props.items():o[k]=v
        o['asset_id']=aid;o['instance_id']=name
    instances.append(dict(asset=aid,name=name,position_m=list(pos),yaw_rad=yaw,scale=list(scale)))
    return root
place('structure','S01-F02-structure')
for i in range(6):
    x=-5.2;y=-7.5+i
    root=place('bookshelf',f'bookshelf-{i}',(x,y,z),math.pi/2)
    for bay in range(5):
        for j in range(14):place('book',f'book-{i}-{bay}-{j}',(-.43+j*.06,-.16,.027+bay*.395),parent=root)
groups=[q for q in f['equipment'] if q['bounds_m'][2]-q['bounds_m'][0]>2 and q['bounds_m'][3]-q['bounds_m'][1]<3]
for i,q in enumerate(groups):
    x0,y0,x1,y1=q['bounds_m'];x=(x0+x1)/2;y=(y0+y1)/2
    place('table',f'table-{i}',(x,y,z))
    for j,(dx,dy,angle) in enumerate([(0,.78,0),(0,-.78,math.pi),(.93,0,-math.pi/2),(-.93,0,math.pi/2)]):place('chair',f'chair-{i}-{j}',(x+dx,y+dy,z),angle)
for i,door in enumerate(f['doors']):
    x,y=door['center_m'];angle=0 if door['outward_side']=='south' else math.pi/2
    if angle==0:y+=.1
    else:x-=.1
    place('doorframe',f'{door["id"]}-frame',(x,y,z),angle)
    if door['kind']=='sliding':
        for sign in [-1,1]:place('liftpanel',f'lift-door-{sign}',(x,y+sign*.95,z),math.pi/2)
        continue
    hinge=bpy.data.objects.new(door['id']+'-hinge',None);bpy.context.collection.objects.link(hinge)
    hinge.location=(x-.63,y,z) if angle==0 else (x,y-.63,z);hinge.rotation_euler.z=angle+math.pi/2
    hinge['motion']='hinge';hinge['closed_rotation_z']=angle;hinge['open_rotation_z']=angle+math.pi/2
    leaf=place('door',door['id']+'-leaf',(.63,0,.02),scale=(.9966666667,1,1),parent=hinge)
    for side in [-1,1]:place('liftpanel',door['id']+f'-lever-{side}',(.43,side*.045,1.0),scale=(.14/.62,.045/.04,.03/2.4),parent=leaf)
place('lift','S01-EA1-cab',(-1.575,7.1,z-.05),math.pi/2)
for i,window in enumerate(f['exterior_openings']):
    xx,_,sill=window['local_position_m'];yy=-8.9 if window['side']=='south' else 8.9
    place('window',f'window-{i}',(xx,yy,z+sill),scale=(window['width_m']/2,1,window['height_m']/1.6))
place('toilet','toilet',(-5.43,4.9,z),math.pi/2)
place('sink','sink',(-5.54,2.9,z),math.pi/2)
bpy.context.view_layer.update()
if missing and not a.draft:
    (OUT/'missing-assets.json').write_text(json.dumps(missing,indent=2),'utf-8');raise RuntimeError('Final assembly refused: '+', '.join(missing))
tag='draft' if missing else 'assembled'
path=OUT/f'S01-F02-{tag}-2k.glb'
objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];triangles=0
for o in objects:o.data.calc_loop_triangles();triangles+=len(o.data.loop_triangles)
assert triangles<=100000,triangles
bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',export_extras=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/f'S01-F02-{tag}-2k.blend'))
# Orthographic cutaway is a presentation view; export retains all facade walls.
for o in objects:
    o.hide_render=o.get('part_kind')=='ceiling'
    if o.get('asset_id')=='structure':
        pts=[o.matrix_world@v.co for v in o.data.vertices]
        if max(v.y for v in pts)<-8.7 or min(v.x for v in pts)>5.7:o.hide_render=True
scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=32;scene.render.resolution_x=1600;scene.render.resolution_y=1500;scene.render.resolution_percentage=100
scene.world=bpy.data.worlds.new('Studio');scene.world.use_nodes=True;scene.world.node_tree.nodes['Background'].inputs[0].default_value=(.65,.65,.65,1)
target=Vector((0,0,5))
for loc,power in [((-6,-10,20),2500),((4,10,19),2000)]:
    bpy.ops.object.light_add(type='AREA',location=loc);o=bpy.context.object;o.data.energy=power;o.data.size=10;o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler()
bpy.ops.object.camera_add(location=(22,-28,36));o=bpy.context.object;o.rotation_euler=(target-o.location).to_track_quat('-Z','Y').to_euler();o.data.type='ORTHO';o.data.ortho_scale=26;scene.camera=o
scene.render.filepath=str(OUT/f'S01-F02-{tag}-review.png');bpy.ops.render.render(write_still=True)
report=dict(status='draft_missing_assets' if missing else 'assembled_requires_independent_acceptance',missing=missing,triangles=triangles,footprint_m=[12,18],z_m=z,height_m=f['height_m'],sources=sources,instances=instances,output=str(path),limitations=['Game controllers are not connected','Independent walking, door and stair acceptance required'],approved=False)
(OUT/'assembly-report.json').write_text(json.dumps(report,indent=2),'utf-8')
