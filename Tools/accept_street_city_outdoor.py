"""Reimport remeshed outdoor assets; verify metric evidence on actual vertices/rays."""
import bpy,sys,json,math
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import intersect_point_tri_2d
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_outdoor_contract import ROOT,OUT,R,S
from street_city_remesh_gate import verify,sha
from street_city_outdoor_gate import verify_outdoor
from street_city_campus_contract import IDS,context
src=json.loads((OUT/'sources.json').read_text('utf8'));checks=[];assets=[]
def check(ok,name,**data):
 checks.append(dict(passed=bool(ok),name=name,**data))
for row in src['facilities']:
 fid=row['id'];r=verify_outdoor(fid);bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=r['path']);bpy.context.view_layer.update();objects=[o for o in bpy.context.scene.objects if o.type=='MESH'];points=[o.matrix_world@v.co for o in objects for v in o.data.vertices];tri=0
 for o in objects:o.data.calc_loop_triangles();tri+=len(o.data.loop_triangles)
 check(tri<=100000,fid+' triangle budget',triangles=tri)
 footprint_points=points
 if fid=='connection':
  footprint_points=[o.matrix_world@o.data.vertices[vi].co for o in objects for face in o.data.polygons if o.data.materials[face.material_index].name.split('.')[0]=='walk' for vi in face.vertices]
 lo=[min(v[k] for v in footprint_points) for k in range(3)];hi=[max(v[k] for v in footprint_points) for k in range(3)]
 expected=([0,175,-8,0] if fid=='connection' else [-row['size'][0]/2,row['size'][0]/2,-row['size'][1]/2,row['size'][1]/2])
 check(max(abs(lo[0]-expected[0]),abs(hi[0]-expected[1]),*([] if fid=='connection' else [abs(lo[1]-expected[2]),abs(hi[1]-expected[3])]))<.005,fid+' exact metric footprint',bounds=[lo,hi])
 trees=[BVHTree.FromPolygons([o.matrix_world@v.co for v in o.data.vertices],[tuple(p.vertices) for p in o.data.polygons]) for o in objects]
 def hits(x,y,z=5):
  return sorted([h[0].z for bt in trees if (h:=bt.ray_cast(Vector((x,y,z)),Vector((0,0,-1)),30))[0] is not None],reverse=True)
 def walking_layers(x,y,top):
  count=0
  for o in objects:
   for tri in o.data.loop_triangles:
    vv=[o.matrix_world@o.data.vertices[i].co for i in tri.vertices]
    if max(abs(p.z-top) for p in vv)>.002 or tri.normal.z<.9:continue
    if intersect_point_tri_2d(Vector((x,y)),*[Vector((p.x,p.y)) for p in vv]):count+=1
  return count
 samples=([(0.117,3.073),(0.117,-86.973),(0.117,-44.973),(-69.117,3.073)] if fid=='stadium' else ([(.117,.073)] if fid in ['reading-park','active-park'] else []))
 for x,y in samples:
  n=walking_layers(x,y,.05 if fid=='stadium' else .031);check(n==1,'one actual path surface at intersection '+str([x,y]),layers=n)
 for f in row['features']:
  if f['kind']=='pergola':check(f['maximum_column_spacing_m']<=3,'pergola supported spans '+str(f['center']),column_spacing_m=f['maximum_column_spacing_m'])
 if fid=='stadium':
  # Reconstruct the white inner lane boundary from actual horizontal mesh vertices.
  radii=[]
  for o in objects:
   for face in o.data.polygons:
    if o.data.materials[face.material_index].name.split('.')[0]!='white':continue
    for vi in face.vertices:
     p=o.matrix_world@o.data.vertices[vi].co
     if .010<=p.z<=.014 and p.y>S/2+10:
      rr=math.hypot(p.x,p.y-S/2)
      if abs(rr-R)<.04:radii.append(rr)
  check(bool(radii),'actual inner curb recovered from remeshed marking')
  radius=(min(radii)+max(radii))/2 if radii else 0;lap=2*S+2*math.pi*(radius+.3)
  check(abs(lap-800)<.05,'800m one lap from actual remeshed lane boundary',measured_m=lap,tolerance_m=.05)
  polyline=2*S+512*2*(radius+.3)*math.sin(math.pi/512);check(abs(polyline-800)<.05,'actual sampled curve lap',measured_m=polyline)
  courts=[q for q in row['features'] if q['kind'] in ('tennis','volleyball')]
  check(sum(q['kind']=='tennis' for q in courts)==4,'four tennis courts');check(sum(q['kind']=='volleyball' for q in courts)==4,'four volleyball courts')
  for i,q in enumerate(courts):
   x,y,_=q['center'];w,d=q['playing_m'];ew,ed=q['safety_m']
   check((ew-w)/2>=3 and (ed-d)/2>=3,'court safety margins '+str(i))
   material='tennis' if q['kind']=='tennis' else 'blue'
   actual=[o.matrix_world@o.data.vertices[vi].co for o in objects for face in o.data.polygons if o.data.materials[face.material_index].name.split('.')[0]==material for vi in face.vertices if abs((o.matrix_world@o.data.vertices[vi].co).x-x)<=ew/2 and abs((o.matrix_world@o.data.vertices[vi].co).y-y)<=ed/2]
   check(bool(actual) and abs(max(v.x for v in actual)-min(v.x for v in actual)-w)<.003 and abs(max(v.y for v in actual)-min(v.y for v in actual)-d)<.003,'actual playing rectangle '+str(i))
   netw=12.798 if q['kind']=='tennis' else 11
   h=hits(x+netw/2,y);check(bool(h) and abs(h[0]-.025-q['net_height_m'])<.003,'actual net post height '+str(i),hits=h[:1])
   if q['kind']=='tennis':
    h=hits(x,y);check(bool(h) and abs(h[0]-.025-.914)<.003,'actual tennis centre net height '+str(i),hits=h[:1])
   for dx in [-w/2+.2,w/2-.2]:
    for dy in [-d/2+.2,d/2-.2]:
     h=hits(x+dx,y+dy);check(bool(h) and abs(h[0]-.024)<.006,'court unobstructed surface '+str((i,dx,dy)))
   if q['kind']=='tennis':
    for dx in [-ew/2,ew/2]:
     for dy in [-1.19,-.9,0,.9,1.19]:
      check(not any(bt.ray_cast(Vector((x+dx-.25,y+dy,1)),Vector((1,0,0)),.5)[0] is not None for bt in trees),'actual 2.4m court gate '+str((i,dx,dy)))
 elif fid=='campus-track':
  radius=row['curb_radius'];straight=row['straight'];rr=[]
  for o in objects:
   for face in o.data.polygons:
    if o.data.materials[face.material_index].name.split('.')[0]!='white':continue
    for vi in face.vertices:
     p=o.matrix_world@o.data.vertices[vi].co
     if .010<=p.z<=.014 and p.x>straight/2+10:
      rrr=math.hypot(p.x-straight/2,p.y)
      if abs(rrr-radius)<.04:rr.append(rrr)
  actual=(min(rr)+max(rr))/2 if rr else 0;lap=2*straight+2*math.pi*(actual+.3);check(bool(rr) and abs(lap-400)<.05,'actual campus 400m lap',measured_m=lap)
  for xx in [-44.7,-20,0,20,44.7]:
   for yy in [-29.7,-15,7,15,29.7]:
    h=hits(xx,yy);check(bool(h) and abs(h[0]-.024)<.006,'campus football surface '+str((xx,yy)))
  pitch=[o.matrix_world@o.data.vertices[vi].co for o in objects for face in o.data.polygons if o.data.materials[face.material_index].name.split('.')[0]=='pitch' for vi in face.vertices]
  check(bool(pitch) and abs(max(v.x for v in pitch)-min(v.x for v in pitch)-90)<.003 and abs(max(v.y for v in pitch)-min(v.y for v in pitch)-60)<.003,'actual campus 90x60 pitch')
  for xx in [-48,48]:check(math.hypot(max(0,abs(xx)-straight/2),33)<radius,'3m football clearance remains inside track '+str(xx))
  for sign in [-1,1]:
   h=hits(sign*45,0,4);check(bool(h) and abs(h[0]-2.584)<.003,'goal crossbar actual height '+str(sign))
   for yy in [-3.65,0,3.65]:check(not any(bt.ray_cast(Vector((sign*45-.2,yy,2.45)),Vector((1,0,0)),.4)[0] is not None for bt in trees),'goal actual 7.32m by 2.44m opening '+str((sign,yy)))
  cx,cy,cz=row['origin'];ww,dd=row['size']
  for bid in IDS:
   b=context(bid)['building'];bx,by,_=b['world_position_m'];bw,bd=b['footprint_m'];check(abs(cx-bx)>=(ww+bw)/2 or abs(cy-by)>=(dd+bd)/2,'campus track avoids '+bid)
  check(200<=cx-ww/2 and cx+ww/2<=400 and 548<=cy-dd/2 and cy+dd/2<670,'campus track stays inside school southwest plot')
 elif fid=='campus-track-access':
  for f in row['features']:
   if f['kind']=='grade_link':
    p,q=f['start'],f['end'];length=math.dist(p[:2],q[:2]);check(abs(q[2]-p[2])/length<=.05,'campus access grade <=5%')
    for t in [.01,.25,.5,.75,.99]:
     xx,yy,zz=[p[k]+t*(q[k]-p[k]) for k in range(3)];h=hits(xx,yy);check(bool(h) and abs(h[0]-zz)<.003,'continuous campus access ramp '+str((p,t)))
   else:
    xx,yy,_=f['center'];h=hits(xx,yy);check(bool(h) and abs(h[0])<.003,'campus access flat route')
 elif fid=='connection':
  for x in range(0,176,5):
   sample=min(174.999,max(.001,x));h=hits(sample,0,4);z=-max(0,sample-15)/20
   check(bool(h) and abs(h[0]-z)<.003,'continuous 1:20 ramp '+str(x),expected_z=z,hits=h[:2])
 elif fid=='annex-ground':
  for x,y,expected in [(-130,0,12),(30,0,11.70),(125,0,12),(30,195,12)]:
   h=hits(x,y,15);check(bool(h) and abs(h[0]-expected)<.003,'actual annex terrain/foundation '+str([x,y]),height_m=h[:1])
 elif fid=='campus-links':
  for q in row['features']:
   x,y,_=q['center'];w,d,_=q['size'];h=hits(x,y);check(bool(h) and abs(h[0]-.031)<.004,'link walking surface '+str([x,y]))
   for bid in IDS:
    b=context(bid)['building'];bx,by,_=b['world_position_m'];bw,bd=b['footprint_m'];check(abs(x+row['origin'][0]-bx)>=(w+bw)/2 or abs(y+row['origin'][1]-by)>=(d+bd)/2,'link avoids '+bid)
 elif fid!='annex-ground':
  for y in range(-int(row['size'][1]/2)+2,int(row['size'][1]/2)-1,2):
   h=hits(0,y);check(bool(h) and abs(h[0]-.031)<.004,'park central route '+str(y))
  x,y,z=row['origin'];w,d=row['size']
  for bid in IDS:
   c=context(bid)['building'];bx,by,_=c['world_position_m'];bw,bd=c['footprint_m'];check(abs(x-bx)>=(w+bw)/2 or abs(y-by)>=(d+bd)/2,fid+' does not intersect '+bid)
 for im in bpy.data.images:
  if im.type=='IMAGE':check(max(im.size)<=2048,im.name+' <=2K')
 assets.append(dict(**row,remesh=r,actual_triangles=tri,bounds=[lo,hi]))
result=dict(status='passed' if all(c['passed'] for c in checks) else 'failed',assets=assets,checks=len(checks),results=checks,issues=[c for c in checks if not c['passed']],source_sha256=sha(Path(__file__)),builder_sha256=sha(ROOT/'Tools/build_street_city_outdoor.py'),contract_sha256=sha(OUT/'programme.json'))
(OUT/'acceptance.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf8');print(result['status'],len(checks),result['issues'][:12]);assert not result['issues']
