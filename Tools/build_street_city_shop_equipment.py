"""S02 board-built stainless beverage equipment, with real open basin geometry."""
import bpy,json,sys,argparse,math
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_building_context import context,LIBRARY
p=argparse.ArgumentParser();p.add_argument('--building',default='S02');a=p.parse_args(sys.argv[sys.argv.index('--')+1:]);C=context(a.building);OUT=C['out']/'furniture';OUT.mkdir(parents=True,exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
def mat(name,color,metal=0,rough=.4):
 m=bpy.data.materials.new(name);m.use_nodes=True;bs=m.node_tree.nodes['Principled BSDF'];bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Metallic'].default_value=metal;bs.inputs['Roughness'].default_value=rough;return m
steel=mat('304 stainless',(.42,.45,.47),.85,.28);black=mat('black ABS',(.025,.03,.035));green=mat('jade shop panels',(.04,.22,.17));paper=mat('sealed paper carton',(.65,.55,.37));screen=mat('display glass',(.03,.12,.15));white=mat('cream enamel',(.8,.8,.75))
def P(s,c,m=steel):return s,c,m
def table(w=1.3,d=.8,h=.9):return [P((w,d,.05),(0,0,h-.025))]+[P((.04,.04,h-.05),(x,y,(h-.05)/2)) for x in [-w/2+.05,w/2-.05] for y in [-d/2+.05,d/2-.05]]+[P((w-.1,d-.1,.03),(0,0,.2))]
def sink(w,d,h,n):
 r=table(w,d,h)[1:]
 for j in range(4):
  size,pos,material=r[j];r[j]=P((size[0],size[1],h-.02),(pos[0],pos[1],(h-.02)/2),material)
 r+=[P((w,.13,.04),(0,y,h-.02)) for y in [-d/2+.065,d/2-.065]]
 bay=w/n
 for x in [-w/2+.03,w/2-.03]+[(-w/2+i*bay) for i in range(1,n)]:r.append(P((.06,d-.24,.04),(x,0,h-.02)))
 for i in range(n):
  x=-w/2+(i+.5)*bay;bw=bay-.07;bd=d-.25
  r.append(P((bw,bd,.015),(x,0,h-.24)))
  r.extend([P((.015,bd,.24),(x+sgn*(bw/2-.0075),0,h-.12)) for sgn in [-1,1]])
  r.extend([P((bw,.015,.24),(x,sgn*(bd/2-.0075),h-.12)) for sgn in [-1,1]])
  r.extend([P((.025,.025,.26),(x,d/2-.08,h+.13)),P((.025,.2,.025),(x,d/2-.16,h+.25))])
 return r
recipes={
 'steel-table':table(),
 'steel-counter':table()+[P((1.2,.03,.62),(0,-.35,.52),green)]+[P((.58,.03,.6),(x,.35,.52)) for x in [-.3,.3]],
 'double-sink':sink(1.3,.8,.9,2),'handwash':sink(.55,.6,.85,1),
 'pos':[P((.3,.25,.035),(0,0,.0175),black),P((.04,.04,.25),(0,.04,.15),black),P((.3,.03,.2),(0,.04,.29),screen)],
 'tea-dispenser':[P((.4,.4,.55),(0,0,.375)),P((.4,.4,.08),(0,0,.04)),P((.35,.015,.12),(0,-.21,.5),screen),P((.08,.1,.04),(0,-.21,.25)),P((.035,.04,.075),(0,-.26,.22),black)]+[P((.015,.3,.015),(x,-.04,.09)) for x in [-.15,-.1,-.05,0,.05,.1,.15]],
 'syrup-doser':[P((.24,.3,.42),(0,0,.21),white),P((.18,.015,.1),(0,-.16,.31),screen),P((.025,.08,.03),(0,-.18,.13)),P((.18,.12,.015),(0,-.2,.03))],
 'ice-maker':[P((.8,.8,.72),(0,0,.36)),P((.8,.7,.55),(0,.05,.995)),P((.72,.02,.32),(0,-.41,.51),white),P((.72,.02,.35),(0,-.31,1.05),black)]+[P((.68,.015,.012),(0,-.415,z),black) for z in [.1,.15,.2,.25]],
 'cup-sealer':[],
 'ingredient-box':[P((.18,.2,.25),(0,0,.125),paper)],
 'signboard':[P((3.2,.09,.65),(0,0,.325),green),P((2.95,.03,.45),(0,-.06,.325),white)],
 'menu-board':[P((1.8,.08,.65),(0,0,.325),green),P((1.7,.025,.55),(0,-.055,.325),white)]
}
# Separate frame pieces keep an actual loading opening instead of a solid box.
recipes['cup-sealer']=[P((.35,.36,.05),(0,0,.025),black),P((.35,.16,.56),(0,.1,.33),white),P((.35,.36,.12),(0,0,.55),green),P((.22,.015,.085),(0,-.19,.53),screen),P((.19,.22,.025),(0,-.07,.09))]+[P((.03,.21,.3),(x,-.06,.26),black) for x in [-.16,.16]]+[P((.19,.035,.02),(0,y,.39)) for y in [-.15,-.015]]+[P((.035,.1,.02),(x,-.0825,.39)) for x in [-.0775,.0775]]
recipes['cup-sealer'][-4:]=[P((.19,.035,.02),(0,y,.39)) for y in [-.15,.015]]+[P((.035,.13,.02),(x,-.0675,.39)) for x in [-.0775,.0775]]
recipes['tea-dispenser'] += [P((.04,.04,.02),(x,y,.09)) for x in [-.15,.15] for y in [-.15,.15]]
recipes['ice-maker'][-4:]=[P((.68,.015,.012),(0,-.4075,z),black) for z in [.1,.15,.2,.25]]
records=[]
for aid,parts in recipes.items():
 for o in list(bpy.data.objects):bpy.data.objects.remove(o,do_unlink=True)
 folder=OUT/aid;folder.mkdir(exist_ok=True)
 for j,(size,pos,material) in enumerate(parts):
  bpy.ops.mesh.primitive_cube_add(size=1,location=pos);o=bpy.context.object;o.name=f'{aid}-{j}';o.dimensions=size;bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(material);o['source_route']='dimensioned independent plates, not AI';o['equipment_detail']='open sink bowl or loading bay where applicable'
  if aid in ('signboard','menu-board') and j==1:
   m=material.copy();m.use_nodes=True;node=m.node_tree.nodes.new('ShaderNodeTexImage');node.image=bpy.data.images.load(str(OUT/(aid+'-2k.png')));node.image.pack();m.node_tree.links.new(node.outputs['Color'],m.node_tree.nodes['Principled BSDF'].inputs['Base Color']);o.data.materials[0]=m
  for face in o.data.polygons:
   axes=[k for k in range(3) if k!=max(range(3),key=lambda k:abs(face.normal[k]))]
   for li in face.loop_indices:
    q=o.data.vertices[o.data.loops[li].vertex_index].co;o.data.uv_layers.active.data[li].uv=(q[axes[0]],q[axes[1]])
    if aid in ('signboard','menu-board') and j==1:o.data.uv_layers.active.data[li].uv=(q.x/size[0]+.5,q.z/size[2]+.5)
 bpy.context.view_layer.update();meshes=[o for o in bpy.context.scene.objects if o.type=='MESH'];bounds=[]
 for o in meshes:
  pts=[o.matrix_world@v.co for v in o.data.vertices];bounds.append(([min(v[k] for v in pts) for k in range(3)],[max(v[k] for v in pts) for k in range(3)]))
 seen={0};changed=True
 while changed:
  changed=False
  for i,(lo,hi) in enumerate(bounds):
   if i in seen:continue
   if any(math.sqrt(sum(max(0,lo[k]-bounds[j][1][k],bounds[j][0][k]-hi[k])**2 for k in range(3)))<=.0051 for j in seen):seen.add(i);changed=True
 assert len(seen)==len(bounds),(aid,'unsupported disconnected plates')
 measured=[max(q[1][k] for q in bounds)-min(q[0][k] for q in bounds) for k in range(3)]
 bpy.ops.export_scene.gltf(filepath=str(folder/'source.glb'),export_format='GLB',export_extras=True)
 records.append(dict(id=aid,source=str(folder/'source.glb'),triangle_cap=max(500,len(parts)*60),parts=len(parts),measured_size_m=measured,plate_contact_tolerance_m=.0051,all_plates_connected=True))
(OUT/'sources.json').write_text(json.dumps(records,indent=2),'utf-8')
