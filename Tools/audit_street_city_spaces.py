"""Complete per-building space inventory and enclosure/open geometry acceptance."""
from pathlib import Path
from collections import Counter
import json,hashlib
from street_city_io import write_json,write_text
from street_city_space_policy import enclosure,PRIVATE
from street_city_geometry import main_hall
from street_city_open_access import public_access_paths

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'Docs/StreetSocialCity/production'
REFERENCES=[dict(title='國立公共資訊圖書館樓層與空間',url='https://www.nlpi.edu.tw/StaticPage/main-floors',use='開架閱覽與流通服務屬公共區；討論小間與自修空間另設管理和隔間'),dict(title='圓山大飯店場地與樓層',url='https://www.grand-hotel.org/EN/official/about-floor.aspx?gh=TP',use='接待與餐飲公共用途、私人包廂與會議用途分別規畫')]

def main():
    source=OUT/'assembly-manifest.json';m=json.loads(source.read_text('utf-8'));plan=json.loads((OUT/'effective-city-plan.json').read_text('utf-8'))
    names={r['id']:r for r in plan['rooms']};assets={a['id']:a for a in m['assets']};rows=[];errors=[];totals=Counter();buildings=[];access_floor_checks=0
    def add(b,f,identity,name,kind,size,mode,reason,openings=None):
        q=dict(building=b['building'],floor=f['floor'],id=identity,name=name,kind=kind,size_m=size,mode=mode,reason=reason,open_edges=openings or [],reference_ids=b['reference_contract']['source_ids'])
        rows.append(q);totals[mode]+=1
    for b in (b for b in m['layouts'] if b.get('kind')=='building'):
        before=len(rows)
        for f in b['floors']:
            access_floor_checks+=1
            access_paths=public_access_paths(b,f)
            errors.extend(rid+': no continuous two-metre public access' for rid,path in access_paths.items() if not path)
            units=[r for r in m['layouts'] if r.get('room_id') and r['building']==b['building'] and r['floor']==f['floor']]
            zones={z['id']:z for z in f['open_activity_zones']}
            for q in units+f['annex_rooms']+f['core_annex_rooms']:
                rid=q.get('room_id',q.get('id'));name=names[rid]['name'] if q.get('room_id') else q['name'];size=q.get('clear_size_m',q.get('size_m'))
                policy=q.get('enclosure_policy',dict(mode='enclosed',reason='共用後勤設施或專用運動分隔'))
                opened=bool(q.get('open_plan'))
                if opened:
                    shell=assets[rid+'-shell'];edges=shell.get('open_display_edges',[])
                    if not edges or max(e['width_m'] for e in edges)<2-1e-6:errors.append(rid+': no usable open connection')
                    if rid+'-door' in assets:errors.append(rid+': obsolete open-zone door asset')
                    if rid not in zones or edges!=zones[rid]['open_edges']:errors.append(rid+': geometry and open zone differ')
                    if any(abs(e['height_m']-size[2])>1e-6 for e in edges):errors.append(rid+': open edge has an overhead wall/lintel')
                elif assets[rid+'-shell'].get('open_display_edges'):errors.append(rid+': enclosed room has an open edge')
                if q.get('room_id'):
                    desired,_=enclosure(names[rid]['type'],name,bid=b['building'])
                    if b.get('layout_profile')=='small_shop_v2':
                        from street_city_small_shop import PROFILES
                        desired=PROFILES[b['building']]['open_floors'][f['floor']-1]
                    if opened!=desired:errors.append(rid+': intended enclosure not implemented')
                elif q.get('owner_unit') in names:
                    parent=names[q['owner_unit']]
                    if parent['type'] in ('retail','fashion','food','cafe','exhibit','study','service','game') and any(t in parent['name'] for t in PRIVATE) and opened:errors.append(rid+': private activity extension lost privacy')
                add(b,f,rid,name,'activity_zone' if opened else 'room',size,'open' if opened else 'enclosed',policy['reason'],zones.get(rid,{}).get('open_edges'))
                rows[-1]['public_access_path']=access_paths.get(rid,[])
                programme=q.get('dimensioned_programme',q)
                for sub in programme.get('internal_rooms',[]):
                    owner=sub.get('geometry_owner_asset_id')
                    if (sub['id']+'-partitions' not in assets and not (owner in assets and assets[owner].get('control_components'))) or sub['id']+'-door' not in assets:errors.append(sub['id']+': missing private/support partition or door')
                    add(b,f,sub['id'],sub['name'],'internal_room',sub['clear_size_m'],'enclosed','隱私或後勤功能的獨立隔間')
            for q in f.get('small_shop_design',{}).get('private_rooms',[]):
                if any(r['id']==q['id'] for r in rows):continue
                a0,b0,c0,d0=q['bounds_m']
                add(b,f,q['id'],q['name'],'internal_room',[c0-a0,d0-b0,f['height_m']-.3],'enclosed','店屋睡眠隱私隔間；牆與門洞由整棟尺寸結構統一建模')
                rows[-1]['door_width_m']=q['door']['width_m']
            for q in f['support_rooms']:add(b,f,q['id'],q['name'],'support',q['size_m'],'enclosed','設備、清潔、庫房等後勤設施')
            for q in f['vertical_circulation']:add(b,f,q['id'],q['kind'],'core',q['size_m'],'open' if q['kind']=='lobby' else 'enclosed','開放電梯廳' if q['kind']=='lobby' else '廁所隱私或樓梯／電梯井道防護邊界')
            hall=main_hall(b['building'],b['footprint_m'][0],f['corridor_y_m'],f['z_m'],f['height_m']-.4)
            add(b,f,b['building']+f'-F{f["floor"]:02d}-main-route','主要通道／公共門廳','circulation',hall['size_m'],'open','連續公共動線，淨寬2m；開放門廳另列活動用途')
            for q in f['service_access_corridors']+f['branch_corridors']:add(b,f,q.get('id',b['building']+f'-F{f["floor"]:02d}-branch{len(rows)}'),'連通通道','circulation',q['size_m'],'open','公共動線淨寬2m；不是需要建成實心的房間')
        subset=rows[before:];buildings.append(dict(building=b['building'],spaces=len(subset),open=sum(r['mode']=='open' for r in subset),enclosed=sum(r['mode']=='enclosed' for r in subset)))
    if len({r['id'] for r in rows})!=len(rows):errors.append('duplicate inventory identity')
    report=dict(status='passed' if not errors else 'failed',source_manifest_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),buildings=len(buildings),totals=dict(totals),open_activity_zones=sum(len(f['open_activity_zones']) for b in m['layouts'] if b.get('kind')=='building' for f in b['floors']),building_summary=buildings,spaces=rows,references=REFERENCES,errors=errors,scope='planned enclosure and exact structural control apertures; not final AI meshes or fire engineering approval')
    report['public_access_floor_checks']=access_floor_checks
    report['connected_open_activity_zones']=sum(r['kind']=='activity_zone' and bool(r['public_access_path']) for r in rows)
    write_json(OUT/'space-enclosure-acceptance.json',report)
    lines=['# 全城逐層空間開放／封閉盤點','',f'結果：{report["status"]}。{len(buildings)}棟、{len(rows)}個主要分區、附屬隔間、公共核心及通道記錄。開放活動分區{report["open_activity_zones"]}處。','', '開放活動區保留尺寸與互動分區，移除相鄰通道或同用途分區之間的實體牆段與內門；外牆、不同店戶界牆與隱私／衛生／隔音／安全隔間保留。不是把所有房間都拆掉。', '', '## 台灣場地參考','']
    lines += [f'- [{r["title"]}]({r["url"]})：{r["use"]}。此處為依來源用途作出的遊戲空間設計判斷，並非照抄館舍施工圖。' for r in REFERENCES]
    lines+=['','## 公共動線連通驗收','',f'{access_floor_checks}層已檢查；{report["connected_open_activity_zones"]}處開放活動分區均有通往主要公共通道的路徑。相鄰分區必須兩側都有相向開口，連通寬度至少2m（發布尺寸容差0.001m）；僅通往另一孤立分區或不連主通道的支線不能算通過。下表路徑為配置幾何驗收，遊戲導航與家具碰撞仍另驗。']
    lines+=['','## 每棟總計','','| 建築 | 空間記錄 | 開放 | 封閉 |','|---|---:|---:|---:|']+[f'| [{r["building"]}](floor-layouts/{r["building"]}.svg) | {r["spaces"]} | {r["open"]} | {r["enclosed"]} |' for r in buildings]
    lines+=['','## 全部逐層記錄','','| 建築／層 | ID／用途 | 長寬高m | 決定 | 理由 | 公共動線路徑 |','|---|---|---|---|---|---|']+[f'| {r["building"]}/{r["floor"]}F | {r["id"]}／{r["name"]} | {"×".join(f"{v:.2f}" for v in r["size_m"])} | {"開放" if r["mode"]=="open" else "封閉"} | {r["reason"]} | {" → ".join(r.get("public_access_path",[])) or "—"} |' for r in rows]
    write_text(OUT/'space-enclosure-inventory.md','\n'.join(lines)+'\n')
    print(json.dumps({k:report[k] for k in ('status','buildings','totals','open_activity_zones')},ensure_ascii=False))
    if errors:print('\n'.join(errors[:15]));raise SystemExit(1)

if __name__=='__main__':main()
