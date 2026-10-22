"""Metric street furniture assembled exclusively from certified rectangular panels."""
import bpy,sys,math,json
from pathlib import Path
from mathutils import Matrix,Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import ROOT,BASE,OUT,write,read,sha,asset_definitions
from street_city_remesh_gate import verify
from street_city_campus_recipes import p
panel=verify(BASE/'campus-v1/components/unit-panel/remesh');bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=panel['path']);bpy.context.view_layer.update();basis=[([o.matrix_world@v.co for v in o.data.vertices],[list(p.vertices) for p in o.data.polygons]) for o in bpy.context.scene.objects if o.type=='MESH']
colors={'oak':(.37,.20,.09),'charcoal':(.045,.065,.065),'stone':(.52,.50,.43),'ivory':(.76,.74,.65),'teal':(.08,.28,.24),'soil':(.075,.045,.025),'light':(1,.78,.38),'blue':(.06,.24,.46),'yellow':(.72,.49,.07),'white':(.86,.85,.74),'red':(.5,.035,.02),'green':(.04,.7,.25)}
def mats():
 result={}
 for key,color in colors.items():
  m=bpy.data.materials.new('Street '+key);m.use_nodes=True;bs=m.node_tree.nodes['Principled BSDF'];bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Roughness'].default_value=.75 if key in ('stone','soil','oak') else .4
  if key=='charcoal':bs.inputs['Metallic'].default_value=.65
  if key=='light':bs.inputs['Emission Color'].default_value=(*color,1);bs.inputs['Emission Strength'].default_value=2
  tex=OUT/'textures'/(key+'.png')
  if tex.exists():
   n=m.node_tree.nodes.new('ShaderNodeTexImage');n.image=bpy.data.images.load(str(tex));n.image.pack();n.extension='REPEAT';m.node_tree.links.new(n.outputs['Color'],bs.inputs['Base Color'])
  result[key]=m
 return result
recipes={}
def box(size,pos,finish='oak',yaw=0):return p(size,pos,finish,yaw)
q=[]
for y in [-.22,-.07,.08,.23]:q.append(box((1.9,.13,.045),(0,y,.44)))
for z in [.64,.8,.92]:q.append(box((1.9,.045,.095),(0,.28,z-.045)))
for x in [-.73,.73]:q.extend([box((.06,.5,.44),(x,0,0),'charcoal'),box((.05,.05,.48),(x,.27,.44),'charcoal'),box((.05,.5,.045),(x,0,.65),'charcoal')])
recipes['bench-oak']=q
recipes['bench-stone']=[box((2,.6,.12),(0,0,.38),'stone')]+[box((.25,.5,.38),(x,0,0),'stone') for x in [-.7,.7]]
for name,w,d,h in [('planter-long',2.8,.8,.58),('planter-square',1.2,1.2,.65)]:
 recipes[name]=[box((w,d,.08),(0,0,0),'stone'),box((w-.16,d-.16,.06),(0,0,h-.12),'soil')]+[box((w,.08,h),(0,y,0),'stone') for y in [-d/2+.04,d/2-.04]]+[box((.08,d-.16,h),(x,0,0),'stone') for x in [-w/2+.04,w/2-.04]]
recipes['tree-grate']=[]
for x in [-.8,.8]:
 for j in range(10):recipes['tree-grate'].append(box((.74,.055,.035),(x,-1.1+j*.244,0),'charcoal'))
for y in [-.8,.8]:
 for j in range(4):recipes['tree-grate'].append(box((.055,.74,.035),(-.36+j*.24,y,0),'charcoal'))
recipes['lamp-modern']=[box((.32,.32,.1),(0,0,0),'stone'),box((.095,.095,5),(0,0,.1),'charcoal'),box((1.3,.24,.09),(.5,0,5.05),'charcoal'),box((1.1,.20,.025),(.5,0,5.025),'light')]
recipes['lamp-lantern']=[box((.2,.2,.08),(0,0,0),'stone'),box((.07,.07,2.9),(0,0,.08),'charcoal'),box((.42,.42,.4),(0,0,2.95),'light'),box((.55,.55,.07),(0,0,3.4),'charcoal')]+[box((.04,.04,.47),(x,y,2.95),'charcoal') for x in [-.22,.22] for y in [-.22,.22]]
recipes['bollard']=[box((.16,.16,.8),(0,0,0),'charcoal'),box((.165,.165,.055),(0,0,.65),'ivory')]
recipes['recycling']=[]
for x,color in [(-.45,'teal'),(0,'blue'),(.45,'yellow')]:
 recipes['recycling'] += [box((.43,.5,.07),(x,0,0),'charcoal'),box((.43,.5,.8),(x,0,.07),color),box((.43,.10,.15),(x,.2,.87),color),box((.43,.12,.045),(x,-.19,1.02),color),box((.43,.5,.04),(x,0,1.065),'charcoal')]
