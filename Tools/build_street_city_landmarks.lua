-- Outdoor landmarks and facilities (Docs/STREET_CITY_WORLD_GAP_INVENTORY_20261029.md: Z1 lighthouse/seawall, Z2/Z9
-- playgrounds, Z3 newsstand/cafe sets/plaza fountain, Z5 clock tower/stage, Z6 gazebo). Planned for Tripo but never
-- submitted (world-tripo-v1/plan.json not_submitted_credits_exhausted), so Blender-authored low poly fitted to the
-- plan's size_m (x length, y depth, z height, metres; z=0 is the ground contact) and tris budget, same contract as
-- Tools/build_street_city_park_props.lua.
-- Output world-blender-v1/landmarks/<key>.glb + build.json (bounds, tris, sha256, probes; placement is a native job).
-- Usage: lua Tools/build_street_city_landmarks.lua
package.path='Tools/lua/?.lua;'..package.path
local j=require('ok.json');local out='C:/portable/OpenKoikatsu/output/street-social-city-production/world-blender-v1'
local code=[====[
import bpy,bmesh,json,hashlib,math
from pathlib import Path
from mathutils import Vector,Matrix
O=Path(TASK['out']);D=O/'landmarks';D.mkdir(parents=True,exist_ok=True)
plan={i['key']:i for i in json.loads(Path(TASK['plan']).read_text('utf-8-sig'))['items']}
def reset():bpy.ops.wm.read_factory_settings(use_empty=True)
def mat(name,rgb,rough=.6,metal=0.):
 m=bpy.data.materials.get(name) or bpy.data.materials.new(name);m.use_nodes=True;b=m.node_tree.nodes['Principled BSDF']
 b.inputs['Base Color'].default_value=(*rgb,1);b.inputs['Roughness'].default_value=rough;b.inputs['Metallic'].default_value=metal;return m
def tag(res,mi):
 for f in {f for v in res['verts'] for f in v.link_faces}:f.material_index=mi
# spin: degrees about the cylinder axis (a 4-segment cone spun 45 is a square pyramid)
def cyl(bm,p0,p1,r,seg=8,mi=0,r2=None,spin=0):
 p0,p1=Vector(p0),Vector(p1);ax=p1-p0;rot=ax.to_track_quat('Z','Y').to_matrix().to_4x4()@Matrix.Rotation(math.radians(spin),4,'Z')
 tag(bmesh.ops.create_cone(bm,cap_ends=True,segments=seg,radius1=r,radius2=r if r2 is None else r2,depth=ax.length,matrix=Matrix.Translation((p0+p1)/2)@rot),mi)
def box(bm,c,s,mi=0,rot=None):
 tag(bmesh.ops.create_cube(bm,size=1,matrix=Matrix.Translation(c)@(rot or Matrix.Identity(4))@Matrix.Diagonal((*s,1))),mi)
RZ=lambda a:Matrix.Rotation(math.radians(a),4,'Z');RX=lambda a:Matrix.Rotation(math.radians(a),4,'X')
def build(key,fn,mats):
 reset();mats=mats();bm=bmesh.new();fn(bm);zmin=min(v.co.z for v in bm.verts);bmesh.ops.translate(bm,verts=bm.verts,vec=(0,0,-zmin))
 bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
 me=bpy.data.meshes.new(key);bm.to_mesh(me);bm.free()
 for m in mats:me.materials.append(m)
 o=bpy.data.objects.new(key,me);bpy.context.collection.objects.link(o)
 for p in me.polygons:p.use_smooth=False
 o.select_set(True);glb=D/(key+'.glb');bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',use_selection=True,export_apply=True,export_yup=True)
 xyz=[v.co for v in me.vertices];lo=[min(c[k] for c in xyz) for k in range(3)];hi=[max(c[k] for c in xyz) for k in range(3)]
 tris=sum(len(p.vertices)-2 for p in me.polygons);want=plan[key]['size_m'];size=[round(hi[k]-lo[k],3) for k in range(3)]
 ok=all(abs(size[k]-want[k])<=.15*want[k] for k in range(3)) and abs(lo[2])<.005 and tris<=plan[key]['tris']
 probes=[[round(c,4) for c in max(xyz,key=lambda v:v.dot(Vector(d)))] for d in ((1,2.3,3.1),(-2.2,1,2.9),(1.7,-1.3,.6),(-1,-2.1,-1.7))]
 return dict(probes_m=probes,path=str(glb),sha256=hashlib.sha256(glb.read_bytes()).hexdigest(),triangles=tris,tris_budget=plan[key]['tris'],size_m=size,plan_size_m=want,bounds_m=[lo,hi],fit=ok,zones=plan[key].get('zone'),instances=plan[key].get('instances'))
STEEL=lambda:mat('Steel_galv',(.70,.72,.73),.35,.85);CONC=lambda:mat('Concrete',(.68,.66,.62),.9);WOOD=lambda:mat('Wood_teak',(.50,.33,.19),.75)
WATER=lambda:mat('Water_fountain',(.30,.52,.62),.05)
built={}
def newsstand(bm):
 # kiosk body 2.2 x 1.4 with a service window, overhanging roof 2.5 x 1.8, sign band, three slanted magazine racks
 box(bm,(0,.2,1.15),(2.2,1.4,2.3),0);box(bm,(0,-.505,1.35),(1.4,.01,.8),1);box(bm,(0,-.58,.95),(1.6,.18,.05),0)
 box(bm,(0,0,2.45),(2.5,1.8,.1),2);box(bm,(0,-.2,2.55),(2.0,.1,.1),3)
 for i,x in enumerate((-.75,0,.75)):
  box(bm,(x,-.70,.55),(.6,.04,.75),2,RX(22))
  for k in range(3):box(bm,(x-.18+k*.18,-.725,.40+k*.12),(.15,.01,.2),4+(i+k)%3,RX(22))
built['newsstand']=build('newsstand',newsstand,lambda:[mat('Kiosk_green',(.12,.36,.26),.55,.2),mat('Glass_dark',(.18,.22,.25),.08),mat('Trim_cream',(.92,.89,.80),.6),mat('Sign_red',(.75,.15,.12),.6),mat('Mag_a',(.90,.60,.20),.8),mat('Mag_b',(.25,.45,.80),.8),mat('Mag_c',(.85,.35,.55),.8)])
def chair(bm,x,y,face):
 r=RZ(face);m=lambda v:Vector((x,y,0))+r.to_3x3()@Vector(v)
 box(bm,m((0,0,.45)),(.42,.42,.04),1,r);box(bm,m((0,.19,.70)),(.42,.04,.46),1,r)
 for a,b in ((-.18,-.18),(.18,-.18),(-.18,.18),(.18,.18)):cyl(bm,m((a,b,0)),m((a,b,.44)),.018,6,0)
def cafe_a(bm):
 # round table, octagonal market umbrella (2.2 m across), four chairs facing the table
 cyl(bm,(0,0,0),(0,0,.04),.25,10,0);cyl(bm,(0,0,.04),(0,0,.72),.03,8,0);cyl(bm,(0,0,.72),(0,0,.75),.40,14,1)
 cyl(bm,(0,0,.75),(0,0,2.30),.022,6,0);cyl(bm,(0,0,1.95),(0,0,2.35),1.1,8,2,.03);cyl(bm,(0,0,2.35),(0,0,2.4),.04,6,0)
 for k in range(4):a=math.radians(45+90*k);chair(bm,.68*math.cos(a),.68*math.sin(a),90*k+135)
built['cafe_set_a']=build('cafe_set_a',cafe_a,lambda:[mat('Steel_black',(.08,.08,.08),.5,.6),mat('Rattan',(.62,.46,.28),.85),mat('Canvas_cream',(.94,.90,.80),.9)])
def cafe_b(bm):
 # square table, square striped umbrella (2.6 m side), four chairs on the table sides
 box(bm,(0,0,.735),(.80,.80,.04),1);box(bm,(0,0,.02),(.5,.5,.04),0);cyl(bm,(0,0,.04),(0,0,.72),.03,8,0)
 cyl(bm,(0,0,.75),(0,0,2.30),.022,6,0);cyl(bm,(0,0,1.95),(0,0,2.35),1.84,4,2,.03,45);cyl(bm,(0,0,1.949),(0,0,2.0),1.80,4,3,1.36,45)
 cyl(bm,(0,0,2.35),(0,0,2.4),.04,6,0)
 for k in range(4):a=math.radians(90*k);chair(bm,.72*math.cos(a),.72*math.sin(a),90*k+90)
built['cafe_set_b']=build('cafe_set_b',cafe_b,lambda:[mat('Steel_black',(.08,.08,.08),.5,.6),mat('Wood_light',(.70,.52,.32),.7),mat('Canvas_green',(.15,.45,.32),.9),mat('Canvas_white',(.95,.95,.92),.9)])
def plaza_fountain(bm):
 # octagonal stone basin (4 m), water surface, pedestal with two tiers of bowls and a centre jet
 cyl(bm,(0,0,0),(0,0,.55),2.0,8,0,spin=22.5);cyl(bm,(0,0,.3),(0,0,.45),1.82,8,1,spin=22.5)
 cyl(bm,(0,0,.45),(0,0,1.55),.28,12,0);cyl(bm,(0,0,1.45),(0,0,1.75),.35,16,0,.95);cyl(bm,(0,0,1.70),(0,0,1.74),.88,16,1)
 cyl(bm,(0,0,1.75),(0,0,2.35),.16,10,0);cyl(bm,(0,0,2.3),(0,0,2.5),.2,12,0,.5);cyl(bm,(0,0,2.46),(0,0,2.49),.46,12,1)
 cyl(bm,(0,0,2.5),(0,0,3.0),.06,8,1,.015)
built['plaza_fountain']=build('plaza_fountain',plaza_fountain,lambda:[mat('Stone_light',(.80,.77,.70),.8),WATER()])
def gazebo(bm):
 # octagonal deck 4 m, eight posts, railing on seven sides (open entrance), octagonal roof, finial
 cyl(bm,(0,0,0),(0,0,.25),2.0,8,0,spin=22.5);P=[(1.75*math.cos(math.radians(45*k)),1.75*math.sin(math.radians(45*k))) for k in range(8)]
 for x,y in P:cyl(bm,(x,y,.25),(x,y,2.6),.08,6,1)
 for k in range(8):
  (x0,y0),(x1,y1)=P[k],P[(k+1)%8];cyl(bm,(x0,y0,2.55),(x1,y1,2.55),.06,4,1)
  if k:cyl(bm,(x0,y0,.95),(x1,y1,.95),.04,4,1);cyl(bm,(x0,y0,.45),(x1,y1,.45),.03,4,1)
 cyl(bm,(0,0,2.6),(0,0,3.8),2.0,8,2,.12,spin=22.5);cyl(bm,(0,0,3.8),(0,0,4.0),.06,6,1,.01)
built['gazebo']=build('gazebo',gazebo,lambda:[mat('Stone_deck',(.72,.70,.66),.9),mat('Wood_white',(.92,.91,.88),.6),mat('Roof_slate',(.25,.30,.33),.7)])
def playground_a(bm):
 # tower (1.5 m square, deck at 1.5, pyramid roof to 3.0), ladder to -x, slide to +x, sandbox frame, spring rider
 for x in (-2.25,-.75):
  for y in (-.75,.75):cyl(bm,(x,y,0),(x,y,2.3),.05,6,0)
 box(bm,(-1.5,0,1.5),(1.5,1.5,.08),1);cyl(bm,(-1.5,0,2.3),(-1.5,0,3.0),1.13,4,2,.02,45)
 for y in (-.75,.75):box(bm,(-1.5,y,1.85),(1.5,.04,.6),3)
 for y in (-.3,.3):cyl(bm,(-3.0,y,0),(-2.25,y,1.5),.03,6,0)
 for k in range(5):t=(k+1)/6;cyl(bm,(-3.0+.75*t,-.3,1.5*t),(-3.0+.75*t,.3,1.5*t),.02,6,0)
 L=math.hypot(2.25,1.4);a=math.degrees(math.atan2(1.4,2.25))
 box(bm,(.375,0,.8),(L,.55,.04),3,Matrix.Rotation(math.radians(a),4,'Y'));box(bm,(2.75,0,.12),(.5,.55,.04),3)
 for y in (-.3,.3):box(bm,(.375,y,.9),(L,.04,.18),3,Matrix.Rotation(math.radians(a),4,'Y'))
 for (cx,cy,sx,sy) in ((1.0,.95,2.0,.08),(1.0,2.0,2.0,.08),(0.0,1.475,.08,1.05),(2.0,1.475,.08,1.05)):box(bm,(cx,cy,.12),(sx,sy,.24),1)
 box(bm,(1.0,1.475,.03),(1.9,.95,.06),4)
 cyl(bm,(1.0,-1.8,0),(1.0,-1.8,.35),.1,8,0,.03);box(bm,(1.0,-1.8,.5),(.6,.25,.3),2)
built['playground_a']=build('playground_a',playground_a,lambda:[mat('Steel_blue',(.15,.35,.70),.45,.5),mat('Wood_deck',(.55,.40,.25),.8),mat('Plastic_red',(.85,.20,.15),.5),mat('Plastic_yellow',(.95,.78,.15),.5),mat('Sand',(.86,.78,.60),.95)])
def playground_b(bm):
 # swing set: two A-frames (3 m spread), top beam 5 m, two seats on chains
 for x in (-2.4,2.4):
  for y in (-1.45,1.45):cyl(bm,(x,y,0),(x,0,2.4),.05,8,0)
 cyl(bm,(-2.5,0,2.42),(2.5,0,2.42),.07,8,0)
 for x in (-1.0,1.0):
  for dx in (-.22,.22):cyl(bm,(x+dx,0,2.38),(x+dx,0,.48),.01,4,1)
  box(bm,(x,0,.46),(.5,.2,.04),2)
built['playground_b']=build('playground_b',playground_b,lambda:[mat('Steel_green',(.12,.45,.25),.45,.5),STEEL(),mat('Rubber_black',(.06,.06,.06),.8)])
def seawall(bm):
 # stepped concrete revetment (sea side -y), coping, drainage slots
 box(bm,(0,0,.25),(4.0,2.0,.5),0);box(bm,(0,.35,.75),(4.0,1.3,.5),0);box(bm,(0,.7,1.25),(4.0,.6,.5),0)
 box(bm,(0,.7,1.47),(4.0,.66,.06),1)
 for x in (-1.2,0,1.2):box(bm,(x,-.351,.75),(.15,.01,.12),2)
built['seawall']=build('seawall',seawall,lambda:[CONC(),mat('Coping_light',(.82,.80,.76),.85),mat('Drain_dark',(.12,.12,.12),.9)])
def lighthouse(bm):
 # round base (4 m), tapered tower in five red/white bands, gallery, lantern glass, dome, finial
 cyl(bm,(0,0,0),(0,0,.6),2.0,12,0);n=5
 for k in range(n):
  z0=.6+k*10.4/n;z1=z0+10.4/n;r0=1.4-.5*k/n;r1=1.4-.5*(k+1)/n;cyl(bm,(0,0,z0),(0,0,z1),r0,12,1+k%2,r1)
 cyl(bm,(0,0,11.0),(0,0,11.2),1.3,12,3);cyl(bm,(0,0,11.2),(0,0,12.6),.7,10,4);cyl(bm,(0,0,12.6),(0,0,13.6),.85,10,1,.1)
 cyl(bm,(0,0,13.6),(0,0,14.0),.04,6,3)
built['lighthouse']=build('lighthouse',lighthouse,lambda:[CONC(),mat('Paint_red',(.75,.12,.10),.6),mat('Paint_white',(.94,.94,.92),.6),mat('Iron_black',(.08,.08,.08),.5,.6),mat('Lantern_glass',(.95,.92,.70),.05)])
def clock_tower(bm):
 # stone plinth 3 m, brick shaft, clock housing with four faces and hands, pyramid roof, finial
 box(bm,(0,0,.5),(3.0,3.0,1.0),0);box(bm,(0,0,5.0),(2.2,2.2,8.0),1);box(bm,(0,0,9.8),(2.6,2.6,1.6),0)
 for k in range(4):
  r=RZ(90*k);m=lambda v:r.to_3x3()@Vector(v)
  cyl(bm,m((0,-1.30,9.8)),m((0,-1.34,9.8)),.9,16,2);box(bm,m((0,-1.35,10.1)),(.06,.01,.55) if k%2==0 else (.01,.06,.55),3,r);box(bm,m((.2,-1.35,9.8)),(.4,.01,.05) if k%2==0 else (.01,.4,.05),3,r)
 cyl(bm,(0,0,10.6),(0,0,11.8),1.84,4,4,.02,45);cyl(bm,(0,0,11.8),(0,0,12.0),.05,6,3,.01)
built['clock_tower']=build('clock_tower',clock_tower,lambda:[mat('Stone_light',(.80,.77,.70),.8),mat('Brick_red',(.62,.30,.22),.85),mat('Clock_face',(.95,.94,.88),.5),mat('Iron_black',(.08,.08,.08),.5,.6),mat('Roof_copper',(.30,.55,.48),.6,.4)])
def stage(bm):
 # deck 8 x 4.4 at 1.0 m, front steps, back wall, two front columns, roof 8 x 6 at 5 m, light truss
 box(bm,(0,-.2,.5),(8.0,4.4,1.0),0);box(bm,(0,-2.42,.95),(8.0,.04,.1),3)
 for k in range(3):box(bm,(0,-2.55-k*.15,.85-k*.33),(2.0,.3,.33),0)
 box(bm,(0,2.9,3.0),(8.0,.2,4.0),1)
 for x in (-3.85,3.85):box(bm,(x,-2.85,2.45),(.25,.25,4.9),2)
 box(bm,(0,0,4.95),(8.0,6.0,.1),2);cyl(bm,(-3.6,-2.2,4.7),(3.6,-2.2,4.7),.05,6,2)
 for x in (-2.4,-.8,.8,2.4):cyl(bm,(x,-2.2,4.65),(x,-2.4,4.35),.12,8,3,.16)
built['stage']=build('stage',stage,lambda:[mat('Stage_wood',(.45,.30,.18),.7),mat('Backdrop_navy',(.10,.14,.30),.9),mat('Steel_black',(.08,.08,.08),.5,.6),mat('Lamp_dark',(.15,.15,.15),.4,.7)])
bad=[k for k,b in built.items() if not b['fit']]
(D/'build.json').write_text(json.dumps(dict(status='authored_requires_native_install',plan=TASK['plan'],models=built,misfit=bad),indent=1),'utf8')
print('LANDMARKS',{k:(b['triangles'],b['size_m'],b['fit']) for k,b in built.items()},'misfit',bad,flush=True)
]====]
local path=out..'/landmarks-payload.json'
os.execute('mkdir "'..out:gsub('/','\\')..'" 2>nul')
local f=assert(io.open(path,'wb'));f:write(j.encode({out=out,code=code,plan='C:/portable/OpenKoikatsu/output/street-social-city-production/world-tripo-v1/plan.json'}));f:close()
local expr="import json;TASK=json.load(open('"..path.."',encoding='utf8'));exec(TASK['code'])"
assert(os.execute('""C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --python-expr "'..expr..'" > "'..out..'/landmarks-build.log" 2>&1"'))
print('Landmarks authored; see world-blender-v1/landmarks-build.log')
