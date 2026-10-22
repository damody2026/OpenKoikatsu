"""Keep every original facility part, extract its trees to independently instanced LODs."""
import bpy,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import OUT,read,write,sha
legacy=read(OUT/'legacy-trees.json');rows=[]
for q in legacy['compound']:
 row=q['row'];source=Path(row['source']);assert sha(source)==row['sha256'];bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(source));removed=[]
 for ob in list(bpy.context.scene.objects):
  if ob.type=='MESH' and ob.get('asset_id') in ('camphor','cherry'):removed.append(ob.name);bpy.data.objects.remove(ob,do_unlink=True)
 assert len(removed)==q['tree_count'];folder=OUT/'facility-sources'/row['id'];folder.mkdir(parents=True,exist_ok=True);path=folder/'facility-without-embedded-trees.glb';bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',export_extras=True);triangles=0
 for ob in bpy.context.scene.objects:
  if ob.type=='MESH':ob.data.calc_loop_triangles();triangles+=len(ob.data.loop_triangles)
 rows.append(dict(label='World_'+row['id'].replace('-','_'),baseline_source=str(source),baseline_sha256=row['sha256'],path=str(path),sha256=sha(path),removed_trees=removed,tree_count=q['tree_count'],triangles=triangles))
write(OUT/'facility-sources.json',dict(status='built_requires_acceptance',models=rows,generator_sha256=sha(__file__)))
