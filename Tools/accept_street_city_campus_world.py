"""Reimport the complete campus GLB and prove all floor counts/world transforms."""
import bpy,sys,json,hashlib
from pathlib import Path
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_campus_contract import IDS,OUT,context
from street_city_remesh_gate import verify
world=json.loads((OUT/'world-assembly.json').read_text('utf8'));assert world['status']=='passed' and len(world['buildings'])==17;checks=[];issues=[]
def check(ok,name,**data):
 row=dict(name=name,passed=bool(ok),**data);checks.append(row)
 if not ok:issues.append(row)
for row in world['buildings']:check(hashlib.sha256(Path(row['source']).read_bytes()).hexdigest()==row['sha256'],row['building']+' current accepted source')
ground=json.loads((OUT.parent/'world-ground-v1/ground-acceptance.json').read_text('utf8'));check(ground['status']=='passed','current terrain acceptance');ground_by_id={r['id']:r for r in ground['chunks']}
for row in world['terrain']:
 current=ground_by_id[row['id']]['ground'];verify(Path(current['remesh_report']).parent,current['sha256']);check(current['sha256']==row['sha256']==hashlib.sha256(Path(row['source']).read_bytes()).hexdigest(),row['id']+' exact current terrain version')
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(OUT/'campus-complete-2k.glb'));bpy.context.view_layer.update();group={bid:[] for bid in IDS};terrain_meshes={r['id']:[] for r in world['terrain']}
for o in bpy.context.scene.objects:
 if o.type!='MESH':continue
 root=o
 while root.parent:root=root.parent
 bid=root.get('building')
 if bid in group:group[bid].append(o)
 cid=o.get('chunk_id')
 if cid in terrain_meshes and not o.get('world_water_chunk'):terrain_meshes[cid].append(o)
for row in world.get('water',[]):
 receipt=verify(Path(ground_by_id[row['id']]['water']['remesh_report']).parent,row['sha256']);check(hashlib.sha256(Path(row['source']).read_bytes()).hexdigest()==receipt['sha256'],row['id']+' actual current certified river water source');objects=[o for o in bpy.context.scene.objects if o.type=='MESH' and o.get('world_water_chunk')==row['id']];count=0
 for o in objects:o.data.calc_loop_triangles();count+=len(o.data.loop_triangles)
 expected=json.loads(Path(receipt['remesh_report']).read_text('utf8'))['triangles'];check(count==expected,row['id']+' complete actual water triangles')
for cid,objects in terrain_meshes.items():
 row=ground_by_id[cid];receipt=json.loads(Path(row['ground']['remesh_report']).read_text('utf8'));count=0
 for o in objects:o.data.calc_loop_triangles();count+=len(o.data.loop_triangles)
 check(count==receipt['triangles'],cid+' all actual remeshed terrain triangles',triangles=count);base=next(o for o in objects if o.get('ground_layer')=='base');points=[base.matrix_world@Vector(v) for v in base.bound_box];ox,oy,_=row['world_origin_m'];check(max(abs(min(v.x for v in points)-ox),abs(max(v.x for v in points)-(ox+275)),abs(min(v.y for v in points)-oy),abs(max(v.y for v in points)-(oy+275)))<.005,cid+' actual geographic placement')
for bid,meshes in group.items():
 c=context(bid);r=json.loads((c['out']/'assembly-report.json').read_text('utf8'));a=json.loads((c['out']/'acceptance.json').read_text('utf8'));check(a['status']=='passed',bid+' source acceptance');W,D=c['building']['footprint_m'];x,y,z=c['building']['world_position_m']
 for f in c['building']['floors']:
  no=f['floor'];objects=[o for o in meshes if o.get('floor')==no];count=0
  for o in objects:o.data.calc_loop_triangles();count+=len(o.data.loop_triangles)
  check(count==a['floor_triangles'][str(no)]+r['water_triangles'][str(no)],bid+f' F{no:02d} complete actual triangle count',triangles=count)
  points=[o.matrix_world@Vector(v) for o in objects if o.get('part_kind') in ('floor','wall') for v in o.bound_box];lo=[min(v[k] for v in points) for k in range(3)];hi=[max(v[k] for v in points) for k in range(3)];check(max(abs(lo[0]-(x-W/2)),abs(hi[0]-(x+W/2)),abs(lo[1]-(y-D/2)),abs(hi[1]-(y+D/2)))<.005,bid+f' F{no:02d} exact world footprint',bounds=[lo,hi]);check(abs(lo[2]-(z+f['z_m']-.3))<.005,bid+f' F{no:02d} correct floor elevation')
for image in bpy.data.images:
 if image.type=='IMAGE':check(max(image.size)<=2048,image.name+' <=2K')
files={p:hashlib.sha256((OUT/p).read_bytes()).hexdigest() for p in ['campus-complete-2k.glb','campus-complete-2k.blend','world-assembly.json']};result=dict(status='failed' if issues else 'passed',building_count=17,floor_count=89,checks=len(checks),issues=issues,results=checks,files=files);(OUT/'world-acceptance.json').write_text(json.dumps(result,indent=2),'utf8');print(result['status'],len(checks),len(issues));assert not issues,str(issues[:10])
