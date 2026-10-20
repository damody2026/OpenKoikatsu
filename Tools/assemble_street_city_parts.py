"""Blender importer for approved room parts; explicit --proxy produces layout boxes only.

blender --background --factory-startup --python Tools/assemble_street_city_parts.py -- --building EE --proxy
Default strict mode refuses missing/unapproved assets and never substitutes proxy geometry.
"""
import argparse
import json
import math
import sys
import hashlib
import struct
from pathlib import Path
import bpy
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_parts import transform
from street_city_model_gate import inspect_glb,approval_errors

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--building',required=True);p.add_argument('--room');p.add_argument('--proxy',action='store_true');p.add_argument('--world',action='store_true')
args=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
manifest=json.loads((ROOT/'Docs/StreetSocialCity/production/assembly-manifest.json').read_text('utf-8'))
room_layouts={l['room_id']:l for l in manifest['layouts'] if l.get('building')==args.building and 'room_id' in l and (not args.room or l['room_id']==args.room)}
if not room_layouts:raise SystemExit('No matching indoor room layouts')
assets={a['id']:a for a in manifest['assets']}
chosen=[i for i in manifest['instances'] if i['owner'] in room_layouts or (not args.room and i['owner']==args.building)]
modpath=ROOT/'Docs/StreetSocialCity/production/manufacturing-manifest.json'
mod=json.loads(modpath.read_text('utf-8'))
digest=hashlib.sha256((ROOT/'Docs/StreetSocialCity/production/assembly-manifest.json').read_bytes()).hexdigest()
if mod['source_manifest_sha256']!=digest:raise SystemExit('Modular manifest is stale; rebuild before assembly')
recipes={q['source_asset_id']:q for q in mod['assemblies']}
assets.update({a['id']:a for a in mod['assets']})
expanded=[];pending_details=[]
for inst in chosen:
    recipe=recipes[inst['asset_id']]
    if recipe['components']:
        for k,c in enumerate(recipe['components']):
            expanded.append(dict(inst,id=inst['id']+f'/part{k}',asset_id=c['asset_id'],position_m=transform(c['position_m'],inst['position_m'],inst['rotation_z_deg']),rotation_z_deg=inst['rotation_z_deg']+c['rotation_z_deg']))
        if recipe['unfinished_details']:pending_details.append(dict(asset=inst['asset_id'],reason=recipe['unfinished_details']))
    elif 'control_components' not in assets[inst['asset_id']] and assets[inst['asset_id']]['role'] not in ('circulation','facade_module','structural_band','outdoor_node'):
        expanded.append(inst)
    elif recipe['unfinished_details']:pending_details.append(dict(asset=inst['asset_id'],reason=recipe['unfinished_details']))
chosen=expanded
ASSET_ROOT=ROOT/'Content/IndependentResources/StreetSocialCity'
OUT=ROOT/'output/street-social-city-production/assemblies';OUT.mkdir(parents=True,exist_ok=True)
cache={};missing=[]
if not args.proxy:
    ledger=json.loads((ROOT/'Docs/StreetSocialCity/production/generation-ledger.json').read_text('utf-8'))
    rejection_reasons={}
    for aid in {i['asset_id'] for i in chosen}:
        a=assets[aid];model=ASSET_ROOT/a['expected_file'];approval=ASSET_ROOT/a['approval_file']
        if not model.is_file() or not approval.is_file():missing.append(aid);continue
        try:
            meta=json.loads(approval.read_text('utf-8'))
            errors=approval_errors(a,inspect_glb(model),meta,ledger,ROOT)
        except (ValueError,KeyError,IndexError,TypeError,OSError,struct.error) as exc:errors=[str(exc)]
        if errors:missing.append(aid);rejection_reasons[aid]=errors
    if missing or pending_details:
        (OUT/f'{args.building}-missing-assets.json').write_text(json.dumps(missing,indent=2),'utf-8')
        (OUT/f'{args.building}-pending-details.json').write_text(json.dumps(pending_details,indent=2),'utf-8')
        (OUT/f'{args.building}-rejected-models.json').write_text(json.dumps(rejection_reasons,indent=2),'utf-8')
        raise SystemExit(f'{len(missing)} missing/unapproved parts, {len(pending_details)} unresolved details; no final assembly created')
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
def empty(name,position=(0,0,0),yaw=0):
    o=bpy.data.objects.new(name,None);bpy.context.collection.objects.link(o);o.location=position;o.rotation_euler[2]=math.radians(yaw);return o
