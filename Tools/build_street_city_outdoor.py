"""Generate real metric outdoor meshes, not concept proxies. Adoption requires Rust."""
import bpy,sys,math,json
import numpy as np
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_outdoor_contract import OUT,FACILITIES,R,S,publish
from street_city_remesh_gate import verify,sha
from street_city_rectangular_paths import union as path_union
publish();bpy.ops.wm.read_factory_settings(use_empty=True)
units={};unit_receipts={}
for name in ['unit-panel','unit-cylinder']:
 receipt=verify(OUT.parent/'components'/name/'remesh');unit_receipts[name]=receipt;bpy.ops.import_scene.gltf(filepath=receipt['path']);ob=next(o for o in bpy.context.scene.objects if o.type=='MESH');vv=[tuple(ob.matrix_world@v.co) for v in ob.data.vertices];lo=[min(v[k] for v in vv) for k in range(3)];hi=[max(v[k] for v in vv) for k in range(3)];units[name]=([( (v[0]-lo[0])/(hi[0]-lo[0])-.5,(v[1]-lo[1])/(hi[1]-lo[1])-.5,(v[2]-lo[2])/(hi[2]-lo[2])) for v in vv],[tuple(p.vertices) for p in ob.data.polygons]);bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
colors={'grass':(.18,.34,.10),'rubber':(.42,.105,.06),'rubber2':(.35,.075,.045),'white':(.9,.91,.86),'court':(.06,.24,.32),'tennis':(.12,.34,.20),'sand':(.59,.40,.21),'walk':(.48,.45,.38),'wood':(.34,.19,.075),'metal':(.08,.1,.105),'net':(.12,.14,.14),'blue':(.08,.28,.46),'pitch':(.12,.30,.09)}
mats={}
for name,c in colors.items():
 m=bpy.data.materials.new(name);m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=(*c,1);p.inputs['Roughness'].default_value=.8
 # Exportable PBR constants. Fine grain is supplementary Blender preview only.
 n=m.node_tree.nodes.new('ShaderNodeTexNoise');n.inputs['Scale'].default_value=80 if name in ('rubber','rubber2','walk') else 25;b=m.node_tree.nodes.new('ShaderNodeBump');b.inputs['Strength'].default_value=.13;b.inputs['Distance'].default_value=.004;m.node_tree.links.new(n.outputs['Fac'],b.inputs['Height']);m.node_tree.links.new(b.outputs['Normal'],p.inputs['Normal']);mats[name]=m
V=[];F=[];M=[];TV=[];TF=[];TM=[];features=[];paths=[]
def poly(v,f,mat,track=False):
 if track:
  i=len(TV);TV.extend(v);TF.extend(tuple(i+j for j in face) for face in f);TM.extend([list(mats).index(mat)]*len(f));return
 i=len(V);V.extend(v);F.extend(tuple(i+j for j in face) for face in f);M.extend([list(mats).index(mat)]*len(f))
def box(x,y,z,w,d,h,mat,tag=None):
 if tag in ['path','court_access','track_crossing']:paths.append((x,y,z,w,d,h))
 else:
  uv,uf=units['unit-panel'];poly([(x+a*w,y+b*d,z+c*h) for a,b,c in uv],uf,mat)
 if tag:features.append(dict(kind=tag,center=[x,y,z],size=[w,d,h]))
def pole(x,y,z,r,h,mat='metal',n=12):
 uv,uf=units['unit-cylinder'];poly([(x+a*2*r,y+b*2*r,z+c*h) for a,b,c in uv],uf,mat)
def bench(x,y,yaw=0):
 # Tables/benches face either park route, with leg and knee clearance.
 box(x,y,.43,2.0,.5,.07,'wood','bench');box(x,y+.22,.5,2,.07,.4,'wood')
 for dx in [-.75,.75]:box(x+dx,y,0,.09,.44,.43,'metal')
def pergola(x,y,w=10,d=8):
 segments=math.ceil(w/3);columns=[-w/2+.12+i*(w-.24)/segments for i in range(segments+1)]
 for dx in columns:
  for dy in [-d/2+.12,d/2-.12]:box(x+dx,y+dy,0,.24,.24,3,'wood')
 for dy in [-d/2,d/2]:
  for aa,bb in zip(columns,columns[1:]):box(x+(aa+bb)/2,y+dy,3,bb-aa,.2,.4,'wood')
 for i in range(int(w/.45)+1):box(x-w/2+i*.45,y,3.4,.12,d+.5,.45,'wood')
 features.append(dict(kind='pergola',center=[x,y,0],size=[w,d,3.85],maximum_column_spacing_m=(w-.24)/segments,roof_structure='segmented longitudinal beams / deep engineered-timber shade rafters'))
