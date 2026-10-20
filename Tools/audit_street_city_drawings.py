"""Inspect actual SVG door geometry and floor/map scale, independently of rendering."""
from pathlib import Path
import hashlib,json,math,re
import xml.etree.ElementTree as ET
from street_city_io import write_json,write_text
from label_street_city_floor_metrics import outer_rectangles,NS
from street_city_space_policy import segments

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'Docs/StreetSocialCity/production'

def inspect_door(group,scale,expected,origin,offset=(0,0)):
    if abs(float(group.get('data-door-width-m'))-expected['width_m'])>1e-8:raise ValueError('Door width differs')
    kind=expected['kind']
    if group.get('data-door-kind')!=kind:raise ValueError('Door operation differs')
    mask=list(group)[1]
    x=(float(mask.get('x1'))+float(mask.get('x2')))/2+offset[0]
    y=(float(mask.get('y1'))+float(mask.get('y2')))/2+offset[1]
    ex=origin[0]+expected['center_m'][0]*scale;ey=origin[1]-expected['center_m'][1]*scale
    if math.hypot(x-ex,y-ey)>1e-5:raise ValueError('Door position differs')
    span=math.hypot(float(mask.get('x2'))-float(mask.get('x1')),float(mask.get('y2'))-float(mask.get('y1')))
    if abs(span-expected['width_m']*scale)>1e-5:raise ValueError('Actual aperture line width differs')
    lines=[q for q in group if q.get('data-door-leaf')]
    arcs=[q for q in group if q.get('data-door-swing')]
    slides=[q for q in group if q.get('data-sliding-leaf')]
    count=0 if kind=='sliding' else 2 if kind=='double_hinged' else 1
    if len(arcs)!=count or len(lines)!=count or len(slides)!=(2 if kind=='sliding' else 0):raise ValueError('Missing or duplicate door leaf/swing')
    radius=expected['width_m']*scale/(2 if count==2 else 1)
    for line,arc in zip(lines,arcs):
        values=[float(x) for x in re.findall(r'[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?',arc.get('d'))]
        if len(values)!=9:raise ValueError('Not a quarter-circle arc command')
        cx,cy,rx,ry,rotation,large,sweep,px,py=values
        hx,hy=float(line.get('x1')),float(line.get('y1'))
        ox,oy=float(line.get('x2')),float(line.get('y2'))
        if max(abs(rx-radius),abs(ry-radius),abs(math.hypot(cx-hx,cy-hy)-radius),abs(math.hypot(ox-hx,oy-hy)-radius),abs(px-ox),abs(py-oy))>1e-5:raise ValueError('Door fan radius or leaf endpoint differs')
        if abs((cx-hx)*(ox-hx)+(cy-hy)*(oy-hy))>1e-5 or rotation!=0 or large!=0:raise ValueError('Door does not swing 90 degrees')
        cross=(cx-hx)*(oy-hy)-(cy-hy)*(ox-hx)
        if sweep!=(1 if cross>0 else 0):raise ValueError('Door fan swings around the wrong hinge')
    return count

