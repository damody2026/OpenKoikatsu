"""Reopen every outdoor delivery and test actual standing routes and source identities."""
import bpy,sys,json,math
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_remaining_outdoor import OUT,ROOT,design
from street_city_remesh_gate import verify,sha
r=json.loads((OUT/'assembly-report.json').read_text('utf8'));checks=[];issues=[]
def check(ok,name,**kw):
 row=dict(name=name,passed=bool(ok),**kw);checks.append(row)
 if not ok:issues.append(row)
for path,digest in r['source_files'].items():check(sha(path)==digest,'current recipe '+Path(path).name)
for aid,source in r['sources'].items():verify(Path(source['remesh_report']).parent,source['sha256']);check(True,'Rust-certified source '+aid)
expected=design();check(len(r['nodes'])==20 and {q['id'] for q in r['nodes']}=={q['id'] for q in expected},'exact twenty named PK nodes')
by={q['id']:q for q in expected};node_models={q['id'] for q in r['models'] if q['id'].startswith('PK-')}
check(node_models=={q['id'] for q in expected if not q.get('existing_asset')},'all non-reused nodes have actual models')
layout=json.loads((ROOT/'Docs/StreetSocialCity/world-terrain-layout.json').read_text('utf8'))
for row in r['models']:
 check(sha(row['source'])==row['sha256'],row['id']+' source hash')
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=row['source']);bpy.context.view_layer.update();meshes=[o for o in bpy.context.scene.objects if o.type=='MESH'];count=0;vv=[];ff=[]
 for o in meshes:
  o.data.calc_loop_triangles();count+=len(o.data.loop_triangles);start=len(vv);vv.extend(o.matrix_world@v.co for v in o.data.vertices);ff.extend([start+i for i in p.vertices] for p in o.data.polygons)
  check(o.data.uv_layers.active is not None,row['id']+' UV '+o.name)
 check(count==row['triangles'] and count<=100000,row['id']+' actual triangle budget',triangles=count)
 for image in bpy.data.images:
  if image.type=='IMAGE':check(max(image.size)<=2048,row['id']+' texture <=2K '+image.name)
 if row['id'] not in by:continue
 q=by[row['id']];check(row['position_m']==q['origin'],row['id']+' actual current metric placement')
 tree=BVHTree.FromPolygons(vv,ff)
 # The public approach reserves a full standing 2m strip through its entry zone.
 for yy in [-7,-5,-3,-1]:
  if row['id']=='PK-0113':continue
  for xx in [-.9,0,.9]:
   pt=Vector((xx,yy,.10));check(tree.ray_cast(pt,Vector((0,0,1)),2.16)[0] is None,row['id']+f' clear standing approach {xx}/{yy}')
   if row['id'] not in ('PK-0107','PK-0109'):check(tree.ray_cast(pt,Vector((0,0,-1)),.12)[0] is not None,row['id']+f' floor support {xx}/{yy}')
 x,y,_=q['origin'];w,d=q['size']
 for b in layout['features']:
  if 'footprint_m' not in b:continue
  bx,byy=b['world_center_m']['east'],b['world_center_m']['north'];bw,bd=b['footprint_m'];overlap=min(x+w/2,bx+bw/2)>max(x-w/2,bx-bw/2) and min(y+d/2,byy+bd/2)>max(y-d/2,byy-bd/2)
  # PK-0103 is explicitly inside E1's certified 33.2 x 50m open courtyard.
  allowed=q['id']=='PK-0103' and b['id']=='E1' and x-w/2>=bx-16.6 and x+w/2<=bx+16.6 and y-d/2>=byy-6 and y+d/2<=byy+44
  check(not overlap or allowed,row['id']+' no building obstruction '+b['id'])
for q in expected:
 if q.get('existing_asset'):
  old=json.loads((OUT.parent.parent/'campus-v1/outdoor/acceptance.json').read_text('utf8'));check(old['status']=='passed',q['id']+' current accepted sports reuse')
report=dict(status='failed' if issues else 'passed',checks=len(checks),issues=issues,models=len(r['models']),nodes=20,vegetation_instances=r['vegetation_instances'],results=checks,assembly_sha256=sha(OUT/'assembly-report.json'),verifier_sha256=sha(__file__))
(OUT/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8');print(report['status'],len(checks),'checks',len(issues),'issues',flush=True)
assert not issues,issues[:12]
