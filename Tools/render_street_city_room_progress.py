"""Blender: place verified real parts in a current room control, explicitly partial.

Unapproved furniture envelopes stay hidden. Structural controls remain grey and
are never registered as Tripo assets or runtime collision meshes.
"""
import argparse
import hashlib
import json
import math
import sys
from pathlib import Path
import bpy
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view

sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_io import write_json
from street_city_model_gate import inspect_glb,approval_errors

ROOT=Path(__file__).resolve().parents[1]
PROD=ROOT/'Docs/StreetSocialCity/production'
LIB=ROOT/'Content/IndependentResources/StreetSocialCity'
p=argparse.ArgumentParser();p.add_argument('--room',required=True)
args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
source=PROD/'assembly-manifest.json';digest=hashlib.sha256(source.read_bytes()).hexdigest()
manifest=json.loads(source.read_text('utf-8'))
layout=next(q for q in manifest['layouts'] if q.get('room_id')==args.room)
building=layout['building'];controls=ROOT/'output/street-social-city-production/assemblies'
control_meta=json.loads((controls/f'{building}-layout-proxy.json').read_text('utf-8'))
if control_meta['source_manifest_sha256']!=digest:
    raise SystemExit('Room progress requires current control geometry')
ledger=json.loads((PROD/'generation-ledger.json').read_text('utf-8'))
specs={a['id']:a for a in manifest['assets']}
instances=[q for q in manifest['instances'] if q['owner']==args.room]
verified={};missing=[]
for aid in {q['asset_id'] for q in instances}:
    spec=specs[aid];model=LIB/spec['expected_file'];meta=LIB/spec['approval_file']
    if not model.is_file() or not meta.is_file():missing.append(aid);continue
    actual=inspect_glb(model)
    errors=approval_errors(spec,actual,json.loads(meta.read_text('utf-8')),ledger,ROOT)
    if errors:raise SystemExit(aid+': '+str(errors))
    verified[aid]=(model,actual)
if not verified:raise SystemExit('No verified real part in this room')
out=ROOT/'output/street-social-city-production/room-progress'/args.room
out.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(controls/f'{building}-layout-proxy.blend'))
room=bpy.data.objects.get(args.room)
if not room:raise SystemExit('Room parent missing from current controls')
def belongs(obj):
    while obj:
        if obj==room:return True
        obj=obj.parent
    return False
keep={o for o in bpy.data.objects if belongs(o)}
room.parent=None;room.location=(0,0,0);room.rotation_euler=(0,0,0)
for obj in list(bpy.data.objects):
    if obj not in keep:bpy.data.objects.remove(obj,do_unlink=True)
placed=[]
for instance in instances:
    aid=instance['asset_id']
    if aid not in verified:continue
    # Controls use half-height centre placement; actual source uses bottom pivot.
    # Read canonical instance placement rather than copying the envelope matrix.
    for obj in list(bpy.data.objects):
        if obj.name.startswith(instance['id']+'_CONTROL'):
            bpy.data.objects.remove(obj,do_unlink=True)
    before=set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(verified[aid][0]))
    imported=set(bpy.data.objects)-before
    anchor=bpy.data.objects.new(instance['id']+'_VERIFIED_STATIC_PART',None)
    bpy.context.scene.collection.objects.link(anchor);anchor.parent=room
    anchor.location=instance['position_m'];anchor.rotation_euler[2]=math.radians(instance['rotation_z_deg'])
    for obj in imported:
        if obj.parent not in imported:obj.parent=anchor
        obj['source_model_sha256']=verified[aid][1]['model_sha256']
        obj['approved_scope']='static asset; runtime collision/performance pending'
    bpy.context.view_layer.update()
    points=[obj.matrix_world @ v.co for obj in imported if obj.type=='MESH' for v in obj.data.vertices]
    actual_low=[min(v[k] for v in points) for k in range(3)]
    actual_high=[max(v[k] for v in points) for k in range(3)]
    spec=specs[aid];sw,sd,sh=spec['size_m'];yaw=math.radians(instance['rotation_z_deg'])
    lo_x=0 if spec['pivot']=='hinge_bottom' else -sw/2
    hi_x=sw if spec['pivot']=='hinge_bottom' else sw/2
    expected=[]
    for x in (lo_x,hi_x):
        for y in (-sd/2,sd/2):
            for z in (0,sh):expected.append([instance['position_m'][0]+x*math.cos(yaw)-y*math.sin(yaw),instance['position_m'][1]+x*math.sin(yaw)+y*math.cos(yaw),instance['position_m'][2]+z])
    expected_low=[min(v[k] for v in expected) for k in range(3)]
    expected_high=[max(v[k] for v in expected) for k in range(3)]
    if any(abs(a-b)>.003 for a,b in zip(actual_low+actual_high,expected_low+expected_high)):
        raise SystemExit(instance['id']+': placed actual mesh differs from manifest transform')
    placed.append(dict(instance_id=instance['id'],asset_id=aid,position_m=instance['position_m'],
        rotation_z_deg=instance['rotation_z_deg'],model_sha256=verified[aid][1]['model_sha256'],
        placed_bounds_min_m=actual_low,placed_bounds_max_m=actual_high,actual_vertex_placement_passed=True))