def picnic(x,y):
 box(x,y,.74,1.6,.9,.08,'wood','picnic_table')
 for dx in [-.55,.55]:box(x+dx,y,0,.12,.7,.74,'metal')
 for dy in [-.9,.9]:
  box(x,y+dy,.44,1.7,.35,.07,'wood')
  for dx in [-.6,.6]:box(x+dx,y+dy,0,.08,.3,.44,'metal')
def line(x,y,w,d,z=.025):box(x,y,z,w,d,.004,'white')
def oval(radius):
 # Counterclockwise: north semicircle, west straight, south semicircle, east straight.
 points=[(radius*math.cos(t),track_straight/2+radius*math.sin(t)) for t in [i*math.pi/256 for i in range(257)]]+[(radius*math.cos(t),-track_straight/2+radius*math.sin(t)) for t in [math.pi+i*math.pi/256 for i in range(257)]]
 return [(y,-x) for x,y in points] if track_axis=='X' else points
def ring(a,b,z,mat):
 pa,pb=oval(a),oval(b);n=len(pa);poly([(x,y,z) for x,y in pa+pb],[(i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n)],mat,True)
def arc(cx,cy,r,start=0,end=2*math.pi,count=128):
 angles=[start+(end-start)*i/count for i in range(count+1)];vv=[(cx+(r+dd)*math.cos(t),cy+(r+dd)*math.sin(t),.028) for dd in [-.025,.025] for t in angles];n=count+1;poly(vv,[(i,i+1,i+1+n,i+n) for i in range(count)],'white',True)
def ramp(x1,y1,x2,y2,z1,z2,width=3):
 dx,dy=x2-x1,y2-y1;length=math.hypot(dx,dy);nx,ny=-dy/length,dx/length;uv,uf=units['unit-panel'];poly([(x1+(a+.5)*dx+b*width*nx,y1+(a+.5)*dy+b*width*ny,z1+(a+.5)*(z2-z1)+(c-1)*.12) for a,b,c in uv],uf,'walk');features.append(dict(kind='grade_link',center=[(x1+x2)/2,(y1+y2)/2,(z1+z2)/2],size=[abs(dx) or width,abs(dy) or width,.12],start=[x1,y1,z1],end=[x2,y2,z2],width_m=width))
def court(cx,cy,kind,num):
 tennis=kind=='tennis';w,d=(10.97,23.77) if tennis else (9,18);ew,ed=(20,38) if tennis else (15,24)
 box(cx,cy,.008,ew,ed,.012,'court' if tennis else 'sand','court_safety_envelope');box(cx,cy,.020,w,d,.004,'tennis' if tennis else 'blue')
 for dx in [-w/2+.025,w/2-.025]:line(cx+dx,cy,.05,d)
 for dy in [-d/2+.025,d/2-.025]:line(cx,cy+dy,w,.05)
 if tennis:
  for dx in [-8.23/2+.025,8.23/2-.025]:line(cx+dx,cy,.05,d)
  for dy in [-6.4+.025,6.4-.025]:line(cx,cy+dy,8.23,.05)
  line(cx,cy,.05,12.8);netw=12.798;nh=1.07
 else:
  line(cx,cy,w,.05)
  for dy in [-3,3]:line(cx,cy+dy,w,.05)
  netw=11;nh=2.43 if num<2 else 2.24
 for dx in [-netw/2,netw/2]:pole(cx+dx,cy,.025,.045,nh)
 # Individual open mesh strands: visible/net collision preserves play clearance.
 for i in range(int(netw/.20)+1):
  xx=cx-netw/2+i*.20;top=nh-(.156*(1-abs(xx-cx)/(netw/2)) if tennis else 0);base=.025 if tennis else nh-1+.025;box(xx,cy,base,.012,.018,top+.025-base,'net')
 for i in range(6):box(cx,cy,(.03+i*.15 if tennis else nh-1+i*.2+.025),netw,.018,.012,'net')
 if tennis:
  # Fence has two actual 2.4m gates, no invisible collision panel across them.
  for sign in [-1,1]:
   xx=cx+sign*ew/2
   for ya,yb in [(-ed/2,-1.235),(1.235,ed/2)]:
    for yy in [ya+j*.4 for j in range(int((yb-ya)/.4)+1)]:box(xx,cy+yy,0,.018,.018,3,'net')
    for zz in [.5,1,1.5,2,2.5,3]:box(xx,cy+(ya+yb)/2,zz,.018,yb-ya,.018,'net')
   for yy in [-ed/2,-1.235,1.235,ed/2]:pole(xx,cy+yy,0,.035,3)
  for sign in [-1,1]:
   yy=cy+sign*ed/2
   for xx in [-ew/2+i*.4 for i in range(int(ew/.4)+1)]:box(cx+xx,yy,0,.018,.018,3,'net')
   for zz in [.5,1,1.5,2,2.5,3]:box(cx,yy,zz,ew,.018,.018,'net')
 features.append(dict(kind=kind,center=[cx,cy,0],playing_m=[w,d],safety_m=[ew,ed],net_height_m=nh,gate_width_m=2.4 if tennis else None))
