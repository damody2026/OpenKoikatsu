"""Retain facility architecture exactly; only its embedded trees use certified LODs."""
import bpy,sys
from pathlib import Path
from mathutils import Matrix
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import OUT,BASE,read,write,sha
from street_city_remesh_gate import verify
data=read(OUT/'legacy-trees.json');assets={q['id']:q for q in read(OUT/'lod-assets.json')['assets']};basis={};lods={};rows=[]
for aid in ['camphor','cherry']:
 for level in range(3):
  row=assets[aid]['lods'][level];verify(Path(row['remesh_report']).parent,row['sha256']);bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=row['path']);bpy.context.view_layer.update();obs=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert len(obs)==1;o=obs[0]
  # Store plain arrays because Blender factory reset invalidates datablocks.
  lods[(aid,level)]=dict(vertices=[list(v.co) for v in o.data.vertices],faces=[list(p.vertices) for p in o.data.polygons],materials=[p.material_index for p in o.data.polygons],uvs=[list(v.uv) for v in o.data.uv_layers.active.data],basis=o.matrix_world.copy())
for q in data['compound']:
 original=q['row'];source=Path(original['source']);assert sha(source)==original['sha256'];levels=[]
 for level in [1,2]:
  bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(source));bpy.context.view_layer.update();changed=[]
  for o in list(bpy.context.scene.objects):
   aid=o.get('asset_id')
   if o.type!='MESH' or aid not in ('camphor','cherry'):continue
   src=lods[(aid,level)];oldm=list(o.data.materials);mesh=bpy.data.meshes.new(o.name+'-LOD'+str(level));mesh.from_pydata(src['vertices'],[],src['faces']);mesh.update()
   for m in oldm:mesh.materials.append(m)
   for p,index in zip(mesh.polygons,src['materials']):p.material_index=min(index,len(oldm)-1)
   uv=mesh.uv_layers.new(name='UVMap')
   for x,value in zip(uv.data,src['uvs']):x.uv=value
   o.data=mesh;o.matrix_world=o.matrix_world@lods[(aid,0)]['basis'].inverted()@src['basis'];changed.append(o.name)
  assert len(changed)==q['tree_count'];folder=OUT/'compound-lods'/original['id'];folder.mkdir(parents=True,exist_ok=True);path=folder/f'LOD{level}.glb';bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',export_extras=True);count=0
  for o in bpy.context.scene.objects:
   if o.type=='MESH':o.data.calc_loop_triangles();count+=len(o.data.loop_triangles)
  levels.append(dict(level=level,path=str(path),sha256=sha(path),triangles=count,replaced_trees=changed,remesh_sources=[assets[aid]['lods'][level] for aid in ['camphor','cherry']]))
 rows.append(dict(label='World_'+original['id'].replace('-','_'),baseline_sha256=original['sha256'],baseline_source=str(source),lods=levels,tree_count=q['tree_count']))
write(OUT/'compound-lods.json',dict(status='built_requires_acceptance',models=rows,generator_sha256=sha(__file__)))
