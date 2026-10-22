-- Native Blender geometry and atlas authoring; all new meshes pass the configured Rust remesher.
package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local root='C:/portable/OpenKoikatsu'
local series=os.getenv('STREET_CITY_TREE_SERIES') or 'v1';assert(series=='v1' or series=='v2')
local out=root..'/output/street-social-city-production/tree-upgrade-'..series
local specs=dofile('Tools/street_city_tree_species.lua')[series]
local mode=arg[1] or 'build'
local first,last=tonumber(arg[2]) or 1,tonumber(arg[3]) or (mode=='remesh' and #specs*3 or #specs)
assert(os.execute('if not exist "'..out..'" mkdir "'..out..'"'))
local function read(p)local f=assert(io.open(p,'rb'));local s=f:read('*a');f:close();return s end
local function write(p,v)local f=assert(io.open(p,'wb'));f:write(json.encode(v));f:close()end
local build=[====[
import bpy,bmesh,math,random,json,hashlib,numpy as np
from pathlib import Path
from mathutils import Vector,Matrix
ROOT=Path(TASK['root']);OUT=Path(TASK['out']);OUT.mkdir(exist_ok=True)
specs=TASK['specs']
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def image(name,data):
 im=bpy.data.images.new(name,width=data.shape[1],height=data.shape[0],alpha=True)
 if name!='bark-color':im.colorspace_settings.name='Non-Color'
 im.pixels.foreach_set(data.reshape(-1));im.filepath_raw=str(OUT/'textures'/(name+'.png'));im.file_format='PNG';im.save();bpy.data.images.remove(im)
(OUT/'textures').mkdir(exist_ok=True)
if True:
 y,x=np.mgrid[0:2048,0:2048].astype(np.float32)/2048
 h=.45*np.sin(x*2*math.pi*27+np.sin(y*2*math.pi*3)*.5)+.17*np.sin(x*2*math.pi*83+np.sin(y*2*math.pi*9))+.08*np.sin(x*2*math.pi*173+y*31);grain=np.clip(.5+h*.35,0,1);a=np.ones((2048,2048,4),np.float32)
 a[:,:,:3]=np.stack([.075+grain*.14,.038+grain*.085,.018+grain*.047],axis=2);image('bark-color',a)
 gy,gx=np.gradient(h);n=np.stack([-gx*75,-gy*75,np.ones_like(h)],axis=2);n/=np.linalg.norm(n,axis=2,keepdims=True);a[:,:,:3]=n*.5+.5;image('bark-normal',a)
 a[:,:,:3]=np.clip(.84+(grain-.5)*.1,0,1)[:,:,None];image('bark-roughness',a)
for idx,spec in enumerate(specs,1):
 aid,seed,width,depth,height,flower=spec[:6];profile=spec[6] if len(spec)>6 else None;leaf_factor={'broad':[.62,.8,.58],'tiered':[.85,1.0,.7],'columnar':[.45,.72,.64],'weeping':[.78,.9,.66]}.get(profile,[1,1,1]);canopy_color={'conifer':[.14,.30,.12],'palm':[.12,.32,.13],'golden':[.95,.56,.045],'white':[.83,.79,.65]}.get(profile,[.78,.38,.47])
 if not TASK['first']<=idx<=TASK['last']:continue
 folder=OUT/'assets'/aid;folder.mkdir(parents=True,exist_ok=True);lods=[]
 for lod in range(3):
  bpy.ops.wm.read_factory_settings(use_empty=True);rng=random.Random(seed);vv=[];ff=[];uvs=[];mids=[];colors=[]
  def vertex(p,c=(1,1,1,1)):vv.append(tuple(p));colors.append(c);return len(vv)-1
  def face(ids,uv,mat):ff.append(ids);uvs.extend(uv);mids.append(mat)
  def branch(points,radii):
   rings=[];sides=[9,7,5][lod]
   for j,p in enumerate(points):
    tangent=(Vector(points[min(j+1,len(points)-1)])-Vector(points[max(0,j-1)])).normalized();axis=tangent.cross(Vector((0,1,0))).normalized();other=tangent.cross(axis).normalized();rings.append([vertex(Vector(p)+radii[j]*(math.cos(k*2*math.pi/sides)*axis+math.sin(k*2*math.pi/sides)*other)) for k in range(sides)])
   for j in range(len(rings)-1):
    for k in range(sides):
     nk=(k+1)%sides;face([rings[j][k],rings[j][nk],rings[j+1][nk],rings[j+1][k]],[(k/sides,j*.4),(nk/sides if nk else 1,j*.4),(nk/sides if nk else 1,(j+1)*.4),(k/sides,(j+1)*.4)],0)
   face(list(reversed(rings[0])),[(0,0)]*sides,0);face(rings[-1],[(0,1)]*sides,0)
  done=False;anchors=[]
  if profile in ['tiered','columnar','conifer']:
   branch([(0,0,0),(.04,0,2.2),(.06,0,4.6),(0,0,7.6)],[.18,.13,.085,.025])
   tiers=5 if profile!='columnar' else 7
   for layer in range(tiers):
    u=layer/(tiers-1);z=(3.6+u*3.5) if profile=='conifer' else (2.8+u*4.3);radius=(2.35*(1-u*.82)) if profile!='columnar' else (.8*math.sin((u*.8+.1)*math.pi)+.25)
    for j in range(6 if profile!='columnar' else 4):
     angle=j*2*math.pi/(6 if profile!='columnar' else 4)+layer*.42;end=Vector((math.cos(angle)*radius,math.sin(angle)*radius,z));anchors.append(end);branch([(0,0,z-.22),end*.55+Vector((0,0,z*.45)),end],[.045,.025,.008])
   anchors.append(Vector((0,0,7.8)))
  elif profile=='palm':
   branch([(0,0,0),(.12,0,2),(.26,.04,4),(.3,.12,6.4),(.25,.12,7.1)],[.18,.16,.14,.12,.09]);sides=[14,10,6][lod];steps=[14,9,5][lod]
   for j in range(sides):
    angle=j*2*math.pi/sides;rad=Vector((math.cos(angle),math.sin(angle),0));lateral=Vector((-math.sin(angle),math.cos(angle),0));points=[]
    for k in range(steps+1):
     u=k/steps;p=Vector((.25,.12,7.1))+rad*(u*3)+Vector((0,0,.48*math.sin(u*math.pi)-u*u*1.7));points.append(p)
     if k==0:continue
     length=(.22+.7*math.sin(u*math.pi))
     for sign in [-1,1]:
      tip=p+lateral*(length*sign)+rad*.28+Vector((0,0,-.28*u));ids=[vertex(p-rad*.12),vertex(p+rad*.12),vertex(tip+rad*.025),vertex(tip-rad*.025)];face(ids,[(0,0),(1,0),(1,1),(0,1)],1)
    branch(points,[.026*(1-k/steps)+.005 for k in range(steps+1)])
   done=True
  elif profile=='weeping':
   branch([(0,0,0),(.05,0,2.4),(.14,0,4.4)],[.18,.13,.075])
   for j in range(8):
    angle=j*2*math.pi/8;rad=Vector((math.cos(angle),math.sin(angle),0));top=rad*1.8+Vector((0,0,5.8+rng.uniform(-.3,.3)));branch([(.1,0,3.4),top*.6+Vector((0,0,1.5)),top],[.065,.03,.012])
    for k in range(5):
     u=k/4;p=top+rad*u*.6+Vector((0,0,-u*u*1.8));anchors.append(p)
    anchors.append(rad*.8+Vector((0,0,6.6)))
  else:
   branch([(0,0,0),(.045,-.03,1.3),(.1,.025,2.5),(.16,0,3.5)],[.18,.155,.115,.07])
  for j in range(6 if profile not in ['tiered','columnar','conifer','weeping','palm'] else 0):
   angle=j*2*math.pi/6+rng.uniform(-.2,.2);r=(2.25 if profile=='broad' else 1.65)+rng.uniform(-.25,.3);p=Vector((math.cos(angle)*r,math.sin(angle)*r,(3.7 if profile=='broad' else 4.6)+rng.uniform(-.4,.4)));branch([(.08,0,2.2),p*.6+Vector((0,0,.8)),p],[.09,.055,.027])
   for k in range(3):
    theta=angle+(k-1)*.5;end=Vector((math.cos(theta)*(r+.65),math.sin(theta)*(r+.65),(5.7 if flower else 4.9)+rng.uniform(-.8,.8)));anchors.append(end)
    if lod<2:branch([p,end*.55+p*.45,end],[.035,.022,.01])
  if profile not in ['tiered','columnar','conifer','weeping','palm']:anchors.extend([Vector((-.7,.1,6.7)),Vector((.5,.1,6.9)),Vector((0,-.6,6.4))])
  if not flower and not done:
   for j,centre in enumerate(anchors):
    for k in range(([8,4,2] if profile in ['tiered','columnar','weeping'] else [20,10,4])[lod]):
     p=centre+Vector((rng.uniform(-.7,.7),rng.uniform(-.7,.7),rng.uniform(-.45,.55)));direction=(Vector((rng.uniform(-.15,.15),rng.uniform(-.15,.15),-1)) if profile=='weeping' else Vector((p.x*.45+rng.uniform(-.8,.8),p.y*.45+rng.uniform(-.8,.8),rng.uniform(.15,.9)))).normalized();rot=direction.to_track_quat('Y','Z').to_matrix();size=rng.uniform(.75,1.05)*[1,1.18,1.48][lod];ids=[];steps=2 if lod<2 else 1
     for y in range(steps+1):
      row=[]
      for x in range(steps+1):
       u=x/steps;v=y/steps;row.append(vertex(p+rot@Vector(((u-.5)*size,v*size,(u-.5)**2*size*.23)),(.9+rng.random()*.1,.9+rng.random()*.1,.85+rng.random()*.12,1)))
      ids.append(row)
     for y in range(steps):
      for x in range(steps):face([ids[y][x],ids[y][x+1],ids[y+1][x+1],ids[y+1][x]],[(x/steps,y/steps),((x+1)/steps,y/steps),((x+1)/steps,(y+1)/steps),(x/steps,(y+1)/steps)],1)
  elif not done:
   # Opaque scalloped flower masses, not stacked transparent cards.
   for j,p in enumerate(anchors):
    segments=[12,9,6][lod];rings=[6,5,4][lod];ids=[];shade=rng.uniform(.88,1.05)
    for lat in range(rings+1):
     row=[];theta=lat/rings*math.pi
     for k in range(segments):
      phi=k/segments*2*math.pi;radius=(.55 if profile=='conifer' else .79)*(1+.12*math.sin(phi*5+j));v=p+Vector((math.sin(theta)*math.cos(phi)*radius,math.sin(theta)*math.sin(phi)*radius,math.cos(theta)*(.9 if profile=='conifer' else .63)));row.append(vertex(v,(shade,shade*rng.uniform(.78,.94),shade*rng.uniform(.85,.99),1)))
     ids.append(row)
    for lat in range(rings):
     for k in range(segments):face([ids[lat][k],ids[lat][(k+1)%segments],ids[lat+1][(k+1)%segments],ids[lat+1][k]],[(0,0),(1,0),(1,1),(0,1)],1)
  fx=width*.5/max(abs(v[0]) for v in vv);fy=depth*.5/max(abs(v[1]) for v in vv);fz=height/max(v[2] for v in vv);vv=[(x*fx,y*fy,z*fz) for x,y,z in vv]
  mesh=bpy.data.meshes.new(aid+'-LOD'+str(lod));mesh.from_pydata(vv,[],ff);mesh.update();uv=mesh.uv_layers.new(name='TreeUV')
  for l,v in zip(uv.data,uvs):l.uv=v
  vc=mesh.color_attributes.new(name='Color',type='FLOAT_COLOR',domain='POINT')
  for c,v in zip(vc.data,colors):c.color=v
  bark=bpy.data.materials.new('TreeBark');bark.use_nodes=True;bs=bark.node_tree.nodes['Principled BSDF'];bs.inputs['Roughness'].default_value=.87
  for filename,socket,normal in [('bark-color.png','Base Color',False),('bark-normal.png','Normal',True),('bark-roughness.png','Roughness',False)]:
   n=bark.node_tree.nodes.new('ShaderNodeTexImage');n.image=bpy.data.images.load(str(OUT/'textures'/filename));n.image.pack()
   if filename!='bark-color.png':n.image.colorspace_settings.name='Non-Color'
   if normal:
    nm=bark.node_tree.nodes.new('ShaderNodeNormalMap');bark.node_tree.links.new(n.outputs['Color'],nm.inputs['Color']);bark.node_tree.links.new(nm.outputs['Normal'],bs.inputs[socket])
   else:bark.node_tree.links.new(n.outputs['Color'],bs.inputs[socket])
  leaves=bpy.data.materials.new('TreeFlowers' if flower else 'TreeLeaves');leaves.use_nodes=True;bs=leaves.node_tree.nodes['Principled BSDF'];bs.inputs['Roughness'].default_value=.88;vcnode=leaves.node_tree.nodes.new('ShaderNodeVertexColor');vcnode.layer_name='Color'
  if flower:
   bs.inputs['Base Color'].default_value=(*canopy_color,1);leaves.use_backface_culling=False
  else:
   n=leaves.node_tree.nodes.new('ShaderNodeTexImage');n.image=bpy.data.images.load(str(ROOT/'output/street-social-city-production/streetscape-v1/textures/camphor-leaves.png'));n.image.pack();leaves.node_tree.links.new(n.outputs['Color'],bs.inputs['Base Color']);leaves.node_tree.links.new(n.outputs['Alpha'],bs.inputs['Alpha']);leaves.surface_render_method='DITHERED';leaves.use_backface_culling=False
  mesh.materials.append(bark);mesh.materials.append(leaves)
  for p,index in zip(mesh.polygons,mids):p.material_index=index;p.use_smooth=True
  bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-6);bmesh.ops.dissolve_degenerate(bm,edges=list(bm.edges),dist=1e-7);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free();mesh.update()
  ob=bpy.data.objects.new(aid,mesh);bpy.context.collection.objects.link(ob);mesh.calc_loop_triangles();tri=len(mesh.loop_triangles);cap=[10000,6000,2500][lod];assert tri<=cap,(aid,lod,tri)
  path=folder/('LOD'+str(lod)+'-source.glb');bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',export_extras=True);lods.append(dict(level=lod,path=str(path),sha256=sha(path),triangles=tri,cap=cap))
 (folder/'build.json').write_text(json.dumps(dict(id=aid,width_m=width,depth_m=depth,height_m=height,flower=flower,profile=profile,leaf_factor=leaf_factor,canopy_color=canopy_color,lods=lods),indent=2),'utf8')
 print('TREE BUILT',aid,[r['triangles'] for r in lods])
]====]
local function blender(task,tag)
 write(out..'/'..tag..'-payload.json',task)
 local expression="import json;TASK=json.load(open('"..out..'/'..tag.."-payload.json',encoding='utf8'));exec(TASK['code'])"
 assert(os.execute('""C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --threads 8 --python-expr "'..expression..'" > "'..out..'/'..tag..'.log" 2>&1"'),tag..' process failed')
