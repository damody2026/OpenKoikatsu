"""Assemble facilities and terrain-conforming vegetation from actual Rust meshes."""
import bpy,sys,json,math,random
from pathlib import Path
from mathutils import Matrix,Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remaining_outdoor import ROOT,OUT,BASE,design,programme
from street_city_remesh_gate import verify,sha
from street_city_remaining_outdoor import sample
from street_city_mall import component_folders
PIN={str(Path(__file__)):sha(__file__),str(ROOT/'Tools/street_city_remaining_outdoor.py'):sha(ROOT/'Tools/street_city_remaining_outdoor.py')}
OUT.mkdir(parents=True,exist_ok=True);REVISION=OUT/'revisions'/(__import__('hashlib').sha256(''.join(PIN.values()).encode()).hexdigest()[:16]);REVISION.mkdir(parents=True,exist_ok=True);rows=design();_,field=programme();cache={};sources={};scene_rows=[]
colors={'grass':(.19,.34,.1),'wood':(.34,.19,.08),'stone':(.48,.48,.43),'steel':(.12,.15,.16),'tile':(.76,.78,.72),'roof':(.08,.12,.13),'glass':(.46,.67,.7),'light':(.98,.85,.55),'court':(.13,.3,.28),'white':(.9,.9,.85),'orange':(.8,.2,.035),'fabric':(.32,.42,.29)}

def source(aid):
 if aid in cache:return cache[aid]
 if aid in ('camphor','cherry','tent','fish'):folder=OUT.parent/'components'/aid/'remesh'
 elif aid in ('toilet','basin'):folder=OUT.parent/'components'/aid/'remesh-lod'
 elif aid in ('panel','cylinder','ring-low'):folder=BASE/'campus-v1/components'/('unit-'+aid)/'remesh'
 elif aid=='bicycle':folder=BASE/'shop-street-components-v1/bicycle-lod/remesh'
 else:folder=component_folders()[aid]
 receipt=verify(folder);sources[aid]=receipt;before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=receipt['path']);new=set(bpy.data.objects)-before;bpy.context.view_layer.update();result=[]
 for o in new:
  if o.type=='MESH':result.append((o.data.copy(),o.matrix_world.copy()))
 for o in new:bpy.data.objects.remove(o,do_unlink=True)
 cache[aid]=result;return result

def materials():
 result={}
 for name,color in colors.items():
  m=bpy.data.materials.new('Outdoor '+name);m.use_nodes=True;bs=m.node_tree.nodes['Principled BSDF'];bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Roughness'].default_value=.75
  if name=='steel':bs.inputs['Metallic'].default_value=.8
  if name=='glass':bs.inputs['Alpha'].default_value=.22;m.surface_render_method='DITHERED'
  if name=='light':bs.inputs['Emission Color'].default_value=(*color,1);bs.inputs['Emission Strength'].default_value=2
  result[name]=m
 return result