rows=[]
for spec in FACILITIES:
 V.clear();F.clear();M.clear();TV.clear();TF.clear();TM.clear();features.clear();paths.clear();fid=spec['id'];w,d=spec['size'];track_radius=spec.get('curb_radius',R);track_straight=spec.get('straight',S);track_axis=spec.get('long_axis','Y')
 if fid=='annex-ground':
  xs=[-140,-45,105,140];ys=[-203,-188,188,203]
  for ia,(xa,xb) in enumerate(zip(xs,xs[1:])):
   for ib,(ya,yb) in enumerate(zip(ys,ys[1:])):
    top=11.70 if ia==1 and ib==1 else 12
    box((xa+xb)/2,(ya+yb)/2,0,xb-xa,yb-ya,top,'grass','extension_terrain')
 elif fid=='campus-track-access':
  ramp(-49.25,-5.5,-49.25,4,.05,0)
  box(-.25,4,-.12,98,3,.12,'walk','path')
  ramp(48.75,4,50.75,4,0,-.06)
 elif fid=='campus-links':
  box(-55,-44.25,0,9,3,.031,'walk','path');box(-80.5,17.25,0,3,57,.031,'walk','path');box(80.5,39.25,0,3,13,.031,'walk','path')
 elif fid=='connection':
  # 15m flat bridge approach, then 160m at 1:20; surface is a continuous solid ramp.
  for x1,x2,z1,z2 in [(0,15,0,0),(15,175,0,-8)]:
   uv,uf=units['unit-panel'];poly([(x1+(a+.5)*(x2-x1),b*4,z1+(a+.5)*(z2-z1)+(c-1)*.3) for a,b,c in uv],uf,'walk')
  # Handrail posts follow slope; wood beams exactly along slope.
  for xx in range(0,176,5):
   zz=-max(0,xx-15)/20
   for yy in [-1.92,1.92]:pole(xx,yy,zz,.035,1.1)
  for ya in [-1.92,1.92]:
   for x1,x2,z1,z2 in [(0,15,0,0),(15,175,0,-8)]:
    uv,uf=units['unit-panel'];poly([(x1+(a+.5)*(x2-x1),ya+b*.07,z1+(a+.5)*(z2-z1)+1.05+c*.07) for a,b,c in uv],uf,'metal')
  # Raised portions are supported on piers, not a floating 160m slab.
  with np.load(OUT.parent.parent/'world-ground-v1/world-field.npz') as archive:heights=archive['height_map']
  for xx in range(15,175,5):
   surface=-max(0,xx-15)/20;wx=1020+xx;ground=(float(heights[325,min(1100,wx)]) if wx<1100 else 12)-20;hh=surface-.3-ground
   if hh>.05:
    for yy in [-1.25,1.25]:pole(xx,yy,ground-.05,.18,hh+.05,'walk')
 else:
  depth=spec.get('foundation_depth_m',.24);box(0,0,-depth,w,d,depth,'grass','site_floor')
  if fid=='stadium':
   bounds=sorted({R+lane*1.22+delta for lane in range(9) for delta in [-.025,.025]})
   for aa,bb in zip(bounds,bounds[1:]):
    mid=(aa+bb)/2;white=min(abs(mid-(R+lane*1.22)) for lane in range(9))<.026;lane=int((mid-R)/1.22);ring(aa,bb,.012,'white' if white else ('rubber' if lane%2 else 'rubber2'))
   line(R+4.88,-S/2,9.76,.05)
   for i,p in enumerate(spec['tennis']):court(*p,'tennis',i)
   for i,p in enumerate(spec['volleyball']):court(*p,'volleyball',i)
   # Routes inside and outside the running lanes, one level crossing at west entrance.
   box(-69,0,.025,4,370,.025,'walk','path')
   box(-55,3,.025,40,4,.025,'walk','track_crossing');box(0,3,.025,70,4,.025,'walk','path')
   box(0,-5,.025,4,240,.025,'walk','path')
   for yy in [-87,-45]:box(0,yy,.025,28,2.4,.025,'walk','court_access')
   features.append(dict(kind='recreation_lawn',center=[0,48,0],size=[68,105,0]))
   pergola(0,139,28,8)
   for xx in [-11,-5,5,11]:picnic(xx,139)
   for yy in [-145,-130,120]:
    for xx in [-30,30]:bench(xx,yy)
   for xx in [-70,70]:
    for yy in [-150,-50,50,150]:pole(xx,yy,0,.12,12);box(xx,yy,12,1.6,.65,.18,'white','floodlight')
   for level in range(3):box(69+level*.8,0,level*.3,1.0,38,.3,'walk','spectator_step')
  elif fid=='campus-track':
   bounds=sorted({track_radius+lane*1.22+delta for lane in range(9) for delta in [-.025,.025]})
   for aa,bb in zip(bounds,bounds[1:]):
    mid=(aa+bb)/2;white=min(abs(mid-(track_radius+lane*1.22)) for lane in range(9))<.026;lane=int((mid-track_radius)/1.22);ring(aa,bb,.012,'white' if white else ('rubber' if lane%2 else 'rubber2'))
   # Raised kerb: outer edge is exactly R, the first-lane measurement datum.
   inner,outer=oval(track_radius-.05),oval(track_radius);n=len(inner);vv=[(x,y,z) for z in [0,.062] for x,y in inner+outer];ff=[]
   for i in range(n):
    j=(i+1)%n;ff.extend([(2*n+i,2*n+j,3*n+j,3*n+i),(i,n+i,n+j,j),(n+i,3*n+i,3*n+j,n+j),(i,j,2*n+j,2*n+i)])
   poly(vv,ff,'white',True);line(track_straight/2,track_radius+4.88,.05,9.76)
   box(0,0,.008,90,60,.016,'pitch','football_pitch')
   for yy in [-29.975,29.975]:line(0,yy,90,.05)
   for xx in [-44.975,44.975]:line(xx,0,.05,60)
   line(0,0,.05,60);arc(0,0,9.15)
   for sign in [-1,1]:
    for depth,pw in [(5.5,18.32),(16.5,40.32)]:
     xx=sign*(45-depth+.025);line(xx,0,.05,pw)
     for yy in [-pw/2+.025,pw/2-.025]:line(sign*(45-depth/2),yy,depth,.05)
    angle=math.acos(5.5/9.15)
    arc(sign*34,0,9.15,math.pi-angle if sign==1 else -angle,math.pi+angle if sign==1 else angle,64)
    box(sign*34,0,.025,.12,.12,.003,'white','penalty_mark')
    for yy in [-3.72,3.72]:box(sign*45,yy,.024,.12,.12,2.44,'white','goal_post')
    box(sign*45,0,2.464,.12,7.56,.12,'white','goal_crossbar')
    for yy in [-3.72,3.72]:pole(sign*47,yy,.024,.04,2.44)
    for yy in [-3.6+i*.2 for i in range(37)]:box(sign*47,yy,.024,.018,.018,2.44,'net')
    for zz in [.024+i*.2 for i in range(13)]:box(sign*47,0,zz,.018,7.32,.018,'net')
    for yy in [-30,30]:
     theta=(0 if yy<0 else -math.pi/2) if sign==-1 else (math.pi/2 if yy<0 else math.pi);arc(sign*45,yy,1,theta,theta+math.pi/2,16)
     pole(sign*45,yy,.024,.025,1.5,'white');box(sign*45,yy+.12,1.3,.02,.24,.2,'rubber')
   for yy in [-49.5,49.5]:box(0,yy,.025,190,3,.025,'walk','path')
   for xx in [-93.5,93.5]:box(xx,0,.025,3,96,.025,'walk','path')
   for xx in [-20,20]:bench(xx,34)
   for row in range(2):box(35,51.7+row*.7,row*.3,14,.7,.3,'walk','spectator_step')
   for xx in [-95.8,95.8]:
    for yy in [-51.5,51.5]:pole(xx,yy,0,.12,12);box(xx,yy,12,1.6,.65,.18,'white','floodlight')
   for xx in [(-96.75+i*193.5/65) for i in range(66)]:pole(xx,-52.75,0,.035,1.1)
   for zz in [.45,.95]:box(0,-52.75,zz,193.5,.06,.08,'metal','safety_rail')
   # The southwest terrace edge is raised above existing terrain. Keep the
   # north access open, while guarding the exposed western half as well.
   for yy in [-52.75+i*52.75/18 for i in range(19)]:pole(-96.75,yy,0,.035,1.1)
   for zz in [.45,.95]:box(-96.75,-26.375,zz,.06,52.75,.08,'metal','safety_rail')
   for xx in [-75,-74.2]:box(xx,51.8,0,.55,.55,.9,'metal','recycling')
   features.append(dict(kind='track',center=[0,0,0],size=[track_straight+2*(track_radius+9.76),2*(track_radius+9.76),.062],lap_m=400,curb_radius_m=track_radius,straight_m=track_straight,lane_width_m=1.22,lanes=8,axis='X'))
  else:
   box(0,0,.006,3,d,.025,'walk','path');box(0,0,.006,w,3,.025,'walk','path')
   for xx in [-w/2+3,w/2-3]:
    for yy in [-d/2+4,d/2-4]:box(xx,yy,.025,4,4,.45,'walk','planter');box(xx,yy,.475,3.7,3.7,.12,'grass')
   if fid=='reading-park':
    pergola(-9,15,12,10)
    for yy in [12,18]:picnic(-9,yy)
    for yy in [-24,-14,24]:bench(9,yy)
    # Chess/board game tables remain open-sided.
    for yy in [-21,-12]:picnic(-10,yy)
   else:
    box(12,4,.035,20,26,.035,'court','dance_plaza');box(9,23,.04,12,8,.36,'wood','stage')
    for step in range(3):box(15,-14-step*1.2,.03,20,1.2,.3*(step+1),'walk','amphitheatre')
    pergola(-23,17,12,10);picnic(-23,17)
    for yy in [-18,-10,-2]:
     for xx in [-28,-24]:pole(xx,yy,0,.045,2.3)
     box(-26,yy,2.3,4,.08,.08,'metal','exercise_bar')
    for yy in [-26,27]:bench(25,yy)
  # Recycling, notice and bicycle storage beside paths rather than across them.
  if fid!='campus-track':
   for xx in [-5,-4.2]:box(xx,-d/2+6,.02,.55,.55,.9,'metal','recycling')
   for xx in range(-12,-5):pole(xx,-d/2+9,.02,.045,.8)
 for piece in path_union(paths):box(*piece,'walk')
 mesh=bpy.data.meshes.new(fid);mesh.from_pydata(V,[],F);mesh.update();o=bpy.data.objects.new(fid,mesh);bpy.context.collection.objects.link(o)
 for m in mats.values():mesh.materials.append(m)
 uv=mesh.uv_layers.new(name='metric_uv')
 for face,mi in zip(mesh.polygons,M):
  face.material_index=mi
  for li in face.loop_indices:
   p=mesh.vertices[mesh.loops[li].vertex_index].co;uv.data[li].uv=(p.x/4,p.y/4) if abs(face.normal.z)>.5 else ((p.x+p.y)/4,p.z/4)
 o['outdoor_id']=fid;o['metric_origin']=spec['origin'];mesh.calc_loop_triangles();tri=len(mesh.loop_triangles);assert tri<=100000,(fid,tri)
 folder=OUT/fid;folder.mkdir(exist_ok=True);bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
 source=folder/'source.glb';bpy.ops.export_scene.gltf(filepath=str(source),export_format='GLB',use_selection=True,export_extras=True)
 track_source=None
 if TV:
  mm=bpy.data.meshes.new('metric-track');mm.from_pydata(TV,[],TF);mm.update();to=bpy.data.objects.new('metric-track',mm);bpy.context.collection.objects.link(to)
  for m in mats.values():mm.materials.append(m)
  for face,mi in zip(mm.polygons,TM):face.material_index=mi
  to['outdoor_id']=fid;bpy.ops.object.select_all(action='DESELECT');to.select_set(True);track_source=folder/'track-source.glb';bpy.ops.export_scene.gltf(filepath=str(track_source),export_format='GLB',use_selection=True,export_extras=True);bpy.data.objects.remove(to,do_unlink=True)
 rows.append(dict(**spec,source=str(source),triangles=tri,features=list(features),track_source=str(track_source) if track_source else None,primitive_sources=unit_receipts))
 bpy.data.objects.remove(o,do_unlink=True)
(OUT/'sources.json').write_text(json.dumps(dict(status='source_only_requires_remesh',facilities=rows),ensure_ascii=False,indent=2),'utf8')
