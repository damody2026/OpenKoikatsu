"""Independent reimport, geometry preservation, actual support and stepped-route checks."""
import bpy,sys,json,math,argparse,hashlib
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_functional_revision import ROOT,OUT,sha,read
from street_city_remesh_gate import verify
p=argparse.ArgumentParser();p.add_argument('--building',required=True);a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);bid=a.building;rpath=Path(read(OUT/'buildings'/bid/'latest.json')['receipt']);r=read(rpath);folder=rpath.parent;checks=[];issues=[]
assert sha(r['generator_snapshot'])==r['generator_sha256'],'Immutable generator snapshot required'
if bid!='M1':
 from street_city_campus_contract import IDS,context as campus
 from street_city_remaining_contract import context as remaining
 context=campus(bid) if bid in IDS else remaining(bid)
 expected_teaching={q['room_id'] for q in context['rooms'].values() if q.get('room_type')=='class' or '教室' in q['display_name']}
 expected_admin={q['room_id'] for q in context['rooms'].values() if q.get('functional_programme',{}).get('kind') in ('admin','reception') or q.get('room_type') in ('office','administration','admin') or any(t in q['display_name'] for t in ('辦公','系辦','站務','售票','旅遊資訊','志工服務','銀行服務'))}
 assert expected_teaching=={q['room'] for q in r['classrooms']},'All teaching spaces must be covered'
 assert expected_admin=={q['room'] for q in r['administration']},'All administrative and staffed service spaces must be covered'
def check(ok,name,**data):
 row=dict(passed=bool(ok),name=name,**data);checks.append(row)
 if not ok:issues.append(row)
verify(Path(r['certified_panel']['remesh_report']).parent,r['certified_panel']['sha256']);check(sha(r['source'])==r['source_sha256'],'immutable baseline source');check(sha(r['model'])==r['model_sha256'],'actual revised model identity')
def load(path):
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(path));bpy.context.scene.frame_set(1);bpy.context.view_layer.update();return {o.name:o for o in bpy.context.scene.objects if o.type=='MESH'}
def digest(o):
 h=hashlib.sha256()
 for v in o.data.vertices:h.update(str(tuple(round(float(c),5) for c in v.co)).encode())
 for poly in o.data.polygons:h.update(str(tuple(poly.vertices)).encode())
 return h.hexdigest()
original=load(r['source']);original={n:dict(digest=digest(o),matrix=[list(row) for row in o.matrix_world],kind=o.get('part_kind'),floor=o.get('floor')) for n,o in original.items()};objects=load(r['model']);removed={q['name'] for q in r['removed_seats']};moved={q['name']:q for q in r['moved_seats']};skipped_windows={n for n,v in original.items() if bid=='M1' and v['kind']=='window'}
for name,old in original.items():
 if name in removed or name in skipped_windows:check(name not in objects,'intended removal '+name);continue
 check(name in objects,'preserved object '+name)
 if name not in objects:continue
 o=objects[name];check(digest(o)==old['digest'],'preserved certified topology '+name)
 expected=Matrix(old['matrix'])
 if name in moved:expected.translation.z+=moved[name]['lift_m']
 check(max(abs(o.matrix_world[i][j]-expected[i][j]) for i in range(4) for j in range(4))<.00003,'preserved or declared transform '+name)
def tree(items):
 vv=[];ff=[]
 for o in items:
  off=len(vv);vv.extend(o.matrix_world@v.co for v in o.data.vertices);ff.extend([off+j for j in f.vertices] for f in o.data.polygons)
 return BVHTree.FromPolygons(vv,ff)
new_objects=[o for o in objects.values() if o.get('revision')=='functional-interiors-v1'];structure=tree([o for o in objects.values() if o.get('part_kind') in ('wall','floor','stair_tread','ceiling')]);baseline_tree=tree([o for n,o in objects.items() if (n in original and n not in removed and n not in skipped_windows) or o.get('part_kind')=='office_desk']);tiers=tree([o for o in new_objects if o.get('part_kind')=='lecture_tiers']) if r['moved_seats'] else None
for q in r['additions']:
 check(q['name'] in objects,'actual addition '+q['name']);o=objects[q['name']];check(o.get('remesh_source_sha256')==r['certified_panel']['sha256'],'certified panel provenance '+q['name'])
 if q['kind']=='office_computer':
  origin=Vector(q['position_m']);rot=Matrix.Rotation(math.radians(q['yaw']),4,'Z');part=q['parts'][0];point=origin+rot@Vector(part['pos']);hit=baseline_tree.ray_cast(point+Vector((0,0,.002)),Vector((0,0,-1)),.035)[0];check(hit is not None,'computer rests on actual desk '+q['name'],position=list(point))
