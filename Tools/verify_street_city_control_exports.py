"""Inspect real exported GLB bytes, node transforms and cube vertex dimensions."""
from pathlib import Path
import hashlib,json,math,struct
from street_city_parts import transform
from street_city_space_policy import segments
from street_city_io import write_json

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production'
raw=(ROOT/'Docs/StreetSocialCity/production/assembly-manifest.json').read_bytes();m=json.loads(raw);digest=hashlib.sha256(raw).hexdigest()
mod=json.loads((ROOT/'Docs/StreetSocialCity/production/manufacturing-manifest.json').read_text('utf-8'));recipes={r['source_asset_id']:r for r in mod['assemblies']}
assets={a['id']:a for a in m['assets']+mod['assets']};rooms={r['room_id']:r for r in m['layouts'] if r.get('room_id')}
report=json.loads((OUT/'control-assemblies-acceptance.json').read_text('utf-8'))
assert report['status']=='passed' and report['source_manifest_sha256']==digest
errors=[];checks=0;building_results=[];entrance_clearance_checks=0;open_edge_clearance_checks=0;small_shop_route_clearance_checks=0
def check(ok,label):
    global checks
    checks+=1
    if not ok:errors.append(label)
def identity():return [[float(r==c) for c in range(4)] for r in range(4)]
def multiply(a,b):return [[sum(a[r][k]*b[k][c] for k in range(4)) for c in range(4)] for r in range(4)]
def matrix(node):
    if 'matrix' in node:return [[node['matrix'][c*4+r] for c in range(4)] for r in range(4)]
    x,y,z,w=node.get('rotation',[0,0,0,1]);sx,sy,sz=node.get('scale',[1,1,1]);t=node.get('translation',[0,0,0])
    rows=[[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
    result=identity()
    for r in range(3):
        for c in range(3):result[r][c]=rows[r][c]*[sx,sy,sz][c]
        result[r][3]=t[r]
    return result
for record in report['buildings']:
    bid=record['building'];data=(ROOT/record['glb']).read_bytes()
    check(hashlib.sha256(data).hexdigest()==record['sha256'],bid+' exported GLB hash changed')
    magic,version,length=struct.unpack_from('<III',data,0);check(magic==0x46546c67 and version==2 and length==len(data),bid+' invalid GLB')
    jl,jt=struct.unpack_from('<II',data,12);g=json.loads(data[20:20+jl]);bl,bt=struct.unpack_from('<II',data,20+jl);binary=data[28+jl:28+jl+bl]
    nodes=g['nodes'];names={n.get('name',''):i for i,n in enumerate(nodes)};parents={j:i for i,n in enumerate(nodes) for j in n.get('children',[])};world={}
    def world_matrix(i):
        if i not in world:world[i]=multiply(world_matrix(parents[i]) if i in parents else identity(),matrix(nodes[i]))
        return world[i]
    mesh_sizes={};mesh_bounds={}
    for i,mesh in enumerate(g['meshes']):
        vertices=[]
        for primitive in mesh['primitives']:
            accessor=g['accessors'][primitive['attributes']['POSITION']];view=g['bufferViews'][accessor['bufferView']]
            assert accessor['componentType']==5126 and accessor['type']=='VEC3'
            offset=view.get('byteOffset',0)+accessor.get('byteOffset',0);stride=view.get('byteStride',12)
            vertices.extend(struct.unpack_from('<fff',binary,offset+k*stride) for k in range(accessor['count']))
        mesh_sizes[i]=[max(v[k] for v in vertices)-min(v[k] for v in vertices) for k in range(3)]
        mesh_bounds[i]=[(min(v[k] for v in vertices),max(v[k] for v in vertices)) for k in range(3)]
    owned={rid:r for rid,r in rooms.items() if r['building']==bid};expected=0;structural_names=set()
    for rid in owned:check(rid in names,bid+' missing actual room parent '+rid)
    for inst in m['instances']:
        if inst['owner']!=bid and inst['owner'] not in owned:continue
        recipe=recipes[inst['asset_id']];source=assets[inst['asset_id']]
        if recipe['components']:items=[(inst['id']+f'/part{k}',c['asset_id'],transform(c['position_m'],inst['position_m'],inst['rotation_z_deg'])) for k,c in enumerate(recipe['components'])]
        elif 'control_components' not in source and source['role'] not in ('circulation','facade_module','structural_band','outdoor_node'):items=[(inst['id'],inst['asset_id'],inst['position_m'])]
        else:items=[]
        for key,aid,base in items:
            a=assets[aid]
            if a.get('role')=='door':continue
            expected+=1;name=key+'_CONTROL';check(name in names,bid+' missing actual part '+name)
            if recipe['components']:structural_names.add(name)
            if name not in names:continue
            node=nodes[names[name]];check('mesh' in node,name+' missing mesh')
            actual=sorted(mesh_sizes[node['mesh']]);check(all(abs(x-y)<.0001 for x,y in zip(actual,sorted(a['size_m']))),name+' incorrect exported vertex dimensions')
            center=[base[0],base[1],base[2]+a['size_m'][2]/2]
            if inst['owner'] in owned:
                r=owned[inst['owner']];center=transform(center,r['position_m'],r['rotation_z_deg'])
            expected_glTF=[center[0],center[2],-center[1]];actual_matrix=world_matrix(names[name]);actual_glTF=[actual_matrix[k][3] for k in range(3)]
            check(all(abs(x-y)<.001 for x,y in zip(expected_glTF,actual_glTF)),name+' wrong exported parent/world position')
    check(sum('mesh' in n for n in nodes)==expected,bid+' unexpected or missing geometry instances')
    # Test the doorway approach against actual exported cube geometry, including
    # display-only furniture bounds. Do not substitute source-plan room boxes.
    building=next(b for b in m['layouts'] if b.get('kind')=='building' and b['building']==bid)
    fronts=building['entrances']
    fronts=[q for q in fronts if q.get('purpose')=='street_main']
    open_zones=[z for f in building['floors'] for z in f['open_activity_zones']]
    if fronts or open_zones or building.get('layout_profile')=='small_shop_v2':
        actual_boxes=[]
        for idx,node in enumerate(nodes):
            if 'mesh' not in node:continue
            matrix_actual=world_matrix(idx);extents=mesh_bounds[node['mesh']]
            points=[]
            for xx in extents[0]:
                for yy in extents[1]:
                    for zz in extents[2]:
                        p=[sum(matrix_actual[r][k]*[xx,yy,zz][k] for k in range(3))+matrix_actual[r][3] for r in range(3)]
                        points.append([p[0],-p[2],p[1]])
            actual_boxes.append((node.get('name',''),[min(p[k] for p in points) for k in range(3)]+[max(p[k] for p in points) for k in range(3)]))
        if building.get('layout_profile')=='small_shop_v2':
            for floor in building['floors']:
                design=floor['small_shop_design'];z=floor['z_m']
                paths=[c['bounds_m'] for c in design['cells'] if c['kind']=='corridor']+[design['internal_route_bounds_m']]
                if design.get('internal_cross_route_bounds_m'):paths.append(design['internal_cross_route_bounds_m'])
                for index,rect in enumerate(paths):
                    small_shop_route_clearance_checks+=1
                    a,b,c,d=rect;probe=[a+1e-4,b+1e-4,z+.05,c-1e-4,d-1e-4,z+2.2]
                    blockers=[name for name,box in actual_boxes if all(min(probe[k+3],box[k+3])-max(probe[k],box[k])>1e-4 for k in range(3))]
                    check(not blockers,f'{bid}/{floor["floor"]}F actual 2m path {index} blocked by '+', '.join(blockers[:5]))
        for entry in fronts:
            entrance_clearance_checks+=1
            x,y,z=entry['position_m'];inside=-1 if entry['facade_side']=='N' else 1
            passage=entry.get('beyond_threshold_clear_width_m',entry['width_m'])
            probe=[x-passage/2,min(y,y+inside*2),z+.05,
                   x+passage/2,max(y,y+inside*2),z+entry['height_m']]
            blockers=[name for name,box in actual_boxes if all(min(probe[k+3],box[k+3])-max(probe[k],box[k])>1e-4 for k in range(3))]
            check(not blockers,entry['id']+' actual control doorway blocked by '+', '.join(blockers[:5]))
        structural_boxes=[(name,box) for name,box in actual_boxes if name in structural_names]
        for zone in open_zones:
            for opening in zone['open_edges']:
                open_edge_clearance_checks+=1
                a,b=segments(zone['center_m'],zone['size_m'],zone['rotation_z_deg'],opening)
                normal={'south':[0,-1,0],'north':[0,1,0],'east':[1,0,0],'west':[-1,0,0]}[opening['side']]
                nx,ny,_=transform(normal,[0,0,0],zone['rotation_z_deg'])
                length=opening['width_m'];trim=min(.01,length/4)
                dx,dy=(b[0]-a[0])/length,(b[1]-a[1])/length
                aa=[a[0]+dx*trim+nx*.1,a[1]+dy*trim+ny*.1]
                bb=[b[0]-dx*trim+nx*.1,b[1]-dy*trim+ny*.1]
                z=zone['center_m'][2]
                probe=[min(aa[0],bb[0])-abs(nx)*.099,min(aa[1],bb[1])-abs(ny)*.099,z+.05,
                       max(aa[0],bb[0])+abs(nx)*.099,max(aa[1],bb[1])+abs(ny)*.099,z+opening['height_m']-.05]
                blockers=[name for name,box in structural_boxes if all(min(probe[k+3],box[k+3])-max(probe[k],box[k])>1e-4 for k in range(3))]
                check(not blockers,zone['id']+' actual open edge '+opening['side']+' blocked by '+', '.join(blockers[:5]))
    building_results.append(dict(building=bid,rooms=len(owned),mesh_instances=expected))
result=dict(status='passed' if not errors else 'failed',source_manifest_sha256=digest,checks=checks,buildings=building_results,
    rooms=sum(q['rooms'] for q in building_results),entrance_clearance_checks=entrance_clearance_checks,open_edge_clearance_checks=open_edge_clearance_checks,
    small_shop_route_clearance_checks=small_shop_route_clearance_checks,
    errors=errors,scope='actual GLB control vertices, transforms, storefront interior approaches and full-height open-zone structural clearances; not Tripo mesh quality or runtime navigation')
write_json(OUT/'control-glb-acceptance.json',result)
print(json.dumps({k:result[k] for k in ('status','checks','rooms','errors')},ensure_ascii=False))
if errors:raise SystemExit(1)
