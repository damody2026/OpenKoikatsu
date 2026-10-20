"""Semantic stocking of real product meshes on measured physical supports.

Furniture is certified panel construction. Products keep their Tripo materials;
they are never recoloured by the architectural finish palette. Every instance
has a product ID, tenant, source and support, and a counted triangle cost.
"""
import json,re,math
from pathlib import Path
from street_city_mall_merchandise import OUT,folder_for,LIMIT
from street_city_remesh_gate import verify

def catalogue():
 data=json.loads((OUT/'catalogue.json').read_text('utf8'))
 assert data['unique_product_models']==len(data['components'])<=LIMIT
 assert len(data['tenants'])==80
 return data

def lods():
 path=OUT/'lods/acceptance.json'
 if not path.exists():return {}
 data=json.loads(path.read_text('utf8'));assert data['status']=='passed';return data['products']

def folders():return {'product-'+q['id']:folder_for(q)/'remesh' for q in catalogue()['components']}|{'product-'+aid+'-display':OUT/'lods'/aid/'remesh' for aid in lods()}

def triangle_count(path):
 import struct
 raw=Path(path).read_bytes();n=struct.unpack_from('<I',raw,12)[0];doc=json.loads(raw[20:20+n]);meshes=[sum(doc['accessors'][p['indices']]['count']//3 for p in m['primitives']) for m in doc['meshes']]
 return sum(meshes[n['mesh']] for n in doc['nodes'] if 'mesh' in n)

def stock(parts,curves,contract):
 from street_city_mall import COLUMN_X,COLUMN_Y,estimate_triangles
 from street_city_mall_boutiques import profiles
 from street_city_campus_recipes import desk,chair,shift,shelf
 data=catalogue();products={q['id']:q for q in data['components']};palettes=profiles();surfaces=[];placements=[];sources={}
 for aid,row in products.items():
  receipt=verify(folder_for(row)/'remesh');sources[aid]=dict(**receipt,triangles=triangle_count(receipt['path']))
 display_sources={}
 for aid,record in lods().items():
  receipt=verify(OUT/'lods'/aid/'remesh');assert record['hero_sha256']==sources[aid]['sha256'];display_sources[aid]=dict(**receipt,triangles=triangle_count(receipt['path']))
 from street_city_product_repairs import members,REPAIRS
 from street_city_mall import component_folders
 primitive_costs={}
 for aid in products:
  for member in members(aid):
   primitive=member['primitive']
   if primitive not in primitive_costs:primitive_costs[primitive]=triangle_count(verify(component_folders()[primitive])['path'])
   sources[aid]['triangles']+=primitive_costs[primitive]
 parts[:]=[q for q in parts if not re.match(r'M1-\d{4}-display-',q['name'])]
 curves[:]=[q for q in curves if not re.match(r'M1-\d{4}-(mannequin|flowers)',q['name'])]
 def add(no,name,size,pos,finish,owner,kind='stock_fixture'):
  q=dict(floor=no,name=name,size=list(size),pos=list(pos),finish=finish,kind=kind,yaw=0,primitive='panel',tenant_id=owner);parts.append(q);return q
 def make_parts(room,name,items,x,y,z):
  palette=palettes[room['id']]
  for i,q in enumerate(items):
   finish=q.get('finish','wood');finish=palette['case'] if finish=='wood' else palette['trim'] if finish=='steel' else palette['fabric'] if finish=='fabric' else finish
   p=q['pos'];add(room['floor'],name+'-'+str(i),q['size'],[x+p[0],y+p[1],z+p[2]],finish,room['id'])
 def support(room,tag,x,y,z,w,d,ids,mode='surface'):
  surfaces.append(dict(id=room['id']+'-stock-'+tag,tenant=room['id'],floor=room['floor'],position_m=[x,y,z],size_m=[w,d],mode=mode,products=list(ids)))
 def safe(x,y,w,d):
  return not any(abs(x-cx)<w/2+.3+.4 and abs(y-cy)<d/2+.3+.4 for cx in COLUMN_X for cy in COLUMN_Y)
 def choose_anchor(x,y,w,d,room):
  a,b,c,dd=room['bounds']
  for dx,dy in [(0,0),(-1.5,0),(1.5,0),(0,-1.5),(0,1.5),(-2.5,0),(2.5,0)]:
   xx,yy=x+dx,y+dy
   if a+1.5+w/2<xx<c-1.5-w/2 and b+2+d/2<yy<dd-2-d/2 and safe(xx,yy,w,d):return xx,yy
  return None
 for room in contract['rooms']:
  rid=room['id'];no=room['floor'];a,b,c,d=room['bounds'];cx=(a+c)/2;cy=(b+d)/2;z=(no-1)*4.2;ids=data['tenants'][rid];palette=palettes[rid];positive=cy>0
  if no>=9 or rid=='M1-0607':
   # Real dining tables and chairs; meals on occupied tables, never on floors.
   yrows=[b+5,b+10] if positive else [d-5,d-10,d-15]
   index=0
   for y in yrows:
    for dx in [-9,-3,3,9]:
     x=cx+dx
     if not safe(x,y,1.8,2):continue
     name=rid+'-stock-dining-'+str(index);items=desk(1.4,.85)+shift(chair(),x=-.45,y=.78)+shift(chair(),x=.45,y=.78)+shift(chair(),x=-.45,y=-.78,yaw=180)+shift(chair(),x=.45,y=-.78,yaw=180)
     make_parts(room,name,items,x,y,z);support(room,'dining-'+str(index),x,y,z+.75,1.4,.85,ids);index+=1
  elif no in (2,3) and rid not in ('M1-0208','M1-0306'):
   # Hanging rails at a real 1.8m height. The garment hook reaches the rail.
   for index,(dx,dy) in enumerate([(-7,-3),(7,-3),(0,4)]):
    point=choose_anchor(cx+dx,cy+dy,2.4,.7,room)
    if not point:continue
    x,y=point
    add(no,rid+f'-stock-rack-{index}-rail',[2.4,.035,.035],[x,y,z+1.8],palette['trim'],rid)
    for xx in [x-1.15,x+1.15]:
     add(no,rid+f'-stock-rack-{index}-post',[.035,.035,1.8],[xx,y,z],palette['trim'],rid);add(no,rid+f'-stock-rack-{index}-foot',[.12,.7,.035],[xx,y,z],palette['trim'],rid)
    support(room,'rack-'+str(index),x,y,z+1.8,2.25,.5,[i for i in ids if i!='folded-shirt'],'hanging')
   if 'folded-shirt' in ids:
    point=choose_anchor(cx,cy-3,1.8,.9,room)
    assert point,(rid,'folded apparel table')
    x,y=point;make_parts(room,rid+'-stock-folded-table',desk(1.8,.9),x,y,z);support(room,'folded-table',x,y,z+.75,1.8,.9,['folded-shirt'])
  elif rid in ('M1-0405','M1-0501','M1-0502','M1-0503','M1-0705'):
   for index,(dx,dy) in enumerate([(-7,-3),(7,-3),(-7,4),(7,4)]):
    point=choose_anchor(cx+dx,cy+dy,2.4,2.6,room)
    if not point:continue
    x,y=point;add(no,rid+f'-stock-platform-{index}',[2.6,2.8,.06],[x,y,z+.008],palette['floor_finish'],rid)
    support(room,'platform-'+str(index),x,y,z+.068,2.6,2.8,[ids[0]])
   if len(ids)>1:
    make_parts(room,rid+'-stock-side-table',desk(1.4,.8),cx,cy,z);support(room,'side-table',cx,cy,z+.75,1.4,.8,ids[1:])
  elif no==6 and rid in ('M1-0601','M1-0602','M1-0603'):
   for index,dx in enumerate([-7,7]):
    point=choose_anchor(cx+dx,cy+1,1.2,.6,room)
    if not point:continue
    x,y=point
    for xx in [-.58,.58]:add(no,rid+f'-stock-bookcase-{index}-side',[.04,.5,.92],[x+xx,y,z],palette['case'],rid)
    for zz in [0,.44,.88]:add(no,rid+f'-stock-bookcase-{index}-shelf',[1.2,.5,.035],[x,y,z+zz],palette['case'],rid)
    for level in range(2):support(room,f'shelf-{index}-{level}',x,y,z+level*.44+.035,1.08,.44,ids)
  else:
   for index,(dx,dy) in enumerate([(-7,-3.5),(7,-3.5),(-7,3.5),(7,3.5)]):
    point=choose_anchor(cx+dx,cy+dy,2.4,1.2,room)
    if not point:continue
    x,y=point;name=rid+'-stock-island-'+str(index)
    # A finished cabinet with a real solid supporting top, not a floating lid.
    add(no,name+'-body',[2.4,1.2,.88],[x,y,z],palette['case'],rid);add(no,name+'-top',[2.42,1.22,.04],[x,y,z+.88],palette['stone'],rid)
    if no in (1,4):
     for side in [-1,1]:add(no,name+'-glass-side',[.022,1.2,.38],[x+side*1.2,y,z+.92],'glass',rid)
     add(no,name+'-glass-back',[2.4,.022,.38],[x,y+(.59 if positive else -.59),z+.92],'glass',rid)
     # Open front for clear merchandise visibility; taller goods use open plinths.
    add(no,name+'-merchandise-tray',[1,.7,.012],[x,y,z+.92],palette['fabric'] if no in (1,4) else palette['stone'],rid)
    support(room,'island-'+str(index),x,y,z+.932,.96,.65,ids)
 # Actual geometry costs include each transformed product instance.
 temporary=dict(contract=contract,parts=parts,curves=curves,doors=[],routes=[],shops=[])
 costs=estimate_triangles(temporary)
 # Door geometry is added outside this function; reserve >2,000 triangles per
 # floor, plus 24 sign panels already conservatively counted by estimate().
 remaining={no:97000-cost for no,cost in costs.items()}
 tenant_remaining={room['id']:max(remaining[room['floor']]/8,sum(sources[aid]['triangles'] for aid in data['tenants'][room['id']])) for room in contract['rooms']}
 assert min(remaining.values())>0,('No room for real products',costs)
 occupied={s['id']:[] for s in surfaces};tenant_counts={r['id']:0 for r in contract['rooms']}
 def place(surface,aid,index,bulk=False):
  row=products[aid];w,d,h=row['size'];x,y,z=surface['position_m'];sw,sd=surface['size_m'];hanging=surface['mode']=='hanging'
  # Slots use metre footprints and leave visible gaps. No random stretching.
  cols=min(10,max(1,int(sw/(w+.04))));rows=min(4,max(1,int(sd/(d+.04))))
  slots=sorted([((i-(cols-1)/2)*(w+.04),(j-(rows-1)/2)*(d+.04)) for i in range(cols) for j in range(rows)],key=lambda p:p[0]*p[0]+p[1]*p[1]) if not hanging else [(-.6,0),(.6,0)]
  if w>sw*.48 or d>sd*.7:slots=[(0,0)]
  for dx,dy in slots:
   if abs(dx)+w/2>sw/2-.01 or abs(dy)+d/2>sd/2-.01:continue
   if any(abs(dx-u[0])<(w+u[2])/2+.025 and abs(dy-u[1])<(d+u[3])/2+.025 for u in occupied[surface['id']]):continue
   no=surface['floor'];display=bulk and aid in display_sources;price=(display_sources if display else sources)[aid]['triangles']
   if min(remaining[no],tenant_remaining[surface['tenant']])<price:return False
   zz=z+.035-h if hanging else z+.0005
   yaw=0 if contract['rooms'][next(i for i,r in enumerate(contract['rooms']) if r['id']==surface['tenant'])]['bounds'][1]>0 else 180
   name=surface['tenant']+f'-product-{index:04d}-{aid}'
   q=dict(floor=no,asset='product-'+aid+('-display' if display else ''),name=name,pos=[x+dx,y+dy,zz],yaw=yaw,kind='merchandise',tenant_id=surface['tenant'],product_id=aid,support_id=surface['id'],size_m=list(row['size']),detail_level='display' if display else 'hero')
   curves.append(q);placements.append(q)
   for mi,member in enumerate(members(aid)):
    angle=math.radians(yaw);mx,my,mz=member['pos'];point=[q['pos'][0]+mx*math.cos(angle)-my*math.sin(angle),q['pos'][1]+mx*math.sin(angle)+my*math.cos(angle),q['pos'][2]+mz]
    part=add(no,name+'-structural-member-'+str(mi),member['size'],point,member['finish'],surface['tenant'],'merchandise_member');part['primitive']=member['primitive'];part['yaw']=yaw+member.get('yaw',0);part['product_id']=aid;part['placement_id']=name
   occupied[surface['id']].append((dx,dy,w,d));remaining[no]-=price;tenant_remaining[surface['tenant']]-=price;tenant_counts[surface['tenant']]+=1;return True
  return False
 # First guarantee every programme has each required product. Then fair rounds
 # fill surfaces until the measured face budget or usable footprint is full.
 for rid,ids in data['tenants'].items():
  for aid in ids:
   candidates=[s for s in surfaces if s['tenant']==rid and aid in s['products']]
   assert any(place(s,aid,len(placements)) for s in candidates),(rid,aid,'cannot place required merchandise')
 for cycle in range(48):
  added=0
  for room in contract['rooms']:
   rid=room['id'];ids=data['tenants'][rid];aid=ids[cycle%len(ids)]
   if 'folded-shirt' in ids:
    original=[i for i in ids if i!='folded-shirt'];aid='folded-shirt' if cycle%8!=7 else original[(cycle//8)%len(original)]
   candidates=sorted([s for s in surfaces if s['tenant']==rid and aid in s['products']],key=lambda s:len(occupied[s['id']]))
   if any(place(s,aid,len(placements),bulk=True) for s in candidates):added+=1
  if not added:break
 contract['merchandise']=dict(revision=1,unique_model_limit=LIMIT,unique_models=len(products),new_tripo_models=data['new_tripo_models'],reused_models=data['reused_models'],instances=len(placements),tenant_counts=tenant_counts,surfaces=surfaces,placements=placements,source_hashes={aid:r['sha256'] for aid,r in sources.items()},remaining_triangle_budget={str(k):v for k,v in remaining.items()},stocking_scope='Static stocked interiors; purchase transaction gameplay is not wired by this production step.')
 assert all(tenant_counts.values())
 contract['merchandise']['display_lod_hashes']={aid:r['sha256'] for aid,r in display_sources.items()}
 return contract['merchandise']
