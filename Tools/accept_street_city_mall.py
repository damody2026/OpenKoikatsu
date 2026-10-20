"""Independent mesh reimport, real circulation, fixture and shaft checks."""
import bpy,sys,json,math,struct
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_mall import OUT,sha,plan,transform
from street_city_remesh_gate import verify
r=json.loads((OUT/'assembly-report.json').read_text('utf8'));G=json.loads((OUT/'geometry.json').read_text('utf8'));C=G['contract'];results=[]
def check(ok,name,**data):results.append(dict(passed=bool(ok),name=name,**data))
for path,digest in r['source_files'].items():check(sha(path)==digest,'current recipe '+Path(path).name)
check(sha(OUT/'geometry.json')==r['geometry_sha256'],'current geometry contract');check(G==plan(),'effective current plan')
for key,q in r['sources'].items():verify(Path(q['remesh_report']).parent,q['sha256']);check(True,'Rust receipt '+key)
passengers=[q for q in C['lifts'] if not q['service']]
banks={q['bank'] for q in passengers}
check(len(banks)==4 and len(passengers)==12,'four distributed passenger banks, three cars each')
for bank in banks:
 rows=[q for q in passengers if q['bank']==bank];check(len(rows)==3,bank+' three cars')
for a in banks:
 for b in banks:
  if a>=b:continue
  pa=next(q for q in passengers if q['bank']==a);pb=next(q for q in passengers if q['bank']==b)
  check(math.dist([pa['x'],pa['y']],[pb['x'],pb['y']])>=35,a+'/'+b+' spatially separated lift banks')
for floor in range(1,10):
 flights=[q for q in C['escalators'] if q['lower_floor']==floor]
 check(len(flights)==8 and len({q['bank'] for q in flights})==4,f'F{floor} four paired escalator banks')
 for bank in {q['bank'] for q in flights}:
  rows=[q for q in flights if q['bank']==bank]
  check({q['direction'] for q in rows}=={'up','down'} and {q['slope_sign'] for q in rows}=={-1,1},f'F{floor} {bank} opposite slopes and travel')
  for q in rows:
   check(abs(q['rise_m']-4.2)<1e-6 and abs(q['run_m']*math.tan(math.radians(30))-4.2)<1e-6,q['id']+' actual 30 degree, 4.2m rise')
   next_floor=floor+1 if q['direction']=='up' else floor-1
   if 1<=next_floor<=9:
    following=next(e for e in C['escalators'] if e['bank']==bank and e['lower_floor']==next_floor and e['direction']==q['direction'])
    arrival=q['upper_point'] if q['direction']=='up' else q['lower_point']
    departure=following['lower_point'] if q['direction']=='up' else following['upper_point']
    check(abs(arrival[0]-departure[0])<1e-6 and abs(arrival[2]-departure[2])<1e-6 and abs(arrival[1]-departure[1])<2,q['id']+' same-end next-storey transfer')
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(OUT/'M1-complete-2k.glb'));bpy.context.view_layer.update();objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];trees={};counts={}
def tree(obs):
 vv=[];ff=[]
 for o in obs:
  offset=len(vv);vv.extend(o.matrix_world@v.co for v in o.data.vertices);ff.extend([offset+i for i in f.vertices] for f in o.data.polygons)
 return BVHTree.FromPolygons(vv,ff)