root=empty(args.building+('_LAYOUT_PROXY_NOT_TRIPO' if args.proxy else '_ASSEMBLED_TRIPO_PARTS'))
if args.world:root.location=next(q['world_position_m'] for q in manifest['layouts'] if q.get('kind')=='building' and q['building']==args.building)
parents={rid:empty(rid,l['position_m'],l['rotation_z_deg']) for rid,l in room_layouts.items()}
for o in parents.values():o.parent=root
def material(name,color):
    m=bpy.data.materials.new(name);m.diffuse_color=(*color,1);return m
blue=material('PROXY_room_not_generated',(.18,.55,.60));orange=material('PROXY_circulation_not_generated',(.8,.47,.21))
if args.proxy:
    # Exact hollow structural controls, with genuine door/shaft voids.
    # Furnishing envelopes are display-only bounds, never physics colliders.
    mesh_cache={}
    for i in chosen:
        a=assets[i['asset_id']]
        if a.get('role')=='door':continue # leaf fully open for control inspection
        size=a['size_m'];dims=tuple(size)
        if dims not in mesh_cache:
            # Data API avoids a dependency-graph update for every unique cube
            # in large halls; the object transform remains canonical metres.
            w,d,h=size;mesh=bpy.data.meshes.new('control-prism')
            mesh.from_pydata([(x*w/2,y*d/2,z*h/2) for x,y,z in [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]],[],[(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)])
            mesh_cache[dims]=mesh
        o=bpy.data.objects.new(i['id']+'_CONTROL',mesh_cache[dims]);bpy.context.collection.objects.link(o)
        o.parent=parents.get(i['owner'],root);o.location=Vector(i['position_m'])+Vector((0,0,size[2]/2));o.rotation_euler[2]=math.radians(i['rotation_z_deg'])
        o['control_only']=True;o['collision_approved']=False
        if a.get('control_geometry'):o.data.materials.clear();o.data.materials.append(blue)
        else:o.display_type='WIRE';o.hide_render=True
else:
    for i in chosen:
        a=assets[i['asset_id']];model=ASSET_ROOT/a['expected_file']
        if a['id'] not in cache:
            before=set(bpy.data.objects);bpy.ops.import_scene.gltf(filepath=str(model));new=list(set(bpy.data.objects)-before)
            top=[o for o in new if o.parent not in new]
            imported_root=empty(a['id']+'_SOURCE')
            for o in top:o.parent=imported_root
            cache[a['id']]=(imported_root,new)
            imported_root.hide_render=True;imported_root.hide_viewport=True
        source,objects=cache[a['id']]
        parent=empty(i['id'],i['position_m'],i['rotation_z_deg']);parent.parent=parents.get(i['owner'],root)
        clones={}
        for o in objects:
            c=o.copy();bpy.context.collection.objects.link(c);clones[o]=c
        for o,c in clones.items():c.parent=clones.get(o.parent,parent);c.hide_viewport=False;c.hide_render=False
    # Sources are not exported; approved models must already have metre scale and canonical pivots.
    for source,objects in cache.values():
        for o in objects:bpy.data.objects.remove(o,do_unlink=True)
        bpy.data.objects.remove(source,do_unlink=True)
tag='layout-proxy' if args.proxy else 'assembled'
stem=f'{args.building}'+(f'-{args.room}' if args.room else '')+f'-{tag}'
scene=bpy.context.scene
scene['production_status']='layout_proxy_not_tripo' if args.proxy else 'assembled_approved_parts_pending_gameplay_QA'
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/f'{stem}.blend'))
bpy.ops.export_scene.gltf(filepath=str(OUT/f'{stem}.glb'),export_format='GLB')
report=dict(building=args.building,rooms=len(room_layouts),status=scene['production_status'],source_manifest_sha256=digest,assets_generated_by_this_script=0,structural_and_bound_instances=len(chosen),pending_details=len(pending_details),glb=str(OUT/f'{stem}.glb'))
(OUT/f'{stem}.json').write_text(json.dumps(report,indent=2),'utf-8');print(json.dumps(report))