w,d,h=layout['clear_size_m']
grey=bpy.data.materials.new('GREY_LAYOUT_CONTROL_NOT_TRIPO');grey.diffuse_color=(.72,.74,.73,1)
bpy.ops.mesh.primitive_cube_add(size=1,location=(0,0,-.1))
floor=bpy.context.object;floor.name='GREY_CONTROL_FLOOR_NOT_TRIPO';floor.dimensions=(w+.4,d+.4,.2)
bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);floor.data.materials.append(grey)
for obj in bpy.data.objects:
    if obj.type=='MESH' and obj.get('control_only'):
        # Front cutaway is for viewing only. It is not an exported open facade.
        if abs(obj.location.y)>d/2 and obj.location.y<0:obj.hide_render=True
        if abs(obj.location.x)>w/2 and obj.location.x<0:obj.hide_render=True
        if not obj.hide_render:obj.data=obj.data.copy();obj.data.materials.clear();obj.data.materials.append(grey)

scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=16
scene.render.resolution_x=1280;scene.render.resolution_y=1000;scene.render.resolution_percentage=100
scene.world.color=(.65,.65,.65)
bpy.context.view_layer.update()
points=[obj.matrix_world @ Vector(c) for obj in scene.objects if obj.type=='MESH' and not obj.hide_render for c in obj.bound_box]
target=Vector((0,0,h/3))
for pos in [(-w,-d,h*3),(w,d,h*4)]:
    bpy.ops.object.light_add(type='AREA',location=pos);bpy.context.object.data.energy=200*max(w,d)**2;bpy.context.object.data.size=max(w,d)
bpy.ops.object.camera_add();cam=bpy.context.object;scene.camera=cam;cam.data.lens=44
distance=max(w,d)*2
for _ in range(40):
    cam.location=target+Vector((-1,-1,1.35)).normalized()*distance
    cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();bpy.context.view_layer.update()
    projected=[world_to_camera_view(scene,cam,v) for v in points]
    if all(.06<=v.x<=.94 and .06<=v.y<=.94 and v.z>0 for v in projected):break
    distance*=1.1
else:raise SystemExit('Room progress camera cannot fit geometry')
scene['production_status']='partial_verified_parts_with_grey_controls_NOT_COMPLETE'
scene.render.filepath=str(out/'progress.png');bpy.ops.render.render(write_still=True)
bpy.ops.wm.save_as_mainfile(filepath=str(out/'partial-preview.blend'))
write_json(out/'progress.json',dict(room_id=args.room,source_manifest_sha256=digest,
    status='partial_verified_parts_with_grey_controls_NOT_COMPLETE',verified_asset_types=len(verified),
    placed_verified_instances=placed,missing_semantic_assets=sorted(missing),
    structure='grey layout controls, NOT Tripo models',runtime_verified=False,world_complete=False))
print(json.dumps(dict(room=args.room,verified_asset_types=len(verified),placed_verified_instances=len(placed),complete=False)))
