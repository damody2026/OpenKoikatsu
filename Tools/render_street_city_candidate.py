"""Blender renders of actual imported candidates, never a control substitute."""
import argparse,json,math,sys
from pathlib import Path
import bpy
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view
p=argparse.ArgumentParser();p.add_argument('--blend',required=True);p.add_argument('--out',type=Path,required=True);args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
args.blend=str(Path(args.blend).resolve());args.out=args.out.resolve()
bpy.ops.wm.open_mainfile(filepath=args.blend);scene=bpy.context.scene
meshes=[o for o in scene.objects if o.type=='MESH'];points=[o.matrix_world @ v.co for o in meshes for v in o.data.vertices]
lo=Vector([min(v[k] for v in points) for k in range(3)]);hi=Vector([max(v[k] for v in points) for k in range(3)]);center=(lo+hi)/2
corners=[Vector((x,y,z)) for x in (lo.x,hi.x) for y in (lo.y,hi.y) for z in (lo.z,hi.z)]
neutral=bpy.data.materials.new('Untextured source inspection');neutral.diffuse_color=(.55,.6,.63,1);neutral.use_nodes=True;neutral.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(.55,.6,.63,1)
for o in meshes:
    if not o.data.materials:o.data.materials.append(neutral)
scene.render.engine='CYCLES';scene.cycles.samples=24;scene.render.resolution_x=768;scene.render.resolution_y=768;scene.render.resolution_percentage=100
scene.world.color=(.6,.6,.6);args.out.mkdir(parents=True,exist_ok=True)
for pos,power in [((2,-3,4),300),((-3,1,3),180)]:
    data=bpy.data.lights.new('inspection light','AREA');data.energy=power;data.shape='DISK';data.size=3
    o=bpy.data.objects.new('inspection light',data);scene.collection.objects.link(o);o.location=pos;o.rotation_euler=(center-o.location).to_track_quat('-Z','Y').to_euler()
for name,direction in [('front-left',(-1,-1,.8)),('rear-right',(1,1,.8)),('high-top',(1,-1,2.3))]:
    data=bpy.data.cameras.new(name);cam=bpy.data.objects.new(name,data);scene.collection.objects.link(cam);scene.camera=cam;distance=(hi-lo).length
    for _ in range(60):
        cam.location=center+Vector(direction).normalized()*distance;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler();bpy.context.view_layer.update()
        projected=[world_to_camera_view(scene,cam,v) for v in corners]
        if all(.08<=v.x<=.92 and .08<=v.y<=.92 and v.z>0 for v in projected):break
        distance*=1.12
    else:raise RuntimeError('Candidate camera cannot fit actual mesh')
    scene.render.filepath=str(args.out/f'{name}.png');bpy.ops.render.render(write_still=True)
print(json.dumps(dict(source=args.blend,mesh_objects=len(meshes),vertices=sum(len(o.data.vertices) for o in meshes),triangles=sum(len(o.data.polygons) for o in meshes),bounds_min=list(lo),bounds_max=list(hi),status='actual candidate inspection; not approved')))