recipes['cycle-rack']=[box((2.4,.08,.08),(0,0,.04),'charcoal')]
for x in [-1,-.5,0,.5,1]:recipes['cycle-rack'] += [box((.035,.55,.035),(x,0,.74),'charcoal')]+[box((.035,.035,.74),(x,y,0),'charcoal') for y in [-.25,.25]]
recipes['drain']=[box((.055,.35,.025),(-.47+j*.105,0,0),'charcoal') for j in range(10)]+[box((1,.035,.025),(0,y,0),'charcoal') for y in [-.16,.16]]
recipes['utility-box']=[box((.8,.45,1.25),(0,0,0),'teal'),box((.72,.035,1.08),(0,-.24,.08),'ivory')]+[box((.6,.015,.02),(0,-.264,.65+j*.07),'charcoal') for j in range(5)]
recipes['bus-shelter']=[box((4,1.8,.10),(0,0,2.7),'teal'),box((3.7,.5,.08),(0,.45,.45),'oak')]+[box((.10,.10,2.7),(x,y,0),'charcoal') for x in [-1.85,1.85] for y in [-.7,.7]]+[box((3.8,.04,.09),(0,.75,z),'charcoal') for z in [.45,1,1.55,2.1]]
recipes['paving']=[box((2,2,.06),(0,0,-.06),'stone')]+[box((.018,2,.002),(x,0,.001),'ivory') for x in [-.66,0,.66]]+[box((2,.018,.002),(0,y,.001),'ivory') for y in [-.66,0,.66]]
recipes['curb']=[box((2,.22,.16),(0,0,-.13),'stone')]
recipes['tactile']=[box((.4,2,.008),(0,0,0),'yellow')]+[box((.028,1.9,.006),(x,0,.008),'yellow') for x in [-.12,-.04,.04,.12]]
recipes['parking-mark']=[box((.035,2,.008),(x,0,0),'white') for x in [-.3575,.3575]]+[box((.75,.035,.008),(0,y,0),'white') for y in [-.9825,.9825]]
recipes['railing']=[box((2,.06,.06),(0,0,1.04),'charcoal')]+[box((.05,.05,1.04),(x,0,0),'charcoal') for x in [-.95,-.45,.05,.55,.95]]
recipes['tree-trunk-collision']=[box((.32,.32,2.5),(0,0,0),'charcoal')]
recipes['lane-dash']=[box((3,.12,.003),(0,0,0),'white')]
recipes['crosswalk']=[box((.30,3.6,.003),(-1.3+j*.43,0,0),'white') for j in range(7)]
recipes['traffic-light']=[box((.10,.10,2.5),(0,0,0),'charcoal'),box((.45,.30,.8),(0,0,2.4),'charcoal')]+[box((.25,.015,.19),(0,-.158,z),color) for z,color in [(2.44,'green'),(2.70,'yellow'),(2.96,'red')]]
rows=[]
for aid,parts in recipes.items():
 bpy.ops.wm.read_factory_settings(use_empty=True);materials=mats();keys=list(materials);vv=[];ff=[];mi=[]
 for q in parts:
  transform=Matrix.Translation(Vector(q['pos']))@Matrix.Rotation(math.radians(q.get('yaw',0)),4,'Z')@Matrix.Diagonal(Vector((*q['size'],1)))
  for vertices,polygons in basis:
   start=len(vv);vv.extend(transform@v for v in vertices);ff.extend([start+j for j in poly] for poly in polygons);mi.extend([keys.index(q['finish'])]*len(polygons))
 # Footprint origin is the true horizontal envelope centre, including lamp arms.
 centre=Vector(((min(v.x for v in vv)+max(v.x for v in vv))/2,(min(v.y for v in vv)+max(v.y for v in vv))/2,0));vv=[v-centre for v in vv]
 mesh=bpy.data.meshes.new(aid);mesh.from_pydata(vv,[],ff);mesh.update();uv=mesh.uv_layers.new(name='Metric UV')
 for m in materials.values():mesh.materials.append(m)
 for poly,index in zip(mesh.polygons,mi):
  poly.material_index=index;axes=[i for i in range(3) if i!=max(range(3),key=lambda i:abs(poly.normal[i]))]
  for li in poly.loop_indices:
   v=mesh.vertices[mesh.loops[li].vertex_index].co;uv.data[li].uv=[v[i] for i in axes]
 ob=bpy.data.objects.new(aid,mesh);bpy.context.scene.collection.objects.link(ob);ob['certified_panel_sha256']=panel['sha256'];mesh.calc_loop_triangles();count=len(mesh.loop_triangles);assert count<=asset_definitions()[aid][1],(aid,count)
 signature=__import__('hashlib').sha256(json.dumps([parts,panel['sha256'],sha(__file__),[(str(p),sha(p)) for p in sorted((OUT/'textures').glob('*.png'))]],sort_keys=True).encode()).hexdigest()
 folder=OUT/'assets'/aid/'revisions'/signature[:16];folder.mkdir(parents=True,exist_ok=True);path=folder/(aid+'.glb');bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLB',export_extras=True);bpy.ops.wm.save_as_mainfile(filepath=str(path.with_suffix('.blend')))
 points=[v.co for v in mesh.vertices];bounds=[[min(v[i] for v in points),max(v[i] for v in points)] for i in range(3)]
 rows.append(dict(id=aid,path=str(path),sha256=sha(path),triangles=count,triangle_cap=asset_definitions()[aid][1],bounds_m=bounds,origin_correction_m=list(centre),certified_sources=[panel],parts=parts))
write(OUT/'panel-assets.json',dict(status='passed',assets=rows,generator_sha256=sha(__file__)))
