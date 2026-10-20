"""Dimension and scale annotations tied to each floor's actual SVG footprint.

Can refresh existing drawings without changing the solved city or mesh sources.
The compiler also uses the same annotation renderer for future rebuilds.
"""
from pathlib import Path
import hashlib,html,json,re
import xml.etree.ElementTree as ET
from street_city_io import write_json,write_text

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'Docs/StreetSocialCity/production'
NS='{http://www.w3.org/2000/svg}'
PREFIX='street-city-floor-metrics-'


def number(value):return f'{value:g}'


def floor_metrics_svg(building,floor,footprint,scale,left,top):
    w,d=footprint;right=left+w*scale;bottom=top+d*scale
    gid=PREFIX+building+f'-F{floor:02d}'
    pieces=[f'<g id="{gid}" data-building="{building}" data-floor="{floor}" data-ew-width-m="{w}" data-ns-length-m="{d}" data-svg-units-per-m="{scale}" fill="#203c45" font-family="Microsoft JhengHei,sans-serif">',
        '<title>建築外框含牆尺寸；東西寬與南北長。比例尺依本層實際繪圖比例計算。</title>']
    def line(x1,y1,x2,y2,extra=''):
        pieces.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="#203c45" stroke-width="1.3" {extra}/>')
    def text(x,y,value,extra=''):
        pieces.append(f'<text x="{x}" y="{y}" font-size="12" {extra}>{html.escape(value)}</text>')
    # Outside the finished footprint, with space between the floor title and wall.
    y=top-7;line(left,y,right,y,'data-measurement="east-west"')
    for x in (left,right):line(x,y-4,x,y+4)
    text((left+right)/2,y-6,f'東西寬 {number(w)} m（含牆）','text-anchor="middle"')
    x=right+12;line(x,top,x,bottom,'data-measurement="north-south"')
    for y in (top,bottom):line(x-4,y,x+4,y)
    text(right+32,(top+bottom)/2,f'南北長 {number(d)} m（含牆）',
         f'text-anchor="middle" transform="rotate(-90 {right+32} {(top+bottom)/2})"')
    text(left,bottom+22,'比例尺｜實際公尺；與本層平面同尺度')
    x=left;y=bottom+39
    for metres in (1,5,10):
        length=metres*scale
        pieces.append(f'<rect x="{x}" y="{y-3}" width="{length}" height="6" fill="#203c45" data-scale-metres="{metres}"/>')
        for end in (x,x+length):line(end,y-6,end,y+6)
        text(x+length/2,y+20,f'{metres} m','text-anchor="middle"')
        x+=max(length,24)+42
    pieces.append('</g>')
    return ''.join(pieces)


def outer_rectangles(root):
    """Find real building outlines, including standalone translated floor groups."""
    result=[]
    # Furniture, icons and door arcs may use arbitrary drawing transforms.
    # Validate transforms only on the actual floor-outline ancestor chain.
    # Skipping a decoration must never skip an outline hidden beneath it.
    from street_city_svg import ancestor_ids
    outline_ancestors=ancestor_ids(root,lambda node:node.tag==NS+'rect' and node.get('fill')=='#f5f3ed' and node.get('stroke')=='#203c45')
    def walk(node,dx=0.,dy=0.):
        if id(node) not in outline_ancestors:return
        if node.get('id','').startswith(PREFIX):return
        transform=node.get('transform')
        if transform:
            match=re.fullmatch(r'translate\(\s*([-+\d.eE]+)[ ,]+([-+\d.eE]+)\s*\)',transform)
            if not match:raise ValueError('Unexpected floor ancestor transform: '+transform)
            dx+=float(match[1]);dy+=float(match[2])
        if node.tag==NS+'rect' and node.get('fill')=='#f5f3ed' and node.get('stroke')=='#203c45':
            result.append((float(node.get('x','0'))+dx,float(node.get('y','0'))+dy,float(node.get('width')),float(node.get('height'))))
        for child in node:walk(child,dx,dy)
    walk(root)
    return result


def refresh(svg,building,floors):
    # Only replace our own flat annotation groups; retain original geometry bytes.
    svg=re.sub(r'<g id="'+PREFIX+r'[^"]+"[^>]*>.*?</g>','',svg,flags=re.S)
    root=ET.fromstring(svg);outlines=outer_rectangles(root)
    if len(outlines)!=len(floors):raise ValueError(building['building']+': floor outline count differs')
    additions=[];w,d=building['footprint_m']
    for floor,(x,y,pw,pd) in zip(floors,outlines):
        scale=pw/w
        if abs(pd/d-scale)>1e-6:raise ValueError('Floor exterior has unequal axis scales')
        additions.append(floor_metrics_svg(building['building'],floor['floor'],[w,d],scale,x,y))
    return svg.replace('</svg>',''.join(additions)+'</svg>')


