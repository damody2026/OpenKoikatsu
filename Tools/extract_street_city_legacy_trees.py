"""Read original exports and reproduce every regional tree's actual transform."""
import bpy,sys,math
from pathlib import Path
from mathutils import Matrix,Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import ROOT,BASE,OUT,read,write,sha
from street_city_remesh_gate import verify
sources={};basis={}
for aid in ['camphor','cherry']:
 source=verify(BASE/'world-completion-v1/components'/aid/'remesh');sources[aid]=source;bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=source['path']);bpy.context.view_layer.update();objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert len(objects)==1;basis[aid]=objects[0].matrix_world.copy()
plan=read(OUT/'plan.json');instances=[];clusters=[];compound=[];original=read(BASE/'world-completion-v1/outdoor/assembly-report.json')
for row in original['models']:
 source=Path(row['source']);assert sha(source)==row['sha256'];bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(source));bpy.context.view_layer.update();trees=[o for o in bpy.context.scene.objects if o.type=='MESH' and o.get('asset_id') in basis]
 if not trees:continue
 regional=row['id'].startswith('Scenery-')
 if not regional:compound.append(dict(row=row,tree_count=len(trees)))
 for ob in trees:
  aid=ob['asset_id'];transform=Matrix.Translation(Vector(row['position_m']))@ob.matrix_world@basis[aid].inverted();pos,rot,scale=transform.decompose();yaw=math.degrees(rot.to_euler().z)
  instances.append(dict(id='LEGACY-'+ob.name,asset=aid,position_m=list(pos),yaw_deg=yaw,scale=list(scale),region='forest' if 'forest' in row['id'] else 'riverside',origin=row['id'],legacy_source_sha256=row['sha256']))
 if regional:clusters.append(dict(label='World_'+row['id'].replace('-','_'),source_sha256=row['sha256'],source=str(source),count=len(trees),position_m=row['position_m']))
assert len(instances)==448+sum(q['tree_count'] for q in compound),(len(instances),'must retain regional and embedded facility trees')
write(OUT/'legacy-trees.json',dict(status='passed',instances=instances,clusters=clusters,compound=compound,sources=sources,generator_sha256=sha(__file__)))
print('EXTRACTED',len(instances),'regional trees;',len(compound),'compound facilities with trees')
