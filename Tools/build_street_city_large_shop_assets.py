"""Board-buildable inn/spa fixtures; export sources only, never bypass remesh."""
import bpy,json,math,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_retail_profiles import COMMON
bpy.ops.wm.read_factory_settings(use_empty=True)
def mat(name,color,metal=0):
 m=bpy.data.materials.new(name);m.use_nodes=True;b=m.node_tree.nodes['Principled BSDF'];b.inputs['Base Color'].default_value=(*color,1);b.inputs['Metallic'].default_value=metal;b.inputs['Roughness'].default_value=.6;return m
wood=mat('Pale oak laminated boards',(.55,.36,.18));white=mat('Washable ivory partition',(.77,.77,.72));steel=mat('Stainless steel',(.45,.48,.5),.9);fabric=mat('Ivory mattress fabric',(.76,.74,.68));water=mat('Pool water',(.16,.48,.5));bs=water.node_tree.nodes['Principled BSDF'];bs.inputs['Transmission Weight'].default_value=.7;bs.inputs['Roughness'].default_value=.16
def p(size,center,material=wood,pitch=0):return (size,center,material,pitch)
def cabinet(w,d,h):
 return [p((w,d,.04),(0,0,z)) for z in [.02,h-.02]]+[p((.04,d,h),(x,0,h/2)) for x in [-w/2+.02,w/2-.02]]+[p((w,.04,h),(0,d/2-.02,h/2))]+[p((w/2-.02,.025,h-.1),(x,-d/2+.0125,h/2)) for x in [-w/4,w/4]]+[p((.025,.04,.2),(x,-d/2-.0075,h/2),steel) for x in [-.06,.06]]
def cubicle(w,d):
 return [p((.05,d,2.4),(x,0,1.2),white) for x in [-w/2+.025,w/2-.025]]+[p((w,.05,2.4),(0,d/2-.025,1.2),white),p((w,.05,.2),(0,-d/2+.025,2.3),white)]+[p(((w-1.2)/2,.05,2.2),(x,-d/2+.025,1.1),white) for x in [-(w+1.2)/4,(w+1.2)/4]]
recipes={
 'large-lift-car':[p((3.3,3,.05),(0,0,-.025),steel),p((3.3,3,.05),(0,0,2.425),steel),p((3.3,.1,2.4),(0,-1.45,1.2),steel)]+[p((.1,2.8,2.4),(x,0,1.2),steel) for x in [-1.6,1.6]]+[p((.97,.1,2.4),(x,1.45,1.2),steel) for x in [-1.165,1.165]],
 'large-lift-threshold':[p((1.2,.6,.05),(0,0,-.025),steel)],
 'large-standard-cubicle':cubicle(1.4,1.9),
 'large-accessible-cubicle':cubicle(2.3,2.3),
 'large-locker':cabinet(.5,.5,1.8),
 'large-wardrobe':cabinet(1.4,.6,2.1),
 'large-campus-wardrobe':cabinet(.65,.6,1.9),
 'large-service-cabinet':cabinet(3,.6,2.2),
 'large-service-tall':cabinet(1,.6,2),
 'large-spa-basin':[p((3,3,.12),(0,0,.06),white)]+[p((3,.15,.78),(0,y,.51),white) for y in [-1.425,1.425]]+[p((.15,2.7,.78),(x,0,.51),white) for x in [-1.425,1.425]]+[p((2.7,2.7,.008),(0,0,.65),water)],
 'large-bunk':[p((.08,.08,1.9),(x,y,.95)) for x in [-.51,.51] for y in [-1.01,1.01]]+[p((1.1,2.1,.08),(0,0,z)) for z in [.2,1.2]]+[p((1,2,.15),(0,0,z),fabric) for z in [.315,1.315]]+[p((.04,2,.04),(x,0,1.75)) for x in [-.53,.53]]+[p((1,.04,.04),(0,y,1.75)) for y in [-1.03,1.03]]+[p((.04,.04,1.4),(x,-1.08,.7)) for x in [-.25,.25]]+[p((.54,.04,.04),(0,-1.08,z)) for z in [.15,.4,.65,.9,1.15]],
 'large-lounger':[p((.06,.06,.35),(x,y,.175)) for x in [-.34,.34] for y in [-.75,.35]]+[p((.8,.07,.04),(0,-.84+j*.09,.37)) for j in range(12)]+[p((.8,.07,.04),(0,.3+j*.07,.4+j*.055),pitch=38) for j in range(7)],
 'large-console':cabinet(1.2,.6,.9)+[p((.55,.04,.35),(0,0,1.15),steel),p((.04,.04,.15),(0,0,.975),steel)],
 'large-grab-rails':[p((.7,.04,.04),(0,0,.35),steel),p((.04,.04,.7),(-.33,0,.35),steel)],
 'large-bin':cabinet(.4,.4,.6),
}
records=[]
recipes['large-spa-basin-v2']=[p((3,3,.12),(0,0,.06),white),p((3,.15,.78),(0,1.425,.51),white),p((3,.15,.78),(0,-.225,.51),white)]+[p((.15,1.65,.78),(x,.6,.51),white) for x in [-1.425,1.425]]+[p((2.7,1.5,.008),(0,.6,.65),water)]+[p((.8,.2,.15*(i+1)),(-.65,-1.4+i*.2,.075*(i+1)),white) for i in range(6)]+[p((.8,.2,.744-i*.156),(-.65,-.05+i*.2,(.744-i*.156)/2),white) for i in range(4)]+[p((.04,.04,.9),(-.19,y,h+.45),steel) for y,h in [(-1.4,.15),(-.4,.9)]]+[p((.04,.04,1.25),(-.19,-.9,1.425),steel,pitch=-53.130102)]
for aid,parts in recipes.items():
 existing=COMMON/aid/'remesh/remesh-report.json'
 if existing.exists():
  from street_city_remesh_gate import verify
  verify(existing.parent);records.append(next(q for q in json.loads((COMMON/'large-shop-board-sources.json').read_text('utf-8')) if q['id']==aid));continue
 for o in list(bpy.data.objects):bpy.data.objects.remove(o,do_unlink=True)
 folder=COMMON/aid;folder.mkdir(parents=True,exist_ok=True)
 for i,(size,center,material,pitch) in enumerate(parts):
  bpy.ops.mesh.primitive_cube_add(size=1,location=center);o=bpy.context.object;o.name=aid+f'-plate-{i}';o.dimensions=size;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.rotation_euler.x=math.radians(pitch);o.data.materials.append(material);o['source_route']='metric local boards';o['asset_id']=aid
 bpy.context.view_layer.update();pts=[o.matrix_world@v.co for o in bpy.context.scene.objects for v in o.data.vertices];dims=[max(q[k] for q in pts)-min(q[k] for q in pts) for k in range(3)]
 bpy.ops.export_scene.gltf(filepath=str(folder/'source.glb'),export_format='GLB',export_extras=True)
 records.append(dict(id=aid,size=dims,triangle_cap=max(1000,len(parts)*60),source=str(folder/'source.glb')))
(COMMON/'large-shop-board-sources.json').write_text(json.dumps(records,indent=2),'utf-8')
