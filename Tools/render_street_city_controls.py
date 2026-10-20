"""Blender: current, hash-bound room/part three-view reference controls.

blender -b --factory-startup --python Tools/render_street_city_controls.py -- --room EE-0101
blender -b --factory-startup --python Tools/render_street_city_controls.py -- --asset COMMON-CITY-study_desk
Controls are never labelled as Tripo outputs or approved game geometry.
"""
import argparse,hashlib,json,math,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import bpy
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view
from street_city_parts import asset_geometry
from street_city_catalog import load_asset_catalog

ROOT=Path(__file__).resolve().parents[1];SOURCE=ROOT/'Docs/StreetSocialCity/production/assembly-manifest.json'
p=argparse.ArgumentParser();g=p.add_mutually_exclusive_group(required=True);g.add_argument('--room');g.add_argument('--asset')
args=p.parse_args(sys.argv[sys.argv.index('--')+1:]);key=args.room or args.asset
m=json.loads(SOURCE.read_text('utf-8'));assets=load_asset_catalog(SOURCE.parent);digest=hashlib.sha256(SOURCE.read_bytes()).hexdigest()
OUT=ROOT/'output/street-social-city-production/current-controls'/key;OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
def material(name,rgb):
    mat=bpy.data.materials.new(name);mat.diffuse_color=(*rgb,1);mat.use_nodes=True
    mat.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(*rgb,1)
    return mat
ivory=material('Ivory',(.85,.83,.76));oak=material('Pale oak',(.62,.43,.24));teal=material('Teal',(.08,.4,.43));steel=material('Steel',(.1,.12,.14))
walls=[]
def box(name,pos,size,mat,parent=None,wall=False):
    bpy.ops.mesh.primitive_cube_add(size=1);o=bpy.context.object;o.name=name;o.dimensions=size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    o.parent=parent;o.location=Vector(pos)+Vector((0,0,size[2]/2));o.data.materials.append(mat)
    if wall:walls.append(o)
    return o
def empty(name,pos,yaw):
    o=bpy.data.objects.new(name,None);bpy.context.collection.objects.link(o);o.location=pos;o.rotation_euler[2]=math.radians(yaw);return o
def desk(name,size,parent):
    w,d,h=size;box(name+' top',[0,0,h-.05],[w,d,.05],oak,parent)
    for x in (-w/2+.04,w/2-.04):
        for y in (-d/2+.04,d/2-.04):box(name+' leg',[x,y,0],[.05,.05,h-.05],steel,parent)
def chair(name,x,y,parent):
    box(name+' seat',[x,y,.42],[.42,.42,.06],teal,parent);box(name+' back',[x,y-.19,.48],[.42,.05,.4],teal,parent)
    for dx in (-.16,.16):
        for dy in (-.16,.16):box(name+' leg',[x+dx,y+dy,0],[.035,.035,.42],steel,parent)
def draw_asset(a,parent):
    if a.get('control_geometry')=='exact_rectangular_prism':
        box(a['id']+'_STRUCTURAL_CONTROL_NOT_TRIPO',[0,0,0],a['size_m'],ivory,parent)
        return
    parts,reason=asset_geometry(a)
    for j,q in enumerate(parts):box(a['id']+f' part{j}',q['position_m'],q['size_m'],ivory,parent,q['kind'].startswith('wall'))
    if parts:return
    label=a['name'];size=a['size_m']
    if a['id']=='COMMON-CITY-study_desk' or '寢室個人書桌' in label:desk(a['id'],size,parent)
    elif a['id']=='COMMON-CITY-seat':
        w,d,h=size;seat_h=.45
        box('chair seat',[0,0,seat_h-.06],[w,d,.06],teal,parent)
        box('chair back',[0,d/2-.03,seat_h],[w,.06,h-seat_h],teal,parent)
        for x in (-w/2+.04,w/2-.04):
            for y in (-d/2+.04,d/2-.04):box('chair leg',[x,y,0],[.045,.045,seat_h-.06],steel,parent)
    elif '雙人課桌' in label:
        dp=empty('desk offset',[0,.45,0],0);dp.parent=parent
        desk('student desk',[1.2,.6,.75],dp)
        chair('student chair L',-.3,-.35,parent);chair('student chair R',.3,-.35,parent)
    elif '教師講桌' in label:desk(a['id'],size,parent)
    elif a['role'] in ('furniture','annex_furniture','service_fixture','sanitary_fixture','sports_annex_furniture'):
        box(a['id']+'_DIMENSION_ENVELOPE',[0,0,0],size,teal,parent)

