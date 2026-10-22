-- Bake adopted remeshed trees into sixteen-view atlases. No generated-image service is needed.
package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local root='C:/portable/OpenKoikatsu'
local series=os.getenv('STREET_CITY_TREE_SERIES') or 'v1';assert(series=='v1' or series=='v2')
local out=root..'/output/street-social-city-production/tree-upgrade-'..series
local code=[====[
import bpy,math,json,hashlib,numpy as np
from pathlib import Path
from mathutils import Vector
ROOT=Path(TASK['root']);OUT=Path(TASK['out'])
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
for aid in TASK['ids']:
 folder=OUT/'assets'/aid;spec=json.loads((folder/'build.json').read_text());height=spec['height_m'];extent=12 if not spec['flower'] else 9;atlasfolder=folder/'atlas';atlasfolder.mkdir(exist_ok=True)
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(folder/'LOD0-remesh/remeshed-candidate.glb'));objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert objects
 scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.samples=16;scene.cycles.transparent_max_bounces=32;scene.cycles.use_denoising=False;scene.render.resolution_x=512;scene.render.resolution_y=512;scene.render.resolution_percentage=100;scene.render.film_transparent=True;scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA';scene.view_settings.view_transform='Standard';scene.view_settings.look='None';scene.view_settings.exposure=0;scene.view_settings.gamma=1
 camera=bpy.data.cameras.new('AtlasCamera');cam=bpy.data.objects.new('AtlasCamera',camera);scene.collection.objects.link(cam);scene.camera=cam;camera.type='ORTHO';camera.ortho_scale=extent
 mats=[]
 for ob in objects:
  for m in ob.data.materials:
   if m and m not in mats:mats.append(m)
 originals={m:m.node_tree.copy() for m in mats}
 def emission_mode(mode):
  for m in mats:
   nt=m.node_tree;nt.nodes.clear();output=nt.nodes.new('ShaderNodeOutputMaterial');em=nt.nodes.new('ShaderNodeEmission');trans=nt.nodes.new('ShaderNodeBsdfTransparent');mix=nt.nodes.new('ShaderNodeMixShader');nt.links.new(trans.outputs[0],mix.inputs[1]);nt.links.new(em.outputs[0],mix.inputs[2]);nt.links.new(mix.outputs[0],output.inputs[0]);mix.inputs[0].default_value=1
   leaf='Leaves' in m.name;flower='Flowers' in m.name
   if leaf:
    tex=nt.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(ROOT/'output/street-social-city-production/streetscape-v1/textures/camphor-leaves.png'),check_existing=True);clip=nt.nodes.new('ShaderNodeMath');clip.operation='GREATER_THAN';clip.inputs[1].default_value=.33;nt.links.new(tex.outputs['Alpha'],clip.inputs[0]);nt.links.new(clip.outputs[0],mix.inputs[0])
   if mode=='color':
    if leaf:
     mul=nt.nodes.new('ShaderNodeMixRGB');mul.blend_type='MULTIPLY';mul.inputs[0].default_value=1;mul.inputs[2].default_value=(*spec.get('leaf_factor',[1,1,1]),1);nt.links.new(tex.outputs['Color'],mul.inputs[1]);nt.links.new(mul.outputs[0],em.inputs['Color'])
    elif flower:em.inputs['Color'].default_value=(*spec.get('canopy_color',[.78,.38,.47]),1)
    else:
     tex=nt.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(OUT/'textures/bark-color.png'),check_existing=True);nt.links.new(tex.outputs['Color'],em.inputs['Color'])
   elif mode=='mask':em.inputs['Color'].default_value=(1,1,1,1) if leaf or flower else (0,0,0,1)
   else:
    geom=nt.nodes.new('ShaderNodeNewGeometry');transn=nt.nodes.new('ShaderNodeVectorTransform');transn.vector_type='NORMAL';transn.convert_from='WORLD';transn.convert_to='CAMERA';nt.links.new(geom.outputs['Normal'],transn.inputs[0]);scale=nt.nodes.new('ShaderNodeVectorMath');scale.operation='MULTIPLY_ADD';scale.inputs[1].default_value=(.5,.5,.5);scale.inputs[2].default_value=(.5,.5,.5);nt.links.new(transn.outputs[0],scale.inputs[0]);nt.links.new(scale.outputs[0],em.inputs['Color'])
 colors=np.zeros((2048,2048,4),np.float32);normals=colors.copy();frames=[]
 for mode in ['color','normal','mask']:
  emission_mode(mode);scene.view_settings.view_transform='Raw' if mode!='color' else 'Standard'
  for row in range(2):
   elevation=[0,45][row]
   for col in range(8):
    i=row*8+col;azimuth=col*45;az=math.radians(azimuth);el=math.radians(elevation);target=Vector((0,0,height*.5));cam.location=target+Vector((math.cos(az)*math.cos(el),math.sin(az)*math.cos(el),math.sin(el)))*25;cam.rotation_euler=(target-cam.location).to_track_quat('-Z','Y').to_euler();scene.render.filepath=str(atlasfolder/(mode+'-'+str(i)+'.png'));bpy.ops.render.render(write_still=True)
    # Blender pixels are bottom-up; atlas UV frame rows follow the same convention.
    im=bpy.data.images.load(scene.render.filepath,check_existing=False);data=np.array(im.pixels[:],np.float32).reshape((512,512,4));bpy.data.images.remove(im);y=(i//4)*512;x=(i%4)*512
    if mode=='color':colors[y:y+512,x:x+512]=data
    elif mode=='normal':normals[y:y+512,x:x+512,:3]=data[:,:,:3]
    else:normals[y:y+512,x:x+512,3]=data[:,:,0]
    if mode=='color':frames.append(dict(index=i,azimuth=azimuth,elevation=elevation))
 for name,data in [('color',colors),('normal-mask',normals)]:
  im=bpy.data.images.new(aid+'-'+name,width=2048,height=2048,alpha=True);im.colorspace_settings.name='Non-Color' if name!='color' else 'sRGB';im.pixels.foreach_set(data.reshape(-1));im.filepath_raw=str(atlasfolder/(name+'.png'));im.file_format='PNG';im.save();bpy.data.images.remove(im)
 # Billboard quad is a real remesh input; final geometry is never added without a gate receipt.
 bpy.ops.wm.read_factory_settings(use_empty=True);mesh=bpy.data.meshes.new('TreeImpostor');mesh.from_pydata([((x-.5)*extent,0,height*.5+(z-.5)*extent) for z in [0,.5,1] for x in [0,.5,1]],[],[(z*3+x,z*3+x+1,(z+1)*3+x+1,(z+1)*3+x) for z in range(2) for x in range(2)]);mesh.update();uv=mesh.uv_layers.new()
 for p in mesh.polygons:
  for li in p.loop_indices:
   v=mesh.vertices[mesh.loops[li].vertex_index].co;uv.data[li].uv=(v.x/extent+.5,1-(v.z-height*.5)/extent-.5)
 mat=bpy.data.materials.new('TreeImpostor');mesh.materials.append(mat);ob=bpy.data.objects.new(aid+'-Impostor',mesh);bpy.context.collection.objects.link(ob);bpy.ops.export_scene.gltf(filepath=str(folder/'LOD3-source.glb'),export_format='GLB');(folder/'atlas.json').write_text(json.dumps(dict(id=aid,extent_m=extent,height_m=height,frames=frames,source_sha256=sha(folder/'LOD0-remesh/remeshed-candidate.glb'),color=str(atlasfolder/'color.png'),normal_mask=str(atlasfolder/'normal-mask.png'),flower=spec['flower'],leaf_factor=spec.get('leaf_factor',[1,1,1]),canopy_color=spec.get('canopy_color',[.78,.38,.47])),indent=2),'utf8');print('ATLAS COMPLETE',aid)
]====]
local ids={};for _,q in ipairs(dofile('Tools/street_city_tree_species.lua')[series])do ids[#ids+1]=q[1] end
local selected={};for i=tonumber(arg[1]) or 1,tonumber(arg[2]) or #ids do selected[#selected+1]=ids[i] end
local p=out..'/bake-atlas-payload.json';local f=assert(io.open(p,'wb'));f:write(json.encode({root=root,out=out,ids=selected,code=code}));f:close()
local expr="import json;TASK=json.load(open('"..p.."',encoding='utf8'));exec(TASK['code'])"
assert(os.execute('""C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --threads 8 --python-expr "'..expr..'" > "'..out..'/bake-atlas.log" 2>&1"'))
for _,id in ipairs(selected) do local h=assert(io.open(out..'/assets/'..id..'/atlas.json','rb'),'Atlas failed; inspect bake-atlas.log');h:close() end
for _,id in ipairs(selected) do
 local folder=out..'/assets/'..id
 local command='""C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --threads 8 --python Tools/run_street_city_blender_task.py -- --script Tools/remesh_street_city_asset.py --status-file "'..folder..'/LOD3-status.json" -- --source "'..folder..'/LOD3-source.glb" --output "'..folder..'/LOD3-remesh" --triangle-cap 64 --tolerance 0.015 --recover-source-topology --project-source --threads 8 > "'..folder..'/LOD3-remesh.log" 2>&1"'
 assert(os.execute(command),'Impostor remesh failed: '..id)
 local f=assert(io.open(folder..'/LOD3-remesh/remesh-report.json','rb'));local r=json.decode(f:read('*a'));f:close();assert(r.status=='passed' and r.triangles<=16,id..' impostor rejected')
end
