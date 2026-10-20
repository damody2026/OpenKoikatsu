"""Build metric S01 F02 architecture from the canonical floor contract.

This source must pass the same mandatory remesh stage before assembly.
"""
import bpy,json,sys,math,argparse
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'Tools'))
from street_city_parts import walls,slab,stairs
from street_city_building_context import context
from street_city_small_shop import stair_slab_opening
BASE=ROOT/'output/street-social-city-production/S01-modular-components-v1'
parser=argparse.ArgumentParser();parser.add_argument('--floor',type=int,default=2);parser.add_argument('--building-mode',action='store_true');parser.add_argument('--building',default='S01');args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
floor=args.floor;C=context(args.building);BID=C['id']
OUT=C['out']/f'structure/F{floor:02d}' if args.building_mode else BASE/'assembly-f02/structure';OUT.mkdir(parents=True,exist_ok=True)
m=json.loads((ROOT/'Docs/StreetSocialCity/production/assembly-manifest.json').read_text('utf-8'))
b=C['building'];f=b['small_shop_design']['floors'][floor-1];z=f['z_m'];h=f['height_m'];w,d=b['footprint_m']
bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.gltf(filepath=str(BASE/'board-bookshelf/bookshelf-boards.glb'))
oak=next(mat for mat in bpy.data.materials if mat.use_nodes and any(n.type=='TEX_IMAGE' for n in mat.node_tree.nodes))
for o in list(bpy.data.objects):bpy.data.objects.remove(o,do_unlink=True)
def mat(name,color,metal=0):
    q=bpy.data.materials.new(name);q.use_nodes=True;bs=q.node_tree.nodes.get('Principled BSDF');bs.inputs['Base Color'].default_value=(*color,1);bs.inputs['Metallic'].default_value=metal;bs.inputs['Roughness'].default_value=.55;return q
plaster=mat('Warm plaster',(.78,.75,.68));steel=mat('Steel',(.35,.38,.4),.8);parts=[]
floor_finish=oak
if BID=='S02':
    floor_finish=mat('S02 matte ceramic tile',(.65,.69,.66));bs=floor_finish.node_tree.nodes['Principled BSDF'];bs.inputs['Roughness'].default_value=.8
    tex=floor_finish.node_tree.nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(C['out']/'furniture/floor-2k.png'));tex.image.pack();floor_finish.node_tree.links.new(tex.outputs['Color'],bs.inputs['Base Color'])
def box(name,pos,size,material=plaster,kind='wall',yaw=0):
    bpy.ops.mesh.primitive_cube_add(size=1,location=Vector(pos)+Vector((0,0,size[2]/2)));o=bpy.context.object;o.name=name;o.dimensions=size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.rotation_euler.z=yaw;o.data.materials.append(material)
    # World-like metre UVs remain continuous across slab patches.
    for p in o.data.polygons:
        axes=[k for k in range(3) if k!=max(range(3),key=lambda k:abs(p.normal[k]))]
        for li in p.loop_indices:
            co=o.data.vertices[o.data.loops[li].vertex_index].co+o.location;o.data.uv_layers.active.data[li].uv=(co[axes[0]],co[axes[1]])
    o['part_kind']=kind;o['source_route']='dimensioned local construction';parts.append(dict(name=name,kind=kind,position_m=pos,size_m=size));return o
# True facade openings and frames share the same outer envelope.
apertures=[dict(q) for q in f['exterior_openings']]
for q in apertures:
    if floor==1 and q['local_position_m'][2]==0:q['width_m']+=.16;q['height_m']+=.08
if args.building_mode and floor==1:apertures.append(dict(side='south',local_position_m=[.9,0,2.62],width_m=3.2,height_m=.65))
for i,p in enumerate(walls([w-.4,d-.4,h-.3],apertures)):
    box(f'F{floor:02d}-exterior-{i}',[p['position_m'][0],p['position_m'][1],p['position_m'][2]+z],p['size_m'])