if args.room:
    room=next(q for q in m['layouts'] if q.get('room_id')==key);size=room['clear_size_m']
    for inst in m['instances']:
        if inst['owner']!=key:continue
        a=assets[inst['asset_id']]
        if a['role']=='door':continue # open leaf for inspection, aperture retained
        draw_asset(a,empty(inst['id'],inst['position_m'],inst['rotation_z_deg']))
    box('control_floor',[0,0,-.15],[size[0]+.4,size[1]+.4,.15],oak)
    selected=dict(room_id=key,size_m=size,capacity_students=room.get('dimensioned_programme',{}).get('capacity_students'))
else:
    a=assets[key];size=a['size_m'];draw_asset(a,empty(key,[0,0,0],0));selected=a
scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.render.engine='CYCLES';scene.cycles.samples=16
if not any(o.type=='MESH' for o in scene.objects):raise SystemExit('No physical control geometry for this asset; define the real shape before rendering')
scene.world.color=(.8,.8,.8);scene.render.resolution_x=768;scene.render.resolution_y=768;scene.render.resolution_percentage=100
scene.render.image_settings.file_format='PNG';scene.view_settings.view_transform='AgX'
scale=max(size);target=Vector((0,0,size[2]*.5));distance=scale*3
for loc in [(-scale,-scale,scale*2),(scale,scale,scale*1.6)]:
    bpy.ops.object.light_add(type='AREA',location=loc);bpy.context.object.data.energy=180*scale*scale;bpy.context.object.data.size=scale
bpy.ops.object.camera_add();cam=bpy.context.object;cam.data.lens=46;scene.camera=cam
views=[('front-left',(-1,-1,.9)),('rear-right',(1,1,.9)),('high-top',(.55,-.8,1.7))]
bpy.context.view_layer.update()
for name,direction in views:
    for o in walls:
        pos=o.matrix_world.translation;o.hide_render=bool(args.room and (pos.x*direction[0]>0 or pos.y*direction[1]>0))
    distance=scale*2
    corners=[o.matrix_world@Vector(c) for o in scene.objects if o.type=='MESH' and not o.hide_render for c in o.bound_box]
    for attempt in range(30):
        cam.location=target+Vector(direction).normalized()*distance;cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler()
        bpy.context.view_layer.update();projected=[world_to_camera_view(scene,cam,v) for v in corners]
        if all(.08<=v.x<=.92 and .08<=v.y<=.92 and v.z>0 for v in projected):break
        distance*=1.12
    else:raise RuntimeError('Control camera cannot fit geometry')
    scene.render.filepath=str(OUT/f'control-{name}.png');bpy.ops.render.render(write_still=True)
for o in walls:o.hide_render=False
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'control.blend'))
bpy.ops.object.select_all(action='DESELECT')
for o in scene.objects:
    if o.type=='MESH':o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(OUT/'control.glb'),export_format='GLB',use_selection=True)
(OUT/'control.json').write_text(json.dumps(dict(status='current_control_geometry_not_tripo',manifest_sha256=digest,
    selected=selected,views=[x[0] for x in views],contains_dimension_envelopes=bool(args.room),actual_model_approved=False),ensure_ascii=False,indent=2),'utf-8')