end
if mode=='build' then blender({root=root,out=out,first=first,last=last,specs=specs,code=build},'build-'..first..'-'..last)
elseif mode=='remesh' then
 local ids={};for _,s in ipairs(specs)do ids[#ids+1]=s[1] end
 for i=first,last do
  local aid=ids[math.floor((i-1)/3)+1];local lod=(i-1)%3;local folder=out..'/assets/'..aid;local spec=json.decode(read(folder..'/build.json')).lods[lod+1]
  local path=folder..'/LOD'..lod..'-remesh';local reportfile=io.open(path..'/remesh-report.json','rb');local report
  if reportfile then report=json.decode(reportfile:read('*a'));reportfile:close()end
  if not report or report.status~='passed' or not (report.arguments and report.arguments.smooth_shading) or report.source_sha256~=spec.sha256 then
   local command='""C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --threads 8 --python Tools/run_street_city_blender_task.py -- --script Tools/remesh_street_city_asset.py --status-file "'..folder..'/LOD'..lod..'-status.json" -- --source "'..spec.path..'" --output "'..path..'" --triangle-cap '..spec.cap..' --tolerance 0.015 --recover-source-topology --project-source --smooth-shading --threads 8 > "'..folder..'/LOD'..lod..'-remesh.log" 2>&1"'
   assert(os.execute(command),'Tree remesh process failed: '..aid..' LOD'..lod);report=json.decode(read(path..'/remesh-report.json'));assert(report.status=='passed',aid..' remesh rejected')
  end
  print(aid..' LOD'..lod..' remesh passed '..report.triangles)
 end
else error('Unknown mode: '..mode)end