core=next(c for c in f['cells'] if c['kind']=='stairs');cx=(core['bounds_m'][0]+core['bounds_m'][2])/2;cy=(core['bounds_m'][1]+core['bounds_m'][3])/2
holes=[dict(center_m=[(c['bounds_m'][0]+c['bounds_m'][2])/2,(c['bounds_m'][1]+c['bounds_m'][3])/2],size_m=[c['bounds_m'][2]-c['bounds_m'][0],c['bounds_m'][3]-c['bounds_m'][1]]) for c in f['cells'] if c['kind']=='lift']
coredata=dict(position_m=[cx,cy,z],size_m=[core['bounds_m'][2]-core['bounds_m'][0],core['bounds_m'][3]-core['bounds_m'][1],h])
holes.append(stair_slab_opening(coredata,f['stair_geometry'],b['small_shop_design']['floors'][max(0,floor-2)]['stair_geometry']))
if args.building_mode and floor==1:holes=[]
for i,p in enumerate(slab([w,d,.3],holes,z=z-.3)):
    box(f'F{floor:02d}-floor-{i}',p['position_m'],p['size_m'],floor_finish,'floor')
ceiling_parts=slab([w,d,.3],[] if floor==3 else holes,z=z+h-.3) if not args.building_mode or floor==3 else []
for i,p in enumerate(ceiling_parts):
    box(f'F{floor:02d}-ceiling-{i}',p['position_m'],p['size_m'],plaster,'ceiling')
for cell in f['cells']:
    if cell['kind'] in ('activity','corridor'):continue
    a,y0,c,y1=cell['bounds_m'];ccx=(a+c)/2;ccy=(y0+y1)/2
    ds=[q for q in f['doors'] if q['owner']==cell['id']]
    cuts=[dict(side=q['outward_side'],local_position_m=[q['center_m'][0]-ccx,q['center_m'][1]-ccy,0],width_m=q['width_m']+.16,height_m=q['height_m']+.08) for q in ds]
    for i,p in enumerate(walls([c-a-.4,y1-y0-.4,h-.3],cuts)):
        side=p.get('side')
        if (side=='north' and abs(y1-(d/2-.2))<1e-6) or (side=='east' and abs(c-(w/2-.2))<1e-6) or (side=='west' and abs(a-(-w/2+.2))<1e-6):continue
        box(f'F{floor:02d}-core-{cell["kind"]}-{i}',[p['position_m'][0]+ccx,p['position_m'][1]+ccy,p['position_m'][2]+z],p['size_m'])
stair_parts=stairs([coredata['size_m'][0],coredata['size_m'][1],h],f['stair_geometry']) if not args.building_mode or floor<3 else []
for i,p in enumerate(stair_parts):
    if p['kind']=='stair_tread':
        p['position_m'][2]+=p['size_m'][2]-.12;p['size_m'][2]=.12
    box(f'F{floor:02d}-{p["kind"]}-{i}',[p['position_m'][0]+cx,p['position_m'][1]+cy,p['position_m'][2]+z],p['size_m'],floor_finish,p['kind'])
# Square-section handrails are exact prism parts, not generated decoration.
g=f['stair_geometry'];run=g['flight_run_m'];start=cy-run/2;end=cy+run/2
for flight,xc,za,zb in ([] if args.building_mode and floor==3 else [(0,cx-.9,z+.04,z+h/2-.12),(1,cx+.9,z+h-.12,z+h/2+.04)]):
    for side in [-1,1]:
        aa=Vector((xc+side*.65,start,za));bb=Vector((xc+side*.65,end,zb));delta=bb-aa
        o=box(f'F{floor:02d}-stringer-{flight}-{side}',[0,0,0],[.08,.16,delta.length],steel,'stringer');o.location=(aa+bb)/2;o.rotation_euler=delta.to_track_quat('Z','Y').to_euler()
for railindex,(xx,za,zb) in enumerate([] if args.building_mode and floor==3 else [(cx-.9-.77,z+g['riser_height_m'],z+h/2),(cx-.9+.77,z+g['riser_height_m'],z+h/2),(cx+.9-.77,z+h,z+h/2),(cx+.9+.77,z+h,z+h/2)]):
    aa=Vector((xx,start,za+.95));bb=Vector((xx,end,zb+.95));delta=bb-aa
    o=box(f'F{floor:02d}-handrail-{railindex}',[0,0,0],[.04,.04,delta.length],steel,'handrail');o.location=(aa+bb)/2;o.rotation_euler=delta.to_track_quat('Z','Y').to_euler()
    for j,t in enumerate([0,.5,1]):
        q=aa.lerp(bb,t);box(f'F{floor:02d}-railpost-{railindex}-{j}',[q.x,q.y,q.z-.95],[.04,.04,.95],steel,'handrail')

