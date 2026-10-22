"""Reimport every delivered mesh, inspect real LOD geometry and certify panel provenance."""
import bpy,sys,math,json,hashlib
from pathlib import Path
from mathutils.bvhtree import BVHTree
from mathutils import Matrix,Vector
from mathutils.kdtree import KDTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import ROOT,BASE,OUT,read,write,sha,asset_definitions
from street_city_remesh_gate import verify
checks=[];issues=[]
def check(ok,name,**data):
 row=dict(passed=bool(ok),name=name,**data);checks.append(row)
 if not ok:issues.append(row)
def inspect(path):
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(path));bpy.context.view_layer.update();objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];vv=[];ff=[];count=0
 for o in objects:
  o.data.calc_loop_triangles();count+=len(o.data.loop_triangles);start=len(vv);vv.extend(o.matrix_world@v.co for v in o.data.vertices);ff.extend([start+j for j in t.vertices] for t in o.data.loop_triangles);check(o.data.uv_layers.active is not None,'actual metric/textured UV '+o.name)
 for im in bpy.data.images:
  if im.type=='IMAGE':check(max(im.size)<=2048,'actual 2K maximum '+im.name)
 return dict(triangles=count,vertices=[list(v) for v in vv],faces=ff,bounds=[[min(v[k] for v in vv),max(v[k] for v in vv)] for k in range(3)])
assets=[]
for q in read(OUT/'panel-assets.json')['assets']:
 for source in q['certified_sources']:verify(Path(source['remesh_report']).parent,source['sha256'])
 check(sha(q['path'])==q['sha256'],'panel actual file '+q['id']);g=inspect(q['path']);check(g['triangles']==q['triangles']<=q['triangle_cap'],'panel actual face budget '+q['id']);check(all(abs(sum(b))<.0001 for b in g['bounds'][:2]),'true centred footprint '+q['id'])
 assets.append(dict(id=q['id'],lods=[dict(level=0,path=q['path'],sha256=q['sha256'],triangles=g['triangles'],bounds_m=g['bounds'],certified_sources=q['certified_sources'])],construction='certified rectangular panel assembly'))
lodrows=[q for q in read(OUT/'lod-assets.json')['assets'] if q['id'] not in ('camphor','cherry')]+read(OUT/'foliage-assets.json')['assets'];scooter=verify(OUT/'assets/scooter/lod0/remesh');lodrows.append(dict(id='scooter',lods=[dict(level=0,**scooter,triangles=read(OUT/'assets/scooter/lod0/remesh/remesh-report.json')['triangles'],lod_source=str(OUT/'assets/scooter/lod0/lod-source.json'))]))
for q in lodrows:
 geometries=[];rows=[]
 for level in q['lods']:
  if level.get('certified_sources'):
   primitive_vertices={}
   for kind,source in level['primitive_sources'].items():
    verify(Path(source['remesh_report']).parent,source['sha256']);primitive_vertices[kind]=inspect(source['path'])['vertices']
   normalization=Matrix(level['normalization_matrix']);expected=[normalization@Matrix(part['matrix'])@Vector(v) for part in level['parts'] for v in primitive_vertices[part['primitive']]];g=inspect(level['path']);actual=[Vector(v) for v in g['vertices']]
   def index(points):
    tree=KDTree(len(points))
    for i,v in enumerate(points):tree.insert(v,i)
    tree.balance();return tree
   et,at=index(expected),index(actual);deviation=max(max(et.find(v)[2] for v in actual),max(at.find(v)[2] for v in expected));check(deviation<.00002,'actual transformed certified primitive vertices '+q['id']+str(level['level']),maximum_error_m=deviation)
   import struct
   data=Path(level['path']).read_bytes();document=json.loads(data[20:20+struct.unpack('<I',data[12:16])[0]]);check(any(m.get('alphaMode')=='MASK' and m.get('doubleSided') and abs(m.get('alphaCutoff',0)-.33)<.00001 for m in document['materials']),'real double-sided cutout leaf material '+q['id']+str(level['level']))
  else:verify(Path(level['remesh_report']).parent,level['sha256']);g=inspect(level['path'])
  check(g['triangles']==level['triangles'],'actual certified LOD face count '+q['id']+str(level['level']))
  row=dict(level,construction='Rust remeshed',bounds_m=g['bounds']);row['path']=str(Path(row['path']).resolve());rows.append(row);geometries.append(g)
  if level.get('lod_source'):
   source=read(level['lod_source']);check(sha(source['source'])==source['source_sha256'],'immutable LOD source '+q['id']+str(level['level']));check(source['measurement']['sampled_surface_error_m']<=source['maximum_source_deviation_m'],'documented LOD approximation '+q['id']+str(level['level']))
 if len(rows)>1:
  check(len(rows)==3 and rows[0]['triangles']>rows[1]['triangles']>rows[2]['triangles'],'three truly decreasing LOD meshes '+q['id'])
  # Height must remain stable so a lower tree LOD cannot visibly sink or grow.
  for i,g in enumerate(geometries[1:]):check(max(abs(g['bounds'][2][j]-geometries[0]['bounds'][2][j]) for j in range(2))<.025,'LOD height continuity '+q['id']+str(i+1))
 assets.append(dict(id=q['id'],lods=rows,construction='Tripo source, measured distance meshes and real Rust remesh'))
def geometry_by_name(path):
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(path));bpy.context.view_layer.update();result={}
 for o in bpy.context.scene.objects:
  if o.type!='MESH' or o.get('asset_id') in ('camphor','cherry'):continue
  o.data.calc_loop_triangles();triangles=sorted(tuple(sorted(tuple(round(float(x),5) for x in (o.matrix_world@o.data.vertices[j].co)) for j in p.vertices)) for p in o.data.loop_triangles);result[o.name]=hashlib.sha256(str(triangles).encode()).hexdigest()
 return result
compounds=read(OUT/'facility-sources.json')
for q in compounds['models']:
 check(sha(q['baseline_source'])==q['baseline_sha256'],'immutable facility baseline '+q['label']);base=geometry_by_name(q['baseline_source'])
 check(sha(q['path'])==q['sha256'],'actual separated facility source '+q['label']);check(geometry_by_name(q['path'])==base,'all facility nonvegetation geometry preserved '+q['label'])
report=dict(status='failed' if issues else 'passed',assets=assets,facility_sources=compounds['models'],checks=len(checks),issues=issues,results=checks,source_files={str(p):sha(p) for p in [OUT/'panel-assets.json',OUT/'lod-assets.json',OUT/'foliage-assets.json',OUT/'facility-sources.json',OUT/'legacy-trees.json']},verifier_sha256=sha(__file__),new_tripo_assets=[dict(id='scooter',job='8534602a-ebba-4f17-ab46-73c12ca3c3c0',texture='2K',mode='Model Multi-view',download_sha256=sha(OUT/'assets/scooter/downloaded-source.glb'))])
write(OUT/'asset-acceptance.json',report);assert not issues,issues[:10];print('STREET ASSETS ACCEPTED',len(assets),len(checks),flush=True)