def inspect(svg,building,floors):
    root=ET.fromstring(svg);w,d=building['footprint_m'];outlines=outer_rectangles(root)
    groups=[q for q in root.iter(NS+'g') if q.get('id','').startswith(PREFIX)]
    if len(groups)!=len(floors):raise ValueError('Missing per-floor dimensions')
    expected_ids={PREFIX+building['building']+f'-F{f["floor"]:02d}' for f in floors}
    if {g.get('id') for g in groups}!=expected_ids:raise ValueError('Floor annotation identity differs')
    byid={g.get('id'):g for g in groups}
    for floor,(x,y,pw,pd) in zip(floors,outlines):
        g=byid[PREFIX+building['building']+f'-F{floor["floor"]:02d}'];scale=pw/w
        if float(g.get('data-ew-width-m'))!=w or float(g.get('data-ns-length-m'))!=d:raise ValueError('Building dimensions differ')
        bars={int(q.get('data-scale-metres')):q for q in g.iter(NS+'rect') if q.get('data-scale-metres')}
        if set(bars)!={1,5,10}:raise ValueError('Missing 1/5/10 metre bars')
        for metres,bar in bars.items():
            if abs(float(bar.get('width'))-metres*scale)>1e-6:raise ValueError('Scale bar is not the actual floor scale')
        lines={q.get('data-measurement'):q for q in g.iter(NS+'line') if q.get('data-measurement')}
        if abs(float(lines['east-west'].get('x2'))-float(lines['east-west'].get('x1'))-pw)>1e-6:raise ValueError('Width dimension span differs')
        if abs(float(lines['north-south'].get('y2'))-float(lines['north-south'].get('y1'))-pd)>1e-6:raise ValueError('Length dimension span differs')
    return len(groups)


def main():
    source=OUT/'assembly-manifest.json';manifest=json.loads(source.read_text('utf-8'))
    buildings=[b for b in manifest['layouts'] if b.get('kind')=='building']
    plan=json.loads((OUT/'effective-city-plan.json').read_text('utf-8'));names={b['id']:b['name'] for b in plan['buildings']}
    records=[];panels=0;rows=[]
    for building in buildings:
        bid=building['building'];w,d=building['footprint_m']
        files=[(OUT/'floor-layouts'/f'{bid}.svg',building['floors'])]+[(OUT/'floor-layouts'/f'{bid}-F{f["floor"]:02d}.svg',[f]) for f in building['floors']]
        for path,floors in files:
            before=path.read_text('utf-8');after=refresh(before,building,floors)
            panels+=inspect(after,building,floors)
            if after!=before:write_text(path,after)
            records.append(dict(file=path.relative_to(OUT).as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),floor_panels=len(floors)))
        rows.append(dict(building=bid,name=names[bid],east_west_width_m=w,north_south_length_m=d,floors=len(building['floors'])))
    digest=hashlib.sha256(source.read_bytes()).hexdigest()
    write_json(OUT/'floor-metrics-acceptance.json',dict(status='passed',source_manifest_sha256=digest,buildings=len(buildings),svg_files=len(records),floor_panels=panels,scale_metres=[1,5,10],dimension_basis='finished exterior footprint including walls; north up',files=records))
    write_json(OUT/'building-dimensions.json',dict(source_manifest_sha256=digest,units='metres',buildings=rows))
    lines=['# 全棟建築長寬表','','全部為含牆外框，北在上；東西寬對應X、南北長對應Y。室內配置沿用同一外框，再扣牆厚。工程一館為U形，此表列其整體外接長寬。','', '| 建築 | 名稱 | 東西寬（m） | 南北長（m） | 樓層 |','|---|---|---:|---:|---:|']
    lines += [f'| [{r["building"]}](floor-layouts/{r["building"]}.svg) | {r["name"]} | {number(r["east_west_width_m"])} | {number(r["north_south_length_m"])} | {r["floors"]} |' for r in rows]
    write_text(OUT/'building-dimensions.md','\n'.join(lines)+'\n')
    index=OUT/'index.html'
    if index.is_file():
        page=index.read_text('utf-8');link='<a href="building-dimensions.md">全棟建築長寬表</a>'
        if link not in page:write_text(index,page.replace('<nav>','<nav>'+link))
    print(json.dumps(dict(status='passed',buildings=len(buildings),svg_files=len(records),floor_panels=panels,scale_metres=[1,5,10])))


if __name__=='__main__':main()