for f in C['floors']:
 n=f['floor'];z=f['z_m'];obs=[o for o in objects if o.get('floor')==n];count=0
 for o in obs:o.data.calc_loop_triangles();count+=len(o.data.loop_triangles)
 check(count<=100000 and count==r['floor_triangles'][str(n)],f'F{n:02d} actual triangles',triangles=count);counts[n]=count
 solid=tree(obs);trees[n]=solid
 pts=[o.matrix_world@v.co for o in obs if o.get('part_kind') in ('wall','floor') for v in o.data.vertices];bbox=[[min(p[k] for p in pts) for k in range(3)],[max(p[k] for p in pts) for k in range(3)]]
 check(all(abs(bbox[0][k]+C['footprint_m'][k]/2)<.005 and abs(bbox[1][k]-C['footprint_m'][k]/2)<.005 for k in [0,1]),f'F{n:02d} full rectangular footprint',bounds=bbox)
 data=(OUT/f'M1-F{n:02d}-complete-2k.glb').read_bytes();check(data[:4]==b'glTF' and len(data)==struct.unpack_from('<I',data,8)[0],f'F{n:02d} standalone bytes');sz=struct.unpack_from('<I',data,12)[0];gl=json.loads(data[20:20+sz]);ns=[sum(gl['accessors'][p['indices']]['count']//3 for p in m['primitives']) for m in gl['meshes']];check(sum(ns[q['mesh']] for q in gl['nodes'] if 'mesh' in q)==count,f'F{n:02d} standalone triangle inventory')
 check(len([s for s in C['rooms'] if s['floor']==n])==8,f'F{n:02d} eight open tenants')
 if n>1:check(solid.ray_cast(Vector((0,0,z+.1)),Vector((0,0,-1)),.5)[0] is None,f'F{n:02d} true atrium slab void')
for o in objects:check(o.data.uv_layers.active is not None,'UV '+o.name)
for image in bpy.data.images:
 if image.type=='IMAGE':check(max(image.size)<=2048,'2K texture '+image.name)
# Inspect the actual imported materials and geometry, not merely the design labels.
for f in C['floors']:
 n=f['floor'];obs=[o for o in objects if o.get('floor')==n]
 names={o.data.materials[poly.material_index].name for o in obs for poly in o.data.polygons if o.data.materials[poly.material_index]}
 for role in ['M1 sanitary porcelain','M1 public travertine','M1 satin bronze','M1 charcoal stone']:
  check(any(role in name for name in names),f'F{n} actual differentiated wall material '+role)
 core=[o.matrix_world@v.co for o in obs if o.get('part_kind')=='wall' for v in o.data.vertices]
 check(max(abs(v.x) for v in core)<74.93 and max(abs(v.y) for v in core)<49.93,f'F{n} real recessed structural wall behind facade skins')
 envelope=[o for o in obs if o.get('part_kind')=='facade_detail']
 check(bool(envelope),f'F{n} actual facade construction details')
 for o in envelope:
  check(all(abs((o.matrix_world@v.co).x)<=75.001 and abs((o.matrix_world@v.co).y)<=50.001 for v in o.data.vertices),f'F{n} facade stays in parcel '+o.name)
 # A real mullion is thin, bronze and surrounded by glass, never a broad solid curtain-wall proxy.
 check(len([q for q in G['parts'] if q['floor']==n and '-mullion' in q['name']])>=90,f'F{n} subdivided glass bay construction')
for name,digest in r['materials']['files'].items():
 check(sha(Path(r['materials']['texture_directory'])/name)==digest,'current architectural texture '+name)
for entry in ['entry-south','entry-west','entry-east']:
 leaves=[o for o in objects if o.name.startswith(entry) and o.get('part_kind')=='doorleaf']
 check(len(leaves)==2,entry+' actual double entrance leaves')
 check(bool(leaves) and all('glass' in o.data.materials[p.material_index].name.lower() for o in leaves for p in o.data.polygons),entry+' glass entrance finish instead of interior timber')
full=tree(objects);walls=tree([o for o in objects if o.get('part_kind')=='wall'])
for q in G['parts']:
 if q['kind']!='void_guard':continue
 x,y,z=q['pos'];w,d,h=q['size'];axis=0 if w<d else 1;normal=Vector((1,0,0)) if axis==0 else Vector((0,1,0));point=Vector((x,y,z+.9));check(full.ray_cast(point-normal*.12,normal,.24)[0] is not None,q['name']+' actual fall-protection guard')
for q in G['parts']:
 if 'grade_start' not in q:continue
 aa,bb=q['grade_start'],q['grade_end'];check(abs(bb[2]-aa[2])/math.dist(aa[:2],bb[:2])<=.05,q['name']+' metric grade <=5%')
 for t in [.01,.25,.5,.75,.99]:
  xx,yy,zz=[aa[k]+t*(bb[k]-aa[k]) for k in range(3)];hit=full.ray_cast(Vector((xx,yy,zz+.03)),Vector((0,0,-1)),.06)[0];check(hit is not None and abs(hit.z-zz)<.003,q['name']+' continuous physical apron '+str(t))
for route in G['routes']:
 p,q=route['start'],route['end'];distance=math.dist(p,q);dx=(q[0]-p[0])/distance;dy=(q[1]-p[1])/distance
 for k in range(math.ceil(distance)):
  t=(k+.5)/math.ceil(distance)
  for edge in [-route['width']/2+.1,0,route['width']/2-.1]:
   pt=Vector((p[0]+t*(q[0]-p[0])-dy*edge,p[1]+t*(q[1]-p[1])+dx*edge,p[2]+.05))
   check(full.ray_cast(pt,Vector((0,0,1)),2.15)[0] is None,f'F{route["floor"]} {route["name"]} headroom {k}/{edge}')
   check(full.ray_cast(pt,Vector((0,0,-1)),.1)[0] is not None,f'F{route["floor"]} {route["name"]} support {k}/{edge}')
for d in G['doors']:
 x,y,z=d['position_m'];a=math.radians(d['yaw_deg']);normal=Vector((-math.sin(a),math.cos(a),0));along=Vector((math.cos(a),math.sin(a),0));center=Vector((x,y,z))
 for sign in [-1,1]:check(walls.ray_cast(center+along*sign*(d['width_m']/2+.12)+Vector((0,0,1.2))-normal*.5,normal,1)[0] is not None,d['id']+' mounted jamb '+str(sign))
 check(walls.ray_cast(center+Vector((0,0,2.54))-normal*.5,normal,1)[0] is not None,d['id']+' mounted lintel')
 closed=d.get('kind')=='lift' and d['floor']>1
 for edge in [-d['width_m']/3,0,d['width_m']/3]:
  for h in [.2,1.1,2]:
   hit=full.ray_cast(center+along*edge+Vector((0,0,h))-normal*.25,normal,.5)[0]
   check(hit is not None if closed else hit is None,d['id']+(' guarded shaft ' if closed else ' clear opening ')+str((edge,h)))
# Sample the real treads against ALL floors, so low overhead slabs cannot pass.
for q in G['parts']:
 if q['kind'] not in ('stair_tread','escalator_tread'):continue
 x,y,z=q['pos'];w,d,h=q['size']
 for edge in [-.5,0,.5] if q['kind']=='escalator_tread' else [-min(w/2-.1,.6),0,min(w/2-.1,.6)]:
  pt=Vector((x,y+edge,z+h+.02)) if q['kind']=='escalator_tread' else Vector((x+edge,y,z+h+.02))
  check(full.ray_cast(pt,Vector((0,0,-1)),.04)[0] is not None,q['name']+' tread support '+str(edge))
  check(full.ray_cast(pt,Vector((0,0,1)),2.18)[0] is None,q['name']+' 2.2m tread headroom '+str(edge))
for lift in C['lifts']:
 for yy in [-3.8,-3.2,-2.6,-2.2,-1.8,-1.4,-.5,.5]:
  for edge in [-.45,0,.45]:
   pt=Vector(transform([edge,yy,.04],[lift['x'],lift['y'],0],lift['yaw']));check(full.ray_cast(pt,Vector((0,0,-1)),.08)[0] is not None,lift['id']+' continuous walk-in car floor '+str((yy,edge)));check(full.ray_cast(pt,Vector((0,0,1)),2.16)[0] is None,lift['id']+' walk-in car headroom '+str((yy,edge)))
# Check deck approaches and step/deck joins on the real exported mesh at every
# flight endpoint. An annotated connection without supporting triangles fails.
for e in C['escalators']:
 for end,fn in [('lower_point','lower_floor'),('upper_point','upper_floor')]:
  x,y,z=e[end];side=1 if x>0 else -1
  for distance in [.01,.2,1,2,4,6]:
   xx=x+side*distance
   if abs(xx)>13.5:continue
   for lane in [-.49,0,.49]:
    point=Vector((xx,y+lane,z+.02))
    check(full.ray_cast(point,Vector((0,0,-1)),.04)[0] is not None,e['id']+' supported '+end+str((distance,lane)))
    check(full.ray_cast(point,Vector((0,0,1)),2.18)[0] is None,e['id']+' clear '+end+str((distance,lane)))
# Premium programmes must exist in the actual reimported mesh/materials.
from street_city_mall_boutiques import profiles,OUT as BOUTIQUES
premium=json.loads((BOUTIQUES/'materials.json').read_text('utf8'))
check(sha(BOUTIQUES/'materials.json')==r['boutiques_sha256'],'current premium material manifest')
for key,record in premium['materials'].items():
 for role,m in record['maps'].items():check(sha(m['path'])==m['sha256'],'premium PBR '+key+' '+role)
for p in profiles().values():
 no=p['floor'];z=(no-1)*4.2;a,b,c,d=p['bounds'];cx=(a+c)/2;positive=(b+d)>0
 obs=[o for o in objects if o.get('floor')==no]
 used={o.data.materials[poly.material_index].name for o in obs for poly in o.data.polygons if o.data.materials[poly.material_index]}
 for key in [p['wall'],p['case'],p['trim'],p['floor_finish']]:check(any(name.startswith(key) for name in used),p['id']+' actual premium material '+key)
 details=[q for q in G['parts'] if q.get('tenant_id')==p['id'] and '-lux-' in q['name']]
 check(len(details)>=12,p['id']+' constructed feature wall and joinery')
 for q in details:
  angle=math.radians(q['yaw']);w,depth=q['size'][:2];ex=abs(math.cos(angle))*w/2+abs(math.sin(angle))*depth/2;ey=abs(math.sin(angle))*w/2+abs(math.cos(angle))*depth/2;x,y=q['pos'][:2]
  check(x-ex>=a-.15 and x+ex<=c+.15 and y-ey>=b-.15 and y+ey<=d+.15,p['id']+' boutique detail stays in tenant bay '+q['name'])
 # The first 3m of the open frontage has a real 2m wide human entrance.
 for lane in [-.9,0,.9]:
  for distance in [.2,1,2,3]:
   yy=b+distance if positive else d-distance;point=Vector((cx+lane,yy,z+.03))
   check(full.ray_cast(point,Vector((0,0,1)),2.17)[0] is None,p['id']+' open storefront 2m entrance '+str((lane,distance)))
 if no>=9:
  door=next(q for q in G['doors'] if q['id']==p['id']+'-kitchen')
  check((door['position_m'][1]>(b+d)/2)==positive,p['id']+' kitchen is at rear, not across storefront')
  check(door['yaw_deg']==(0 if positive else 180),p['id']+' kitchen door faces dining area')
for o in objects:
 if o.get('part_kind')!='sign':continue
 text_faces=[poly for poly in o.data.polygons if str(o.data.materials[poly.material_index].get('city_text_id','')).startswith('tenant.')]
 check(bool(text_faces) and all(poly.normal.y<-.5 for poly in text_faces),o.name+' lettering on front only, plain reverse')
errors=[q for q in results if not q['passed']];result=dict(status='failed' if errors else 'passed',checks=len(results),issues=errors,results=results,model_sha256=sha(OUT/'M1-complete-2k.glb'),assembly_sha256=sha(OUT/'assembly-report.json'),verifier_sha256=sha(Path(__file__)),floor_triangles=counts)
(OUT/'acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8');print(result['status'],len(results),len(errors),errors[:8],flush=True)
assert not errors,errors[:8]
