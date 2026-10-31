-- Outdoor small furniture (Docs/STREET_CITY_WORLD_GAP_INVENTORY_20261029.md section 3: Z1/Z2/Z3/Z5/Z8 lack picnic
-- tables, wayfinding, notice boards, drinking fountains, lifebuoys, beach showers, A-frame signs, banner poles, hurdles,
-- bike shelters). These keys were planned for Tripo but never submitted (world-tripo-v1/plan.json status
-- not_submitted_credits_exhausted), so they are Blender-authored low poly here, each fitted to the plan's size_m
-- (x length, y depth, z height, metres; z=0 is the ground contact) and its tris budget.
-- Output world-blender-v1/park-props/<key>.glb + build.json (bounds, tris, sha256; placement is a separate native job).
-- Usage: lua Tools/build_street_city_park_props.lua
package.path='Tools/lua/?.lua;'..package.path
local j=require('ok.json');local out='C:/portable/OpenKoikatsu/output/street-social-city-production/world-blender-v1'
local code=[====[
import bpy,bmesh,json,hashlib,math,re
from pathlib import Path
from mathutils import Vector,Matrix
O=Path(TASK['out']);D=O/'park-props';D.mkdir(parents=True,exist_ok=True)
plan={i['key']:i for i in json.loads(Path(TASK['plan']).read_text('utf-8-sig'))['items']}
def reset():bpy.ops.wm.read_factory_settings(use_empty=True)
def mat(name,rgb,rough=.6,metal=0.):
 m=bpy.data.materials.get(name) or bpy.data.materials.new(name);m.use_nodes=True;b=m.node_tree.nodes['Principled BSDF']
 b.inputs['Base Color'].default_value=(*rgb,1);b.inputs['Roughness'].default_value=rough;b.inputs['Metallic'].default_value=metal;return m
def tag(res,mi):
 for f in {f for v in res['verts'] for f in v.link_faces}:f.material_index=mi
def cyl(bm,p0,p1,r,seg=8,mi=0,r2=None):
 p0,p1=Vector(p0),Vector(p1);ax=p1-p0;rot=ax.to_track_quat('Z','Y').to_matrix().to_4x4()
 tag(bmesh.ops.create_cone(bm,cap_ends=True,segments=seg,radius1=r,radius2=r if r2 is None else r2,depth=ax.length,matrix=Matrix.Translation((p0+p1)/2)@rot),mi)
def box(bm,c,s,mi=0,rot=None):
 tag(bmesh.ops.create_cube(bm,size=1,matrix=Matrix.Translation(c)@(rot or Matrix.Identity(4))@Matrix.Diagonal((*s,1))),mi)
def torus(bm,c,R,r,seg=16,ring=6,mi=0,axis='Y'):
 vs=[]
 for i in range(seg):
  a=2*math.pi*i/seg;row=[]
  for k in range(ring):
   b=2*math.pi*k/ring;rr=R+r*math.cos(b);p=Vector((rr*math.cos(a),r*math.sin(b),rr*math.sin(a))) if axis=='Y' else Vector((rr*math.cos(a),rr*math.sin(a),r*math.sin(b)))
   row.append(bm.verts.new(Vector(c)+p))
  vs.append(row)
 for i in range(seg):
  for k in range(ring):
   f=bm.faces.new((vs[i][k],vs[(i+1)%seg][k],vs[(i+1)%seg][(k+1)%ring],vs[i][(k+1)%ring]));f.material_index=mi
def build(key,fn,mats):
 reset();mats=mats();bm=bmesh.new();fn(bm);zmin=min(v.co.z for v in bm.verts);bmesh.ops.translate(bm,verts=bm.verts,vec=(0,0,-zmin))  # slanted legs / leaning boards poke below 0: sit the lowest point on the ground
 bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
 me=bpy.data.meshes.new(key);bm.to_mesh(me);bm.free()
 for m in mats:me.materials.append(m)
 o=bpy.data.objects.new(key,me);bpy.context.collection.objects.link(o)
 for p in me.polygons:p.use_smooth=False
 o.select_set(True);glb=D/(key+'.glb');bpy.ops.export_scene.gltf(filepath=str(glb),export_format='GLB',use_selection=True,export_apply=True,export_yup=True)
 xyz=[v.co for v in me.vertices];lo=[min(c[k] for c in xyz) for k in range(3)];hi=[max(c[k] for c in xyz) for k in range(3)]
 tris=sum(len(p.vertices)-2 for p in me.polygons);want=plan[key]['size_m'];size=[round(hi[k]-lo[k],3) for k in range(3)]
 # fit check: every axis within 15 % of the planned size, grounded at z=0, inside the tris budget
 ok=all(abs(size[k]-want[k])<=.15*want[k] for k in range(3)) and abs(lo[2])<.005 and tris<=plan[key]['tris']
 # asymmetric probe vertices (extremes of skewed directions) for the native install's axis-mapping match
 probes=[[round(c,4) for c in max(xyz,key=lambda v:v.dot(Vector(d)))] for d in ((1,2.3,3.1),(-2.2,1,2.9),(1.7,-1.3,.6),(-1,-2.1,-1.7))]
 return dict(probes_m=probes,path=str(glb),sha256=hashlib.sha256(glb.read_bytes()).hexdigest(),triangles=tris,tris_budget=plan[key]['tris'],size_m=size,plan_size_m=want,bounds_m=[lo,hi],fit=ok,zones=plan[key].get('zone'),instances=plan[key].get('instances'))
WOOD=lambda:mat('Wood_teak',(.50,.33,.19),.75);STEEL=lambda:mat('Steel_galv',(.70,.72,.73),.35,.85);DARK=lambda:mat('Steel_green',(.10,.24,.18),.45,.6)
built={}
def picnic(bm):
 # 1.8 x 1.6: table top 1.8 x .8 at .75, benches 1.8 x .3 at .45 on both sides, A-frame legs at both ends
 for i in range(4):box(bm,(0,-.3+i*.2,.735),(1.8,.18,.04),0)
 for s in (-1,1):
  for i in range(2):box(bm,(0,s*(.55+i*.13),.435),(1.8,.12,.04),0)
  for x in (-.7,.7):cyl(bm,(x,s*.78,0),(x,s*.08,.715),.035,6,1);box(bm,(x,s*.62,.40),(.06,.36,.05),1)
 for x in (-.7,.7):box(bm,(x,0,.40),(.06,1.60,.05),1)
built['picnic_table']=build('picnic_table',picnic,lambda:[WOOD(),mat('Wood_frame',(.36,.23,.13),.8)])
def campus_map(bm):
 # two posts, a framed board 1.6 x 1.2 (map panel with coloured block districts, no text), pitched roof
 for x in (-.85,.85):box(bm,(x,0,1.05),(.1,.1,2.1),0)
 box(bm,(0,0,1.35),(1.7,.12,1.3),0);box(bm,(0,-.065,1.35),(1.56,.01,1.16),1)
 for (cx,cz,w,h,mi) in ((-.45,1.6,.5,.4,2),(.2,1.55,.6,.3,3),(.5,1.1,.4,.5,2),(-.3,1.05,.7,.3,4),(-.05,1.3,1.4,.04,5),(.05,1.35,.04,1.0,5)):box(bm,(cx,-.072,cz),(w,.006,h),mi)
 roof=Matrix.Rotation(math.radians(18),4,'X');box(bm,(0,-.08,2.13),(1.8,.30,.04),0,roof);box(bm,(0,.08,2.13),(1.8,.30,.04),0,Matrix.Rotation(math.radians(-18),4,'X'))
built['campus_map']=build('campus_map',campus_map,lambda:[mat('Board_frame',(.22,.30,.26),.6,.3),mat('Map_paper',(.92,.90,.82),.9),mat('Map_green',(.45,.66,.38),.9),mat('Map_blue',(.42,.62,.82),.9),mat('Map_brick',(.78,.50,.38),.9),mat('Map_path',(.98,.97,.94),.9)])
def signpost(bm):
 # 2.6 m pole, four arrow blades at different heights/yaws, finial; blades stay inside 0.8 x 0.3
 cyl(bm,(0,0,0),(0,0,2.55),.045,10,0);cyl(bm,(0,0,2.55),(0,0,2.6),.07,10,0,.02)
 for i,(z,d) in enumerate(((2.25,1),(1.95,-1),(1.65,1),(1.35,-1))):
  x0=d*.04;x1=d*.40;tip=d*.40
  b=[bm.verts.new((x0,-.015,z-.09)),bm.verts.new((x1-d*.08,-.015,z-.09)),bm.verts.new((tip,-.015,z)),bm.verts.new((x1-d*.08,-.015,z+.09)),bm.verts.new((x0,-.015,z+.09))]
  t=[bm.verts.new((v.co.x,.015,v.co.z)) for v in b];f=bm.faces.new(b);f.material_index=1+i%2;g=bm.faces.new(t[::-1]);g.material_index=1+i%2
  for k in range(5):s=bm.faces.new((b[k],b[(k+1)%5],t[(k+1)%5],t[k]));s.material_index=0
 box(bm,(0,0,.05),(.30,.30,.10),0)
built['signpost']=build('signpost',signpost,lambda:[DARK(),mat('Sign_cream',(.93,.90,.80),.7),mat('Sign_teal',(.15,.45,.48),.7)])
def notice(bm):
 # two posts, cork board with a few pinned sheets, glazed front frame, flat roof
 for x in (-.85,.85):box(bm,(x,0,.95),(.1,.1,1.9),0)
 box(bm,(0,0,1.25),(1.7,.12,1.1),0);box(bm,(0,-.065,1.25),(1.56,.01,.96),1)
 for (cx,cz,w,h,mi) in ((-.5,1.45,.3,.4,2),(-.1,1.5,.3,.3,3),(.3,1.4,.35,.45,2),(-.35,1.0,.4,.3,4),(.35,.95,.3,.35,3)):box(bm,(cx,-.071,cz),(w,.004,h),mi)
 box(bm,(0,-.08,1.25),(1.6,.01,1.0),5);box(bm,(0,0,1.95),(1.8,.40,.06),0)
built['notice_board']=build('notice_board',notice,lambda:[mat('Board_wood',(.30,.22,.15),.7),mat('Cork',(.66,.48,.30),.95),mat('Paper_white',(.95,.95,.93),.9),mat('Paper_yellow',(.97,.88,.45),.9),mat('Paper_pink',(.95,.70,.75),.9),mat('Glass_clear',(.80,.88,.90),.05,0)])
def fountain(bm):
 # stainless column 1.0 m with a basin bowl and a spout, square foot
 box(bm,(0,0,.03),(.36,.36,.06),0);cyl(bm,(0,0,.06),(0,0,.80),.11,12,0)
 cyl(bm,(0,0,.80),(0,0,.95),.15,14,0,.25);cyl(bm,(0,0,.93),(0,0,.95),.22,14,1)
 cyl(bm,(.05,0,.95),(.05,0,1.0),.015,6,0);cyl(bm,(.12,0,.60),(.12,0,.66),.03,8,2)
built['drinking_fountain']=build('drinking_fountain',fountain,lambda:[mat('Steel_brushed',(.78,.79,.80),.3,.95),mat('Basin_inner',(.55,.57,.58),.2,.9),mat('Button_blue',(.15,.35,.75),.4)])
def lifebuoy(bm):
 # wooden post 1.6 m with a cross arm; orange ring (r .30) hangs on the face with four white bands
 box(bm,(0,0,.80),(.12,.12,1.6),0);box(bm,(0,0,1.45),(.5,.10,.08),0)
 torus(bm,(0,-.075,1.05),.30,.065,16,6,1,'Y')
 for a in (45,135,225,315):r=math.radians(a);box(bm,(.30*math.cos(r),-.075,1.05+.30*math.sin(r)),(.06,.15,.15),2,Matrix.Rotation(-r,4,'Y'))
 box(bm,(0,-.08,1.40),(.04,.06,.10),0)
built['lifebuoy_post']=build('lifebuoy_post',lifebuoy,lambda:[WOOD(),mat('Buoy_orange',(.95,.38,.08),.6),mat('Buoy_white',(.95,.95,.93),.6)])
def shower(bm):
 # concrete pad, stainless column 2.3 m with an arm and a head, foot tap
 box(bm,(0,0,.04),(.6,.6,.08),0);cyl(bm,(0,0,.08),(0,0,2.20),.04,10,1)
 cyl(bm,(0,0,2.18),(.22,0,2.25),.025,8,1);cyl(bm,(.24,0,2.30),(.24,0,2.20),.08,12,1,.10)
 cyl(bm,(0,-.04,1.05),(0,-.09,1.05),.02,6,1);box(bm,(0,-.12,1.05),(.08,.02,.03),2);box(bm,(0,-.1,.12),(.1,.12,.03),2)
built['beach_shower']=build('beach_shower',shower,lambda:[mat('Concrete_pad',(.70,.68,.64),.9),mat('Steel_brushed',(.78,.79,.80),.3,.95),mat('Tap_blue',(.15,.35,.75),.4)])
def aframe(bm):
 # two hinged boards leaning 12 degrees, blank chalk faces in wooden frames, spreader bar
 for s in (-1,1):
  r=Matrix.Rotation(math.radians(s*-13),4,'X');c=Vector((0,s*.10,.44))
  box(bm,c,(.58,.03,.88),0,r);box(bm,c+Vector((0,s*.017,.03)),(.48,.005,.66),1,r)
 box(bm,(0,0,.88),(.58,.08,.03),0);box(bm,(0,0,.30),(.02,.40,.02),0)
built['aframe_sign']=build('aframe_sign',aframe,lambda:[mat('Frame_wood',(.55,.38,.22),.75),mat('Chalk_black',(.08,.09,.09),.95)])
def banner(bm):
 # 3.0 m pole with weighted base, horizontal top and bottom arms, two-colour vertical banner
 cyl(bm,(0,0,0),(0,0,.08),.14,12,1);cyl(bm,(0,0,.08),(0,0,2.98),.035,10,0);cyl(bm,(0,0,2.98),(0,0,3.0),.05,10,0)
 for z in (2.85,1.45):cyl(bm,(0,0,z),(.54,0,z),.015,6,0)
 box(bm,(.29,0,2.15),(.50,.01,1.36),2);box(bm,(.29,-.004,2.6),(.50,.004,.25),3)
built['banner_pole']=build('banner_pole',banner,lambda:[STEEL(),mat('Base_black',(.08,.08,.08),.6),mat('Banner_navy',(.12,.20,.45),.85),mat('Banner_gold',(.90,.70,.20),.85)])
def hurdle(bm):
 # L-shaped feet (.7 deep) with uprights to 1.0 m (adjustable top), white bar with black stripes
 for x in (-.58,.58):
  box(bm,(x,.0,.03),(.05,.70,.05),0);cyl(bm,(x,-.30,.05),(x,-.30,1.0),.022,8,0)
 box(bm,(0,-.30,.93),(1.2,.025,.08),1)
 for x in (-.4,0,.4):box(bm,(x,-.316,.93),(.12,.004,.081),2)
built['track_hurdle']=build('track_hurdle',hurdle,lambda:[STEEL(),mat('Bar_white',(.95,.95,.93),.5),mat('Bar_black',(.05,.05,.05),.5)])
def shelter(bm):
 # 6 m steel frame: 4 posts, back rail, curved polycarbonate roof (8 strips), front wheel stands for 8 bikes
 for x in (-2.9,-.97,.97,2.9):
  cyl(bm,(x,.95,0),(x,.95,2.05),.05,8,0);cyl(bm,(x,.95,2.05),(x,-1.0,2.15),.04,8,0)
 cyl(bm,(-2.9,.95,1.0),(2.9,.95,1.0),.03,8,0);cyl(bm,(-2.9,-1.0,2.15),(2.9,-1.0,2.15),.035,8,0)
 prof=[(.95+.01,2.05),(.70,2.20),(.30,2.27),(-.10,2.30),(-.50,2.27),(-.80,2.21),(-1.05,2.13)]
 for (y0,z0),(y1,z1) in zip(prof,prof[1:]):
  a=[bm.verts.new((-3.0,y0,z0+.03)),bm.verts.new((3.0,y0,z0+.03)),bm.verts.new((3.0,y1,z1+.03)),bm.verts.new((-3.0,y1,z1+.03))];f=bm.faces.new(a);f.material_index=1
 for i in range(8):
  x=-2.45+i*.7;cyl(bm,(x-.18,-.2,.0),(x-.18,-.2,.35),.015,6,2);cyl(bm,(x+.18,-.2,.0),(x+.18,-.2,.35),.015,6,2);cyl(bm,(x-.18,-.2,.35),(x+.18,-.2,.35),.015,6,2)
 box(bm,(0,.95,.03),(5.9,.10,.06),0)
built['bike_shelter']=build('bike_shelter',shelter,lambda:[mat('Steel_grey',(.42,.45,.48),.4,.8),mat('Polycarbonate',(.75,.85,.88),.15),STEEL()])
bad=[k for k,b in built.items() if not b['fit']]
(D/'build.json').write_text(json.dumps(dict(status='authored_requires_native_install',plan=TASK['plan'],models=built,misfit=bad),indent=1),'utf8')
print('PARK',{k:(b['triangles'],b['size_m'],b['fit']) for k,b in built.items()},'misfit',bad,flush=True)
]====]
local path=out..'/park-props-payload.json'
os.execute('mkdir "'..out:gsub('/','\\')..'" 2>nul')
local f=assert(io.open(path,'wb'));f:write(j.encode({out=out,code=code,plan='C:/portable/OpenKoikatsu/output/street-social-city-production/world-tripo-v1/plan.json'}));f:close()
local expr="import json;TASK=json.load(open('"..path.."',encoding='utf8'));exec(TASK['code'])"
assert(os.execute('""C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" --background --factory-startup --python-expr "'..expr..'" > "'..out..'/park-props-build.log" 2>&1"'))
print('Park props authored; see world-blender-v1/park-props-build.log')
