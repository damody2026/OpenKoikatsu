"""Independently reimport the assembled GLB and inspect real stocked products."""
import bpy,sys,json,math
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_mall import OUT,sha
from street_city_mall_stock import catalogue
from street_city_mall_merchandise import OUT as STOCK,folder_for
from street_city_remesh_gate import verify
data=catalogue();products={q['id']:q for q in data['components']};geometry=json.loads((OUT/'geometry.json').read_text('utf8'));stock=geometry['contract']['merchandise'];checks=[]
def check(ok,name,**extra):checks.append(dict(passed=bool(ok),name=name,**extra))
check(len(products)==stock['unique_models']<=100,'at most 100 distinct product models')
check(len(stock['tenant_counts'])==80 and all(stock['tenant_counts'].values()),'all eighty tenants stocked')
for aid,q in products.items():
 r=verify(folder_for(q)/'remesh');check(r['sha256']==stock['source_hashes'][aid],aid+' exact Rust remesh source')
 if q['provider']=='Tripo Model':check(bool(q.get('job')) and (folder_for(q)/'tripo-source.glb').is_file(),aid+' recorded real Tripo job and immutable raw source')
for aid,digest in stock.get('display_lod_hashes',{}).items():
 receipt=verify(STOCK/'lods'/aid/'remesh');check(receipt['sha256']==digest,aid+' certified display LOD source')
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(OUT/'M1-complete-2k.glb'));bpy.context.view_layer.update();objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];actual={}
for o in objects:
 if o.get('placement_id'):actual.setdefault(o['placement_id'],[]).append(o)
check(set(actual)=={q['name'] for q in stock['placements']},'actual model placements equal complete stocking manifest')
surfaces={s['id']:s for s in stock['surfaces']};fixture_trees={}
for no in range(1,11):
 vv=[];ff=[]
 for o in objects:
  if o.get('floor')!=no or o.get('part_kind')!='stock_fixture':continue
  start=len(vv);vv.extend(o.matrix_world@v.co for v in o.data.vertices);ff.extend([i+start for i in f.vertices] for f in o.data.polygons)
 fixture_trees[no]=BVHTree.FromPolygons(vv,ff)
member_trees={}
for no in range(1,11):
 vv=[];ff=[]
 for o in objects:
  if o.get('floor')!=no or o.get('part_kind')!='merchandise_member':continue
  start=len(vv);vv.extend(o.matrix_world@v.co for v in o.data.vertices);ff.extend([i+start for i in f.vertices] for f in o.data.polygons)
 if vv:member_trees[no]=BVHTree.FromPolygons(vv,ff)
for q in stock['placements']:
 obs=actual.get(q['name'],[]);check(bool(obs),q['name']+' present')
 if not obs:continue
 pts=[o.matrix_world@v.co for o in obs for v in o.data.vertices];lo=Vector(tuple(min(v[k] for v in pts) for k in range(3)));hi=Vector(tuple(max(v[k] for v in pts) for k in range(3)));expected=products[q['product_id']]['size'];s=surfaces[q['support_id']];x,y,z=s['position_m'];w,d=s['size_m']
 check(all(o.get('product_id')==q['product_id'] and o.get('tenant_id')==q['tenant_id'] for o in obs),q['name']+' actual semantic ownership')
 check(all(o.get('asset_id')==q['asset'] for o in obs),q['name']+' actual selected hero/display source')
 check(all(abs((hi-lo)[k]-expected[k])<.012 for k in range(3)),q['name']+' metric product dimensions',actual_size=list(hi-lo),expected=expected)
 check(lo.x>=x-w/2-.012 and hi.x<=x+w/2+.012 and lo.y>=y-d/2-.012 and hi.y<=y+d/2+.012,q['name']+' supported footprint stays inside measured shelf')
 if s['mode']=='hanging':
  check(abs(hi.z-(z+.035))<.012 and lo.z>=(q['floor']-1)*4.2+.03,q['name']+' hook reaches hanging rail and garment clears floor')
  candidates=[v for v in pts if v.z>hi.z-.12]
  distance=min(math.sqrt(max(abs(v.y-y)-.018,0)**2+max(z-v.z,v.z-z-.035,0)**2) for v in candidates)
  check(distance<.06,q['name']+' actual hook geometry near rail',nearest_m=distance)
 else:
  check(abs(lo.z-(z+.0005))<.012,q['name']+' bottom rests at support elevation')
  lows=sorted(pts,key=lambda p:p.z)[:8];supported=False
  for p in lows:
   hit=fixture_trees[q['floor']].ray_cast(p+Vector((0,0,.015)),Vector((0,0,-1)),.045)[0]
   if hit is not None and abs(hit.z-z)<.012:supported=True;break
  check(supported,q['name']+' actual solid supporting mesh beneath product')
 # In each tenant, the open entrance strip is still free of merchandise.
 r=next(r for r in geometry['contract']['rooms'] if r['id']==q['tenant_id']);a,b,c,d=r['bounds'];positive=b>0
 check(lo.x>=a and hi.x<=c and lo.y>=b and hi.y<=d,q['name']+' stays inside correct tenant')
 check(lo.y>=b+3 if positive else hi.y<=d-3,q['name']+' does not block storefront entrance strip')
 if q['product_id']=='floor-lamp':
  tree=member_trees.get(q['floor']);xx,yy,zz=q['pos'];left=tree.ray_cast(Vector((xx-.1,yy,zz+.7)),Vector((1,0,0)),.2)[0] if tree else None;right=tree.ray_cast(Vector((xx+.1,yy,zz+.7)),Vector((-1,0,0)),.2)[0] if tree else None
  check(left is not None and right is not None and .026<=right.x-left.x<=.034,q['name']+' real continuous 30mm certified lamp post')
for rid,ids in data['tenants'].items():
 placed={q['product_id'] for q in stock['placements'] if q['tenant_id']==rid};check(set(ids)<=placed,rid+' every category-specific product is present',products=sorted(placed))
issues=[c for c in checks if not c['passed']];result=dict(status='failed' if issues else 'passed',checks=len(checks),issues=issues,model_sha256=sha(OUT/'M1-complete-2k.glb'),verifier_sha256=sha(Path(__file__)),unique_product_models=len(products),stocked_tenants=80,instances=len(stock['placements']),results=checks)
(STOCK/'assembly-acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8');print(result['status'],len(checks),'checks',len(issues),'issues',issues[:8],flush=True);assert not issues,issues[:8]
