"""Dimensioned board furniture and rectangular equipment, mandatory remesh next."""
import bpy,json,argparse,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];BASE=ROOT/'output/street-social-city-production/S01-modular-components-v1';OUT=BASE/'assembly-building/furniture';OUT.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser();parser.add_argument('--only');args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(BASE/'board-bookshelf/bookshelf-boards.glb'))
oak=next(m for m in bpy.data.materials if m.use_nodes and any(n.type=='TEX_IMAGE' for n in m.node_tree.nodes))
def mat(name,color,metal=0,rough=.5,glass=False):
 m=bpy.data.materials.new(name);m.use_nodes=True;b=m.node_tree.nodes.get('Principled BSDF');b.inputs['Base Color'].default_value=(*color,1);b.inputs['Metallic'].default_value=metal;b.inputs['Roughness'].default_value=rough
 if glass:b.inputs['Transmission Weight'].default_value=.85
 return m
steel=mat('brushed stainless steel',(.36,.38,.4),.8,.3);white=mat('white enamel',(.83,.82,.78));fabric=mat('woven cream fabric',(.65,.61,.53),rough=.9);dark=mat('equipment black',(.025,.03,.035));glass=mat('glass',(.65,.8,.83),rough=.1,glass=True)
sign=mat('S01 sign print',(.1,.15,.1))
diffuser=mat('warm luminous diffuser',(.85,.8,.65));bs=diffuser.node_tree.nodes['Principled BSDF'];bs.inputs['Emission Color'].default_value=(1,.86,.65,1);bs.inputs['Emission Strength'].default_value=3
if (OUT/'sign-2k.png').exists():
 node=sign.node_tree.nodes.new('ShaderNodeTexImage');node.image=bpy.data.images.load(str(OUT/'sign-2k.png'));node.image.pack();sign.node_tree.links.new(node.outputs['Color'],sign.node_tree.nodes['Principled BSDF'].inputs['Base Color'])
recipes={}
def part(s,p,m=oak):return (s,p,m)
def cabinet(w,d,h):
 return [part((w,d,.05),(0,0,h-.025)),part((w,d,.07),(0,0,.035))]+[part((.04,d,h-.12),(x,0,h/2)) for x in [-w/2+.02,w/2-.02]]+[part((w,.035,h-.12),(0,d/2-.0175,h/2))]+[part((w/2-.025,.04,h-.16),(x,-d/2+.02,h/2)) for x in [-w/4,w/4]]+[part((.18,.03,.025),(x,-d/2-.01,h*.6),steel) for x in [-w/4,w/4]]