def main():
    source=OUT/'assembly-manifest.json';m=json.loads(source.read_text('utf-8'))
    terrain=json.loads((OUT.parent/'world-terrain-layout.json').read_text('utf-8'))
    terrain={b['id']:b for b in terrain['features']};rooms=[r for r in m['layouts'] if r.get('room_id')];assets={a['id']:a for a in m['assets']}
    errors=[];files=[];panels=doors=swings=public=open_edges=0
    for b in (b for b in m['layouts'] if b.get('kind')=='building'):
        bid=b['building'];w,d=b['footprint_m']
        if [w,d]!=terrain[bid]['footprint_m']:errors.append(bid+': indoor/map footprint differs')
        for f in b['floors']:
            units=[r for r in rooms if r['building']==bid and r['floor']==f['floor']]
            expected={r['room_id'] for r in units if not r.get('open_plan')}
            expected|={q['id'] for q in f['annex_rooms']+f['core_annex_rooms']+f['support_rooms'] if not q.get('open_plan')}
            owners={q['owner'] for q in f['door_schedule']}
            if not expected<=owners:errors.append(bid+': enclosed rooms without doors '+str(expected-owners))
            required_ids={rid+'-door' for rid in expected}
            for unit in units+f['annex_rooms']+f['core_annex_rooms']:
                programme=unit.get('dimensioned_programme',unit)
                required_ids|={q['id']+'-door' for q in programme.get('internal_rooms',[])}
            for q in f['vertical_circulation']:
                if q['kind']=='stairs':required_ids.add(q['id']+'-fire-door')
                elif q['kind']=='lift':required_ids.add(q['id']+'-landing-door')
                elif q['kind']=='restroom':
                    required_ids.add(q['id']+'-entrance')
                    required_ids|={q['id']+f'-cubicle-{i}' for i,c in enumerate(q['fixtures']) if 'door_clear_width_m' in assets[c['asset_id']]}
            if f['floor']==1:required_ids|={q['id'] for q in b['entrances']}
            missing=required_ids-{q['id'] for q in f['door_schedule']}
            if missing:errors.append(bid+': door schedule omitted physical room/core '+str(missing))
            if f['corridor_width_m']!=2 or any(q['clear_width_m']!=2 for q in f['service_access_corridors']):errors.append(bid+': wrong corridor policy')
            for q in f['door_schedule']:
                width=2.4 if q['source']=='building_entrance' or (bid=='M1' and q['source']=='room') else 1.2
                if abs(q['width_m']-width)>1e-6:errors.append(q['id']+': wrong current door policy')
            public+=len(f['open_public_spaces'])
            for q in f['open_public_spaces']:
                if q['circulation_clear_width_m']!=2 or q['size_m'][1]<=2 or not q['interactions']:errors.append(q['id']+': unassigned open public space')
        for path,floors in [(OUT/'floor-layouts'/f'{bid}.svg',b['floors'])]+[(OUT/'floor-layouts'/f'{bid}-F{f["floor"]:02d}.svg',[f]) for f in b['floors']]:
            root=ET.parse(path).getroot();frames=outer_rectangles(root);groups={};open_lines={}
            from street_city_svg import ancestor_ids
            semantic_ancestors=ancestor_ids(root,lambda node:node.get('data-door-kind') or node.get('data-open-display-edge'))
            def walk(node,dx=0,dy=0):
                if id(node) not in semantic_ancestors:return
                if node.tag==NS+'g' and node.get('transform','').startswith('translate'):
                    tx,ty=map(float,re.findall(r'[-+\d.]+',node.get('transform')));dx+=tx;dy+=ty
                if node.get('data-door-kind'):groups[node.get('id')]=(node,(dx,dy))
                if node.get('data-open-display-edge'):open_lines[node.get('id')]=(node,(dx,dy))
                for child in node:walk(child,dx,dy)
            walk(root)
            expected_ids={q['id'] for f in floors for q in f['door_schedule']}
            if set(groups)!=expected_ids:errors.append(path.name+': missing/unexpected drawn doors '+str(set(groups)^expected_ids))
            for f,(x,y,pw,pd) in zip(floors,frames):
                panels+=1;scale=pw/w;origin=(x+pw/2,y+pd/2)
                if abs(pd/d-scale)>1e-6:errors.append(path.name+': unequal X/Y scales')
                for zone in f['open_activity_zones']:
                    for i,opening in enumerate(zone['open_edges']):
                        open_edges+=1
                        found=open_lines.get(zone['id']+f'-open-edge-{i}')
                        if found is None:errors.append(zone['id']+': missing drawn open edge');continue
                        node,offset=found
                        a,b=segments(zone['center_m'],zone['size_m'],zone['rotation_z_deg'],opening)
                        actual=math.hypot(float(node.get('x2'))-float(node.get('x1')),float(node.get('y2'))-float(node.get('y1')))
                        if abs(actual-opening['width_m']*scale)>1e-5:errors.append(zone['id']+': incorrect actual open span')
                        for index,point in enumerate((a,b),1):
                            if abs(float(node.get(f'x{index}'))+offset[0]-(origin[0]+point[0]*scale))>1e-5 or abs(float(node.get(f'y{index}'))+offset[1]-(origin[1]-point[1]*scale))>1e-5:errors.append(zone['id']+': misplaced actual open edge')
                        if zone['id']+'-door' in groups:errors.append(zone['id']+': open zone incorrectly has a door')
                for q in f['door_schedule']:
                    try:
                        node,offset=groups[q['id']];swings+=inspect_door(node,scale,q,origin,offset);doors+=1
                    except (KeyError,ValueError) as e:errors.append(path.name+'/'+q['id']+': '+str(e))
            files.append(dict(file=path.relative_to(OUT).as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    report=dict(status='passed' if not errors else 'failed',source_manifest_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),svg_files=len(files),floor_panels=panels,drawn_doors=doors,quarter_circle_swings=swings,open_public_spaces=public,drawn_open_edges=open_edges,door_width_m=1.2,main_door_width_m=2.4,corridor_clear_width_m=2,files=files,errors=errors,scope='dimensioned architectural drawings and planned openings; moving mesh/collision approval is separate')
    write_json(OUT/'drawing-acceptance.json',report)
    write_text(OUT/'drawing-acceptance.md',f'# 門表、門扇與圖面驗收\n\n結果：{report["status"]}。{len(files)}份SVG、{panels}個樓層外框、{doors}個門符號、{swings}段90度門扇弧線。\n\n一般門1.2m；建築／百貨店面正門2.4m；走廊淨寬2m。{public}處開放公共門廳另列用途、尺寸與通道。每層外框與地圖長寬相同；門表、實際開口線、門扇長度、弧線半徑與方向逐項核對。\n\n此項不代表動態門扇或遊戲碰撞已驗收。\n'+''.join('- '+e+'\n' for e in errors))
    print(json.dumps({k:v for k,v in report.items() if k not in ('files','errors')},ensure_ascii=False))
    if errors:print('\n'.join(errors[:20]));raise SystemExit(1)

if __name__=='__main__':main()