def assemble(q):
 bpy.ops.wm.read_factory_settings(use_empty=True);cache.clear();mats=materials();keys=list(mats);vv=[];ff=[];mi=[]
 for part in q['parts']:
  pos=part['pos'];mat=Matrix.Translation(Vector(pos))@Matrix.Rotation(math.radians(part.get('yaw',0)),4,'Z')@Matrix.Rotation(math.radians(part.get('pitch',0)),4,'X')@Matrix.Diagonal(Vector((*part['size'],1)))
  for mesh,basis in source(part['primitive']):
   start=len(vv);vs=[mat@basis@v.co for v in mesh.vertices]
   if 'slope_y' in part:
    for v in vs:v.z+=part['slope_center_z']-pos[2]+part['slope_y']*(v.y-pos[1])
   vv.extend(vs);ff.extend([start+i for i in p.vertices] for p in mesh.polygons);mi.extend([keys.index(part['finish'])]*len(mesh.polygons))
 mesh=bpy.data.meshes.new(q['id']+'-board-assembly');mesh.from_pydata([tuple(v) for v in vv],[],ff);mesh.update()
 for k in keys:mesh.materials.append(mats[k])
 uv=mesh.uv_layers.new(name='Metre UV')
 for poly,index in zip(mesh.polygons,mi):
  poly.material_index=index;axes=[k for k in range(3) if k!=max(range(3),key=lambda k:abs(poly.normal[k]))]
  for li in poly.loop_indices:
   v=mesh.vertices[mesh.loops[li].vertex_index].co;uv.data[li].uv=(v[axes[0]],v[axes[1]])
 ob=bpy.data.objects.new(q['id']+'-facilities',mesh);bpy.context.scene.collection.objects.link(ob)
 for i,placement in enumerate(q['curves']):
  factor=placement.get('scale',1);scale=[factor]*3 if isinstance(factor,(int,float)) else factor
  for j,(mesh,basis) in enumerate(source(placement['asset'])):
   ob=bpy.data.objects.new(q['id']+'-'+placement['asset']+f'-{i:03d}-{j}',mesh);bpy.context.scene.collection.objects.link(ob);ob.matrix_world=Matrix.Translation(Vector(placement['pos']))@Matrix.Rotation(math.radians(placement['yaw']),4,'Z')@Matrix.Diagonal(Vector((*scale,1)))@basis;ob['asset_id']=placement['asset']
 count=0
 for ob in bpy.context.scene.objects:
  if ob.type=='MESH':ob.data.calc_loop_triangles();count+=len(ob.data.loop_triangles)
 assert count<=100000,(q['id'],count)
 path=REVISION/(q['id']+'-complete-2k.glb');bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',export_extras=True,export_animations=False);bpy.ops.wm.save_as_mainfile(filepath=str(path.with_suffix('.blend')))
 row=dict(id=q['id'],name=q['name'],source=str(path),sha256=sha(path),position_m=q['origin'],triangles=count,features=q['features'],curved_instances=len(q['curves']),board_parts=len(q['parts']))
 scene_rows.append(row);return row

for q in rows:
 if not q.get('existing_asset'):assemble(q)

# Forests, riverbanks and the bay contain actual individual trees, not just a land texture.
rng=random.Random(20261019);layout=json.loads((ROOT/'Docs/StreetSocialCity/world-terrain-layout.json').read_text('utf8'));buildings=[f for f in layout['features'] if 'footprint_m' in f];positions=[];height=field['height_map']
for region,kind,target in [('forest',3,336),('riverbank',4,56),('bay',5,56)]:
 points=[]
 for attempt in range(100000):
  if len(points)>=target:break
  x,y=rng.uniform(15,1085),rng.uniform(15,1085);ix,iy=round(x),round(1100-y)
  if field['kind_map'][iy,ix]!=kind or field['water_map'][iy,ix] or field['road_map'][iy,ix]:continue
  if any((x-px)**2+(y-py)**2<7**2 for px,py in positions):continue
  if any(abs(x-f['world_center_m']['east'])<f['footprint_m'][0]/2+6 and abs(y-f['world_center_m']['north'])<f['footprint_m'][1]/2+6 for f in buildings):continue
  if any(abs(x-r['origin'][0])<r['size'][0]/2+5 and abs(y-r['origin'][1])<r['size'][1]/2+5 for r in rows):continue
  points.append((x,y));positions.append((x,y))
 assert len(points)==target,(region,len(points),target)
 for index in range(0,len(points),28):
  local=points[index:index+28];ox=sum(p[0] for p in local)/len(local);oy=sum(p[1] for p in local)/len(local);oz=0
  q=dict(id=f'Scenery-{region}-{index//28+1:02d}',name=region+' vegetation',origin=[ox,oy,oz],parts=[],curves=[],features=[dict(kind='terrain_conforming_trees',count=len(local),region=region)])
  for x,y in local:q['curves'].append(dict(asset='camphor',pos=[x-ox,y-oy,sample(height,x,1100-y)-.05],yaw=rng.uniform(0,360),scale=rng.uniform(.65,1.2)))
  assemble(q)

assert all(sha(path)==digest for path,digest in PIN.items()),'Source changed during outdoor assembly; rebuild'
receipt=dict(status='assembled_requires_acceptance',nodes=rows,models=scene_rows,sources=sources,vegetation_instances=len(positions),source_files=PIN,scope='physical facilities only; gameplay deferred')
(OUT/'assembly-report.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),'utf8');print('Outdoor assembly',len(scene_rows),'models',len(positions),'trees',flush=True)