recipes['service-counter']=cabinet(.45,2.4,.9)
recipes['prep-counter']=cabinet(.6,2.5,.9)
# A 1.5m-wide, 0.7m-deep cabinet rotated to stand along the east wall.
recipes['wardrobe']=[((size[1],size[0],size[2]),(pos[1],-pos[0],pos[2]),material) for size,pos,material in cabinet(1.5,.7,2.0)]
recipes['kitchen']=cabinet(4.5,.7,.9)
recipes['fridge']=[part((.7,.7,1.8),(0,0,.9),white),part((.66,.03,1.05),(0,-.365,.6),white),part((.66,.03,.62),(0,-.365,1.47),white),part((.025,.04,.45),(.24,-.4,.85),steel),part((.025,.04,.3),(.24,-.4,1.5),steel)]
recipes['bed']=[part((2.0,2.1,.22),(0,0,.25)),part((2.0,2.0,.22),(0,-.05,.47),fabric),part((2.08,.08,.9),(0,1.01,.65)),part((1.95,1.2,.04),(0,-.4,.6),fabric)]+[part((.75,.4,.13),(x,.65,.645),white) for x in [-.48,.48]]+[part((.08,.08,.18),(x,y,.09)) for x in [-.9,.9] for y in [-.93,.93]]
recipes['sofa']=[part((1.6,.82,.18),(0,0,.28)),part((1.6,.12,.65),(0,.35,.675))]+[part((.12,.82,.5),(x,0,.55)) for x in [-.74,.74]]+[part((.66,.65,.18),(x,-.035,.46),fabric) for x in [-.34,.34]]+[part((.66,.13,.43),(x,.23,.73),fabric) for x in [-.34,.34]]+[part((.06,.06,.19),(x,y,.095)) for x in [-.68,.68] for y in [-.32,.32]]
recipes['coffee-machine']=[part((.7,.42,.5),(0,.05,.35),steel),part((.7,.48,.08),(0,0,.04),steel),part((.64,.025,.1),(0,-.175,.51),dark),part((.7,.4,.025),(0,.04,.615),steel)]+[part((.11,.12,.08),(x,-.21,.27),steel) for x in [-.16,.16]]+[part((.035,.18,.03),(x,-.32,.23),dark) for x in [-.16,.16]]+[part((.025,.45,.015),(x,0,.09),steel) for x in [-.3,-.2,-.1,0,.1,.2,.3]]
recipes['shower']=[part((.9,.9,.06),(0,0,.03),white),part((.015,.9,2),(.4425,0,1.06),glass),part((.9,.015,2),(0,.4425,1.06),glass),part((.025,.03,1.4),(-.3,.38,1.3),steel),part((.025,.25,.025),(-.3,.26,2),steel),part((.18,.2,.025),(-.3,.15,2),steel),part((.14,.04,.08),(-.3,.38,1.0),steel)]
recipes['entryframe']=[part((.08,.2,2.48),(x,0,1.24)) for x in [-1.24,1.24]]+[part((2.4,.2,.08),(0,0,2.44))]
recipes['entryleaf']=[part((.05,.045,2.36),(x,0,1.18)) for x in [-.573,.573]]+[part((1.096,.045,.05),(0,0,z)) for z in [.025,2.335]]+[part((1.096,.008,2.26),(0,0,1.18),glass),part((.025,.04,.35),(.45,-.04,1.1),steel)]
recipes['facade-band']=[part((12,.08,.1),(0,0,.05),dark)]
recipes['signboard']=[part((3.2,.09,.65),(0,0,.325),oak),part((2.95,.03,.45),(0,-.06,.325),sign)]
recipes['stool']=[part((.42,.42,.05),(0,0,.425))]+[part((.045,.045,.4),(x,y,.2)) for x in [-.17,.17] for y in [-.17,.17]]
recipes['ceiling-light']=[part((.6,.6,.05),(0,0,.035),white),part((.54,.54,.01),(0,0,.005),diffuser)]
recipes['hob']=[part((.6,.5,.03),(0,0,.015),dark)]+[part((.19,.008,.005),(x,y,.032),steel) for x in [-.15,.15] for y in [-.1,.1]]+[part((.008,.2,.005),(x,y,.032),steel) for x in [-.245,-.055,.055,.245] for y in [0]]
recipes['vent-grille']=[part((.35,.03,.25),(0,0,.125),white)]+[part((.3,.01,.008),(0,-.02,z),dark) for z in [.04,.07,.1,.13,.16,.19,.22]]
records=json.loads((OUT/'sources.json').read_text('utf-8')) if args.only and (OUT/'sources.json').exists() else []
for aid,parts in recipes.items():
 if args.only and aid not in args.only.split(','):continue
 records=[r for r in records if r['id']!=aid]
 for obj in list(bpy.data.objects):bpy.data.objects.remove(obj,do_unlink=True)
 folder=OUT/aid;folder.mkdir(exist_ok=True)
 for j,(size,pos,material) in enumerate(parts):
  bpy.ops.mesh.primitive_cube_add(size=1,location=pos);o=bpy.context.object;o.name=f'{aid}-{j}';o.dimensions=size;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(material);o['source_route']='dimensioned rectangular construction'
  for face in o.data.polygons:
   axes=[k for k in range(3) if k!=max(range(3),key=lambda k:abs(face.normal[k]))]
   for li in face.loop_indices:
    q=o.data.vertices[o.data.loops[li].vertex_index].co;o.data.uv_layers.active.data[li].uv=(q[axes[0]],q[axes[1]])
    if aid=='signboard' and j==1:o.data.uv_layers.active.data[li].uv=(q.x/size[0]+.5,q.z/size[2]+.5)
 bpy.ops.export_scene.gltf(filepath=str(folder/'source.glb'),export_format='GLB',export_extras=True)
 records.append(dict(id=aid,source=str(folder/'source.glb'),triangle_cap=max(500,len(parts)*60),parts=len(parts)))
(OUT/'sources.json').write_text(json.dumps(records,indent=2),'utf-8')