for room in r['classrooms']:
 check(room['room']+'-teaching-board-screen' in objects,'teaching display in every classified classroom '+room['room'])
 # Inspect reimported, material-separated vertices in room coordinates rather
 # than trusting a layout flag or a preview image.
 display=objects[room['room']+'-teaching-board-screen'];groups={}
 for poly in display.data.polygons:
  material=display.data.materials[poly.material_index].name
  groups.setdefault(material,[]).extend(display.data.vertices[i].co.copy() for i in poly.vertices)
 def bounds(token):
  points=[v for name,vs in groups.items() if token in name for v in vs]
  assert points,(room['room'],token)
  return [(min(v[j] for v in points),max(v[j] for v in points)) for j in range(3)]
 bb=bounds('Functional blackboard');sb=bounds('Functional projection fabric')
 check(abs(sum(bb[0]))<.0001 and abs(sum(sb[0]))<.0001,'board and fabric share front-wall centre '+room['room'])
 check(bb[0][1]-bb[0][0]>=6 and bb[2][1]-bb[2][0]>=1.49,'one large continuous blackboard '+room['room'])
 check(sb[0][0]>bb[0][0]+.5 and sb[0][1]<bb[0][1]-.5 and sb[2][0]<=bb[2][0]+.001 and sb[2][1]>=bb[2][1],'lowered screen covers board centre with exposed wings '+room['room'])
 check(.10<bb[1][0]-sb[1][1]<.25,'fabric physically in front of board without coplanar overlap '+room['room'])
 rec=next(q for q in r['additions'] if q['owner']==room['room'] and q['kind']=='teaching_display')
 roller=[q for q in rec['parts'] if q['finish']=='frame' and abs(q['size'][0]-(sb[0][1]-sb[0][0]+.16))<.001 and abs(q['size'][2]-.12)<.001]
 check(len(roller)==1 and abs(roller[0]['pos'][0])<.001 and abs(roller[0]['pos'][2]-sb[2][1])<.001,'centred roller directly above fabric '+room['room'])
 if room['tiered']:
  check(room['central_aisle_width_m']>=2 and room['flat_side_bypass_m']>=2,'two metre central and flat bypass '+room['room']);check(room['minimum_headroom_m']>=2.2,'tier headroom '+room['room']);check(room['seats_after']>0 and room['seats_after']<room['seats_before'],'honest reduced seat count '+room['room'])
  rec=next(q for q in r['additions'] if q['owner']==room['room'] and q['kind']=='lecture_tiers');origin=Vector(rec['position_m']);rot=Matrix.Rotation(math.radians(rec['yaw']),4,'Z')
  for yy,hh in [(-2.85,.12),(-2.55,.24),(-2.25,.36),(-1.65,.48),(-1.05,.36),(-.025,.24),(1,.12)]:
   for xx in [-.8,0,.8]:
    v=origin+rot@Vector((xx,yy,hh));hit=tiers.ray_cast(v+Vector((0,0,.04)),Vector((0,0,-1)),.08)[0];check(hit is not None and abs(hit.z-v.z)<.012,'actual tier tread '+room['room']+f' {xx}/{yy}')
  # Side bypass is flat, outside the platforms; test ground support and 2.2m standing headroom.
  for xx in [-room['clear_size_m'][0]/2+1.05,room['clear_size_m'][0]/2-1.05]:
   for yy in [-3.5,-2,-.2,1.6,3.3]:
    v=origin+rot@Vector((xx,yy,0));hit=baseline_tree.ray_cast(v+Vector((0,0,.02)),Vector((0,0,-1)),.08)[0];check(hit is not None,'flat accessible side support '+room['room']+f' {xx}/{yy}');hit=structure.ray_cast(v+Vector((0,0,.03)),Vector((0,0,1)),2.2)[0];check(hit is None,'flat bypass headroom '+room['room']+f' {xx}/{yy}')
for q in r['opaque_bays']:
 o=objects[q['name']];bb=[o.matrix_world@Vector(v) for v in o.bound_box];spans=[max(v[i] for v in bb)-min(v[i] for v in bb) for i in range(3)];check(abs(min(spans[:2])-.18)<.0001,'real 180mm opaque infill '+q['name'])
 for m in o.data.materials:
  bs=m.node_tree.nodes.get('Principled BSDF');check(bs and bs.inputs['Alpha'].default_value==1 and bs.inputs['Transmission Weight'].default_value==0,'opaque nontransmitting facade '+q['name'])
if bid=='M1':check(not any(o.get('part_kind')=='window' for o in objects.values()),'no transparent external mall window meshes remain')
for f in r['floors']:
 check(sha(f['source'])==f['source_sha256'],'standalone floor identity '+f['id']);subset=[o for o in objects.values() if o.get('floor')==f['floor'] and o.get('part_kind')!='water'];count=0
 for o in subset:o.data.calc_loop_triangles();count+=len(o.data.loop_triangles)
 check(count==f['triangles']<=100000,'actual per-floor triangle budget '+f['id'],triangles=count)
for im in bpy.data.images:
 if im.type=='IMAGE':check(max(im.size)<=2048,'2K image '+im.name)
report=dict(status='failed' if issues else 'passed',building=bid,receipt=str(rpath),receipt_sha256=sha(rpath),model_sha256=sha(r['model']),checks=len(checks),issues=issues,results=checks,scope='Reimported geometry, original preservation, teaching fixtures, actual support, stepped routes, headroom, opaque infill and per-floor budgets')
(folder/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8');assert not issues,str(issues[:8]);print('FUNCTIONAL ACCEPTED',bid,len(checks),flush=True)
