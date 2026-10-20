"""Add accepted outdoor assets without rebuilding or altering accepted school floors."""
import bpy,sys,json,shutil,ast
from pathlib import Path
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_outdoor_contract import OUT,ROOT
from street_city_remesh_gate import verify,sha
from street_city_outdoor_gate import verify_outdoor
CAMPUS=OUT.parent;a=json.loads((OUT/'acceptance.json').read_text('utf8'));assert a['status']=='passed'
backup=OUT/'before';backup.mkdir(exist_ok=True)
for n in ['campus-complete-2k.blend','campus-complete-2k.glb','world-assembly.json','world-render-receipt.json','world-acceptance.json','campus-overview.png']:
 if not (backup/n).exists():shutil.copy2(CAMPUS/n,backup/n)
bpy.ops.wm.open_mainfile(filepath=str(CAMPUS/'campus-complete-2k.blend'))
for o in list(bpy.data.objects):
 if o.get('outdoor_id') or o.name.startswith('Outdoor-'):bpy.data.objects.remove(o,do_unlink=True)
rows=[]
for q in a['assets']:
 r=verify_outdoor(q['id'],q['remesh']['sha256']);before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=r['path']);new=set(bpy.data.objects)-before;root=bpy.data.objects.new('Outdoor-'+q['id'],None);bpy.context.collection.objects.link(root);root.location=q['origin'];root['outdoor_id']=q['id']
 for o in new:
  if o.parent is None:o.parent=root
  o['outdoor_id']=q['id']
 rows.append(dict(id=q['id'],source=r['path'],sha256=r['sha256'],position_m=q['origin'],triangles=q['actual_triangles']))
# East-bank chunks include the existing bridge and water approach in the preview.
w=json.loads((CAMPUS/'world-assembly.json').read_text('utf8'));existing={q['id'] for q in w['terrain']};g=json.loads((CAMPUS.parent/'world-ground-v1/ground-acceptance.json').read_text('utf8'))
for q in g['chunks']:
 if q['id'] not in ['G12','G16'] or q['id'] in existing:continue
 r=verify(Path(q['ground']['remesh_report']).parent,q['ground']['sha256']);before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=r['path']);root=bpy.data.objects.new('Campus-terrain-'+q['id'],None);bpy.context.collection.objects.link(root);root.location=q['world_origin_m']
 for o in set(bpy.data.objects)-before:
  if o!=root and o.parent is None:o.parent=root
 w['terrain'].append(dict(id=q['id'],source=r['path'],sha256=r['sha256'],position_m=q['world_origin_m']))
waters=[];terrain_ids={q['id'] for q in w['terrain']}
for ob in list(bpy.data.objects):
 if ob.get('world_water_chunk'):bpy.data.objects.remove(ob,do_unlink=True)
for q in g['chunks']:
 if q['id'] not in terrain_ids or not q.get('water'):continue
 r=verify(Path(q['water']['remesh_report']).parent,q['water']['sha256']);before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=r['path']);new=set(bpy.data.objects)-before;root=bpy.data.objects.new('Campus-water-'+q['id'],None);bpy.context.collection.objects.link(root);root.location=q['world_origin_m'];root['world_water_chunk']=q['id']
 for ob in new:
  if ob.parent is None:ob.parent=root
  ob['world_water_chunk']=q['id'];ob['ground_layer']='water'
 waters.append(dict(id=q['id'],source=r['path'],sha256=r['sha256'],position_m=q['world_origin_m']))
bpy.context.view_layer.update();bpy.ops.export_scene.gltf(filepath=str(CAMPUS/'campus-complete-2k.glb'),export_format='GLB',export_extras=True,export_animations=False);bpy.ops.wm.save_as_mainfile(filepath=str(CAMPUS/'campus-complete-2k.blend'))
w['outdoor']=rows;w['water']=waters;w['source_scope']='17 accepted buildings, current certified terrain/water and remeshed outdoor expansion';(CAMPUS/'world-assembly.json').write_text(json.dumps(w,indent=2),'utf8')
tree=ast.parse((ROOT/'Tools/render_street_city_campus.py').read_text('utf8'));exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('fit','studio')],type_ignores=[]),'camera-functions','exec'))
sc=studio(Vector((820,770,20)),850,(.8,-1.25,1.6));sc.render.resolution_x=2000;sc.render.resolution_y=1400;fit(sc,True);sc.render.filepath=str(CAMPUS/'campus-overview.png');bpy.ops.render.render(write_still=True)
for q in a['assets']:
 if q['id']=='connection':continue
 # An isolated scene avoids repeatedly evaluating thousands of hidden school
 # objects and proves that the individual download is the geometry pictured.
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=q['remesh']['path']);sc=studio(Vector((0,0,0)),max(q['size']),(.8,-1.25,1.6));sc.render.resolution_x=1200;sc.render.resolution_y=1000;sc.cycles.samples=12;fit(sc);sc.render.filepath=str(OUT/q['id']/'preview.png');bpy.ops.render.render(write_still=True)
(OUT/'assembly.json').write_text(json.dumps(dict(status='passed',assets=rows,model_sha256=sha(CAMPUS/'campus-complete-2k.glb'),blend_sha256=sha(CAMPUS/'campus-complete-2k.blend'),image_sha256=sha(CAMPUS/'campus-overview.png'),assembler_sha256=sha(Path(__file__)),images={str(p.relative_to(OUT)):sha(p) for p in OUT.glob('*/preview.png')}),indent=2),'utf8')
