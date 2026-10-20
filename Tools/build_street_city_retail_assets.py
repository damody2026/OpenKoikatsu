"""Reusable dimensioned plate assemblies. Curved merchandise is never replaced by boxes."""
import bpy,sys,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production/shop-street-components-v1'
bpy.ops.wm.read_factory_settings(use_empty=True)
def material(name,color,metal=0,rough=.5,alpha=1):
 m=bpy.data.materials.new(name);m.use_nodes=True;b=m.node_tree.nodes['Principled BSDF'];b.inputs['Base Color'].default_value=(*color,alpha);b.inputs['Metallic'].default_value=metal;b.inputs['Roughness'].default_value=rough
 if alpha<1:b.inputs['Alpha'].default_value=alpha;m.surface_render_method='DITHERED'
 return m
steel=material('brushed stainless',(.48,.5,.52),.95,.33);wood=material('walnut laminate',(.22,.13,.065),rough=.55);black=material('black coating',(.025,.028,.03));glass=material('clear display glass',(.8,.91,.94),rough=.12,alpha=.22);paper=material('paperboard',(.6,.43,.2));white=material('appliance enamel',(.8,.8,.77),rough=.35)
def p(size,center,mat=steel):return size,center,mat
def cabinet(w,d,h,mat=wood):
 return [p((w,d,.04),(0,0,z),mat) for z in [.02,h-.02]]+[p((.04,d,h),(x,0,h/2),mat) for x in [-w/2+.02,w/2-.02]]+[p((w,.04,h),(0,d/2-.02,h/2),mat)]
def shelf(w=1.2,d=.45,h=1.8):
 return [p((.04,d,h),(x,0,h/2),wood) for x in [-w/2+.02,w/2-.02]]+[p((w,d,.03),(0,0,z),wood) for z in [.015,.45,.9,1.35,h-.015]]
recipes={
 'retail-case':cabinet(1.5,.7,.55)+[p((1.5,.7,.02),(0,0,z),glass) for z in [.78,1.02,1.2]]+[p((.02,.7,.65),(x,0,.875),glass) for x in [-.74,.74]]+[p((1.5,.02,.65),(0,-.34,.875),glass)],
 'bread-rack':shelf(1.2,.55,1.6),
 'retail-rack':shelf(),
 'tray-rack':[p((.04,.04,1.7),(x,y,.85)) for x in [-.34,.34] for y in [-.34,.34]]+[p((.72,.72,.025),(0,0,z)) for z in [.1,.35,.6,.85,1.1,1.35,1.6]],
 'deck-oven':cabinet(1.2,.9,1.8,steel)+[p((1.12,.04,.43),(0,-.44,z),black) for z in [.35,.9,1.45]]+[p((.75,.025,.25),(-.1,-.465,z),glass) for z in [.35,.9,1.45]]+[p((.7,.05,.025),(-.1,-.49,z)) for z in [.53,1.08,1.63]],
 'exhaust-hood':cabinet(1.4,1,.3,steel)+[p((.4,.4,.7),(0,.25,.65))],
 'clothes-rack':[p((1.2,.6,.04),(0,0,.02),black),p((.04,.04,1.7),(x,0,.89),black)] if False else [],
 'record-bin':cabinet(1.2,.6,.8)+[p((.025,.6,.3),(x,0,.95),wood) for x in [-.59,-.2,.2,.59]]+[p((1.2,.025,.3),(0,y,.95),wood) for y in [-.29,.29]],
 'display-plinth':cabinet(.65,.65,.85),
 'product-carton':[p((.12,.08,.18),(0,0,.09),paper)],
 'folded-fabric':[p((.35,.25,.06),(0,0,.03),white)],
 'screen-terminal':[p((.4,.25,.03),(0,0,.015),black),p((.04,.04,.3),(0,.05,.18),black),p((.55,.04,.34),(0,.05,.37),black)],
 'wall-mirror':[p((.7,.04,1.2),(0,0,.6),wood),p((.62,.012,1.12),(0,-.026,.6),material('mirror silver',(.92,.92,.92),1,.025))],
 'fruit-bin':cabinet(1.2,.65,.75)+[p((.035,.65,.18),(x,0,.84),wood) for x in [-.58,-.2,.2,.58]],
 'guitar-stand':[p((.55,.5,.04),(0,0,.02),black),p((.04,.04,.7),(0,.2,.37),black),p((.4,.04,.04),(0,.2,.72),black)],
 'bike-stand':[p((.7,.5,.04),(0,0,.02),black)]+[p((.04,.04,.35),(x,.15,.215),black) for x in [-.18,.18]],
 'planter-box':cabinet(.45,.45,.5),
}
recipes['clothes-rack']=[p((1.2,.6,.04),(0,0,.02),black)]+[p((.04,.04,1.7),(x,0,.89),black) for x in [-.55,.55]]+[p((1.14,.04,.04),(0,0,1.76),black)]
records=[]
for aid,parts in recipes.items():
 for o in list(bpy.data.objects):bpy.data.objects.remove(o,do_unlink=True)
 folder=OUT/aid;folder.mkdir(parents=True,exist_ok=True)
 for i,(size,center,mat) in enumerate(parts):
  bpy.ops.mesh.primitive_cube_add(size=1,location=center);o=bpy.context.object;o.name=f'{aid}-plate-{i}';o.dimensions=size;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(mat);o['source_route']='metric local plates';o['asset_id']=aid
 bpy.context.view_layer.update();points=[o.matrix_world@v.co for o in bpy.context.scene.objects for v in o.data.vertices]
 size=[max(p[k] for p in points)-min(p[k] for p in points) for k in range(3)]
 bpy.ops.export_scene.gltf(filepath=str(folder/'source.glb'),export_format='GLB',export_extras=True)
 records.append(dict(id=aid,source=str(folder/'source.glb'),size=size,triangle_cap=max(500,len(parts)*60),route='dimensioned plates'))
(OUT/'board-sources.json').write_text(json.dumps(records,indent=2),'utf-8')