if args.building_mode and not next(c for c in f['cells'] if c['kind']=='activity')['open']:
    # Apartment entrance in its north wall; bedroom has its own west door.
    cut=next(q for q in f['doors'] if q['source']=='room')
    xo=cut['center_m'][0];left=xo-.68;right=xo+.68
    boundary=next(c['bounds_m'][3] for c in f['cells'] if c['kind']=='activity')+.1
    for j,(aa,bb) in enumerate([(-w/2+.2,left),(right,w/2-.2)]):box(f'F{floor:02d}-activity-north-{j}',[(aa+bb)/2,boundary,z],[bb-aa,.2,h-.3])
    box(f'F{floor:02d}-activity-door-lintel',[xo,boundary,z+2.48],[1.36,.2,h-.3-2.48])
# Local enclosed rooms still need all walls inside an open-plan shop.
if args.building_mode:
    activity=next(c for c in f['cells'] if c['kind']=='activity')
    for room in f['private_rooms']:
        aa,yy,cc,tt=room['bounds_m'];rx=(aa+cc)/2;ry=(yy+tt)/2;door=room['door']
        cuts=[dict(side=door['side'],local_position_m=[door['center_m'][0]-rx,door['center_m'][1]-ry,0],width_m=door['width_m']+.16,height_m=2.48)]
        for j,part in enumerate(walls([cc-aa-.4,tt-yy-.4,h-.3],cuts)):
            side=part.get('side')
            exterior=(side=='west' and abs(aa-(-w/2+.2))<1e-6) or (side=='east' and abs(cc-(w/2-.2))<1e-6) or (side=='south' and abs(yy-(-d/2+.2))<1e-6) or (side=='north' and abs(tt-(d/2-.2))<1e-6)
            shared_activity=(not activity['open'] and side=='north' and abs(tt-activity['bounds_m'][3])<1e-6)
            if exterior or shared_activity:continue
            box(f'F{floor:02d}-private-{room["id"]}-{j}',[part['position_m'][0]+rx,part['position_m'][1]+ry,part['position_m'][2]+z],part['size_m'])
if args.building_mode and floor>1:
    # Guard only true upper-floor void edges. Top storey also guards the
    # unused lower-flight entrance; intermediate storey keeps it open.
    hole=next(q for q in holes if q.get('kind')=='stair_flights');hx,hy=hole['center_m'];hw,hd=hole['size_m'];x0=hx-hw/2-.02;x1=hx+hw/2+.02;y0=hy-hd/2-.02;y1=hy+hd/2+.02
    lines=[((x0,y0),(x0,y1)),((x1,y0),(x1,y1)),((x0,y1),(x1,y1))]
    if floor==3:lines.append(((x0,y0),(cx+.15,y0)))
    for j,(aa,bb) in enumerate(lines):
        xx=(aa[0]+bb[0])/2;yy=(aa[1]+bb[1])/2;length=math.dist(aa,bb)
        box(f'F{floor:02d}-void-guard-top-{j}',[xx,yy,z+.93],[length,.04,.04] if aa[1]==bb[1] else [.04,length,.04],steel,'void_guard')
        for k in range(math.ceil(length/1.2)+1):
            t=k/math.ceil(length/1.2);box(f'F{floor:02d}-void-guard-post-{j}-{k}',[aa[0]+(bb[0]-aa[0])*t,aa[1]+(bb[1]-aa[1])*t,z],[.04,.04,.93],steel,'void_guard')

# Flush thresholds close the landing-to-cab gap without filling the lift shaft.
box(f'F{floor:02d}-lift-threshold',[C['lift_door'][0]+.1,C['lift_door'][1],z-.05],[.5,1.2,.05],steel,'threshold')
for o in bpy.context.scene.objects:o.select_set(True)
bpy.ops.export_scene.gltf(filepath=str(OUT/'source.glb'),export_format='GLB',use_selection=True,export_extras=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'source.blend'))
(OUT/'source.json').write_text(json.dumps(dict(structure_revision=2,floor=floor,building_mode=args.building_mode,footprint_m=[w,d],z_m=z,height_m=h,slab_openings=holes,parts=parts),indent=2),'utf-8')
