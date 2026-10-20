"""Produce a current, bounded acceptance receipt from measured evidence.

Does not regenerate geometry, contact services or declare the city complete.
Any stale planning/control report or changed approved GLB fails the receipt.
"""
from pathlib import Path
import csv
import hashlib
import json
import sys
import unittest
import argparse
from html.parser import HTMLParser
from street_city_io import write_json,write_text
from street_city_model_gate import inspect_glb,approval_errors
from street_city_catalog import load_asset_catalog
from street_city_generation_state import resume_state

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'Docs/StreetSocialCity/production'
CONTROL=ROOT/'output/street-social-city-production'
LIB=ROOT/'Content/IndependentResources/StreetSocialCity'


def read(path):return json.loads(path.read_text('utf-8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


class EvidenceLinks(HTMLParser):
    def __init__(self):super().__init__();self.targets=[]
    def handle_starttag(self,tag,attrs):
        self.targets.extend(value for key,value in attrs if key in ('src','href') and value)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--planning-only',action='store_true');args=parser.parse_args()
    if args.planning_only:
        from verify_street_city_planning_delivery import main as verify_planning
        return verify_planning()
    from audit_street_city_spaces import main as audit_spaces
    audit_spaces()
    manifest=read(OUT/'assembly-manifest.json');digest=sha(OUT/'assembly-manifest.json')
    report_files={name:OUT/name for name in (
        'site-acceptance.json','layout-acceptance.json','functional-acceptance.json',
        'scale-audit.json','modular-acceptance.json','modular-geometry-acceptance.json',
        'asset-library-acceptance.json','floor-metrics-acceptance.json','drawing-acceptance.json','space-enclosure-acceptance.json')}
    report_files.update({name:CONTROL/name for name in (
        'control-assemblies-acceptance.json','control-glb-acceptance.json')})
    reports={name:read(path) for name,path in report_files.items()}
    for name,digest_expected in reports['site-acceptance.json']['geographic_files_sha256'].items():
        if sha(OUT.parent/name)!=digest_expected:raise SystemExit(name+': changed geographic artifact')
    for name,report in reports.items():
        if report.get('status')!='passed':raise SystemExit(name+': not passed')
        if report.get('source_manifest_sha256',report.get('manifest_sha256'))!=digest:
            raise SystemExit(name+': stale source manifest')
    buildings=[q for q in manifest['layouts'] if q.get('kind')=='building']
    rooms=[q for q in manifest['layouts'] if q.get('room_id')]
    actual=reports['control-glb-acceptance.json']
    if actual.get('entrance_clearance_checks')!=reports['site-acceptance.json']['opposing_storefront_entrances']:
        raise SystemExit('Actual control storefront doorway coverage differs')
    spaces=reports['space-enclosure-acceptance.json']
    if spaces['connected_open_activity_zones']!=spaces['open_activity_zones'] or spaces['public_access_floor_checks']!=sum(len(b['floors']) for b in buildings):raise SystemExit('Open-zone public access coverage differs')
    expected_edges=sum(len(z['open_edges']) for b in buildings for f in b['floors'] for z in f['open_activity_zones'])
    if actual.get('open_edge_clearance_checks')!=expected_edges:raise SystemExit('Actual open-zone wall clearance coverage differs')
    controls=reports['control-assemblies-acceptance.json']
    if {q['building'] for q in controls['buildings']}!={q['building'] for q in buildings}:
        raise SystemExit('Control building coverage differs')
    if actual['rooms']!=len(rooms):raise SystemExit('Control room count differs')
    for item in controls['buildings']:
        if sha(ROOT/item['glb'])!=item['sha256']:raise SystemExit(item['building']+': changed control GLB')
    metrics=reports['floor-metrics-acceptance.json']
    from audit_street_city_drawings import main as audit_drawings
    audit_drawings()
    drawings=read(OUT/'drawing-acceptance.json')
    if drawings['status']!='passed' or drawings['source_manifest_sha256']!=digest:raise SystemExit('Architectural drawings failed')
    from label_street_city_floor_metrics import inspect as inspect_floor_metrics
    by_building={b['building']:b for b in buildings}
    for item in metrics['files']:
        path=OUT/item['file']
        if sha(path)!=item['sha256']:raise SystemExit(item['file']+': changed floor drawing')
        bid=path.stem.split('-F')[0];building=by_building[bid]
        floors=building['floors'] if '-F' not in path.stem else [f for f in building['floors'] if f['floor']==int(path.stem.split('-F')[1])]
        inspect_floor_metrics(path.read_text('utf-8'),building,floors)
    expected_files={f'floor-layouts/{b["building"]}.svg' for b in buildings}|{f'floor-layouts/{b["building"]}-F{f["floor"]:02d}.svg' for b in buildings for f in b['floors']}
    if {q['file'] for q in metrics['files']}!=expected_files:raise SystemExit('Building/floor metric drawing coverage differs')
    effective=read(OUT/'effective-city-plan.json');room_by_id={r['id']:r for r in effective['rooms']}
    with (OUT.parent/'room-schedule.csv').open(encoding='utf-8-sig',newline='') as stream:
        schedule=list(csv.DictReader(stream))
    page=(OUT.parent/'index.html').read_text('utf-8')
    start=page.index('const plan=')+len('const plan=');end=page.index(';const $=id=>',start)
    catalog=json.loads(page[start:end])
    for collection in (schedule,catalog['rooms']):
        if {r['id'] for r in collection}!=set(room_by_id):raise SystemExit('Catalog coverage differs')
        for r in collection:
            size=[float(x) for x in r['size_m'].split(' / ')] if isinstance(r['size_m'],str) else r['size_m']
            if any(abs(a-b)>.001 for a,b in zip(size,room_by_id[r['id']]['size_m'])):
                raise SystemExit(r['id']+': catalog dimensions differ')
    for r in catalog['rooms']:
        if r['required_props']!=room_by_id[r['id']]['required_props'] or r.get('enclosure_policy')!=room_by_id[r['id']].get('enclosure_policy'):raise SystemExit(r['id']+': catalog programme/enclosure differs')
    for line in (OUT.parent/'tripo-room-briefs.jsonl').read_text('utf-8').splitlines():
        q=json.loads(line);r=room_by_id[q['room_id']]
        if q['required_props']!=r['required_props'] or q.get('open_plan')!=r.get('open_plan'):raise SystemExit(q['room_id']+': design brief programme/enclosure differs')
    ledger=read(OUT/'generation-ledger.json');specs=load_asset_catalog(OUT)
    approved=[]
    for entry in ledger['entries']:
        if not entry.get('actual_model_approved'):continue
        spec=specs[entry['asset_id']]
        errors=approval_errors(spec,inspect_glb(LIB/spec['expected_file']),read(LIB/spec['approval_file']),ledger,ROOT)
        if errors:raise SystemExit(entry['asset_id']+': '+'; '.join(errors))
        approved.append(entry['asset_id'])
    queue=read(OUT/'production-queue.json')
    if queue['source_manifest_sha256']!=digest or set(queue['verified_models'])!=set(approved):
        raise SystemExit('Actual production queue is stale')
    for item in queue['queue']:
        if item.get('recovery') != resume_state(specs[item['asset_id']],ledger,ROOT):
            raise SystemExit('Generation recovery evidence is stale: '+item['asset_id'])
    parser=EvidenceLinks();parser.feed((OUT/'generation-progress.html').read_text('utf-8'))
    for target in parser.targets:
        if target.startswith(('https://','http://','#')):continue
        if not (OUT/target).resolve().is_file():raise SystemExit('Missing local production evidence: '+target)
    submitted=sum(bool(e.get('tripo_model_id')) for e in ledger['entries'])
    if f'{submitted}種零件已送生成；{len(approved)}種通過靜態模型驗收' not in (OUT/'index.html').read_text('utf-8'):
        raise SystemExit('Production index model counts are stale')
    partial=[]
    for path in sorted((CONTROL/'room-progress').glob('*/progress.json')):
        proof=read(path)
        if proof['source_manifest_sha256']!=digest:continue
        for item in proof['placed_verified_instances']:
            if item['asset_id'] not in approved or not item['actual_vertex_placement_passed']:
                raise SystemExit('Partial preview has an unverified placement')
            if item['model_sha256']!=sha(LIB/specs[item['asset_id']]['expected_file']):
                raise SystemExit('Partial preview uses an old approved model')
        partial.append(dict(room=proof['room_id'],verified_instances=len(proof['placed_verified_instances']),complete=False))
    suite=unittest.defaultTestLoader.discover(str(ROOT/'Tools/tests'),pattern='test_street_city*.py')
    tests=unittest.TextTestRunner(verbosity=1).run(suite)
    if not tests.wasSuccessful():raise SystemExit('Current city regression failed')
    site=reports['site-acceptance.json'];functions=reports['functional-acceptance.json']
    receipt=dict(status='current_features_passed',scope='current planning and control geometry plus individually approved static Tripo parts',
        source_manifest_sha256=digest,unit_tests=tests.testsRun,
        world_size_m=site['world_size_m'],linear_scale_ratio=site['linear_scale_ratio'],area_scale_ratio=site['area_scale_ratio'],
        buildings=len(buildings),floors=sum(len(q['floors']) for q in buildings),indoor_units=len(rooms),
        outdoor_units=len(room_by_id)-len(rooms),main_interactive_units=len(room_by_id),
        shops=site['shops'],office_towers=site['offices'],mall_footprint_m=site['mall_footprint_m'],
        opposing_storefront_entrances=site['opposing_storefront_entrances'],shared_student_lanes=site['shared_student_lanes'],
        site_checks=site['checks'],control_glb_checks=actual['checks'],catalog_and_csv_dimensions_verified=len(room_by_id),
        dimensioned_floor_svg_files=metrics['svg_files'],dimensioned_floor_panels=metrics['floor_panels'],floor_scale_metres=metrics['scale_metres'],
        door_clear_width_m=drawings['door_width_m'],main_door_clear_width_m=drawings['main_door_width_m'],corridor_clear_width_m=drawings['corridor_clear_width_m'],drawn_doors=drawings['drawn_doors'],quarter_circle_swings=drawings['quarter_circle_swings'],open_public_spaces=drawings['open_public_spaces'],
        open_activity_zones=spaces['open_activity_zones'],connected_open_activity_zones=spaces['connected_open_activity_zones'],public_access_floor_checks=spaces['public_access_floor_checks'],space_inventory_records=len(spaces['spaces']),space_inventory_totals=spaces['totals'],open_edge_clearance_checks=actual['open_edge_clearance_checks'],
        functional_totals=functions['totals'],reports={name:'passed' for name in reports},
        actual_tripo_models_approved=len(approved),approved_static_assets=approved,
        required_distinct_model_files=queue['required_distinct_model_files'],ready_primary_rooms=queue['ready_primary_rooms'],
        submitted_tripo_assets=sum(bool(e.get('tripo_model_id')) for e in ledger['entries']),partial_room_previews=partial,
        full_city_models_complete=False,runtime_navigation_verified=False,runtime_collision_and_performance_verified=False)
    previous=OUT/'current-feature-acceptance.json'
    if previous.is_file():
        old=read(previous)
        if old.get('source_manifest_sha256')!=digest:
            archive=OUT/'acceptance-history';archive.mkdir(exist_ok=True)
            tag=old.get('source_manifest_sha256','unknown')[:12]
            for name in ('current-feature-acceptance.json','current-acceptance.md'):
                source=OUT/name;dest=archive/f'{tag}-{name}'
                if source.is_file() and not dest.exists():dest.write_bytes(source.read_bytes())
    write_json(previous,receipt)
    lines=['# 當前配置與實際製作驗收','',f'來源 SHA-256：`{digest}`。','',
        f'全城 {len(buildings)} 棟、{receipt["floors"]} 層、{len(rooms):,} 個室內＋{receipt["outdoor_units"]} 個戶外＝{len(room_by_id):,} 個主要互動單元。',
        f'世界 {site["world_size_m"]}m，長寬比例 {site["linear_scale_ratio"]}；建築與人尺度設施維持真實公尺。{site["shops"]} 棟學生街店屋、{site["mall_footprint_m"]}m 百貨及鄰近 {site["offices"]} 棟辦公樓。','',
        '## 已通過的當前功能','',
        f'- {tests.testsRun} 項單元回歸通過。',
        f'- {site["checks"]:,} 項街區檢查通過，包含外框、道路、球場、商業街覆蓋及百貨街廓。',
        f'- 48棟商店的 {site["opposing_storefront_entrances"]} 個沿街入口面向 {site["shared_student_lanes"]} 條共用步行街；兩排店面正門相向。門位與1F圖、門前步道、立面開口工單及組裝座標一致，並以實際控制GLB檢查入口內側淨空。',
        f'- 全部 {len(buildings)} 棟控制 GLB 通過 {actual["checks"]:,} 項實際頂點與座標檢查；產物 SHA 逐棟比對一致。',
        '- 逐層外框／牆厚／完整分隔／門位／電梯／樓梯／公共廁所／至少 2m 通路及用途設備配置檢查通過。',
        f'- [逐層空間盤點](space-enclosure-inventory.md)：{len(spaces["spaces"]):,}筆用途與開放／封閉理由。{spaces["open_activity_zones"]:,}處開放活動分區移除實體隔牆與內門；{actual["open_edge_clearance_checks"]:,}段開口通過實際控制GLB結構淨空檢查。客房、衛浴、更衣、廚房及需要隔離的用途保留隔間。',
        f'- {spaces["public_access_floor_checks"]}層公共動線連通檢查通過；全部{spaces["connected_open_activity_zones"]:,}處開放活動分區能經至少2m寬的相向開口與連續通道接到主要公共動線，逐區路徑已記錄。',
        f'- 宿舍 {functions["totals"]["dorm_study_desks"]:,} 張書桌補齊同數座椅；逐房檢查唯一配對、設備碰撞與通路。',
        f'- HTML 與 CSV 全部 {len(room_by_id):,} 筆尺寸逐一核對有效規畫，誤差不超過 0.001m。',
        f'- {metrics["svg_files"]} 份逐層總覽／單層 SVG、{metrics["floor_panels"]} 個樓層圖框均有東西寬／南北長含牆尺寸，以及依實際圖面比例繪製的 1m、5m、10m 獨立比例尺。',
        f'- 一般門1.2m、建築／百貨店面正門2.4m、走廊淨寬2m。{drawings["drawn_doors"]:,}個門符號及{drawings["quarter_circle_swings"]:,}段90度開啟弧線通過實際SVG檢查；電梯為滑門。{drawings["open_public_spaces"]}處開放公共門廳另列用途與尺寸。','',
        '## 真實 Tripo 模型進度','',
        f'{len(approved)} 種實際下載模型通過靜態零件驗收；目前為：'+(', '.join(approved) or '無')+'。',
        '通過的資產已登錄正式資產庫；嚴格組裝器仍核對原檔來源、模型 SHA、真實尺寸、品質紀錄及參考圖 SHA。局部預覽不代表完整組裝。']
    for entry in ledger['entries']:
        lines.append(f'- {entry["asset_id"]}：{entry.get("tripo_status")}。{entry.get("blocking_reason","")}')
        if entry.get('material_origin'):lines.append('  - 材質來源：'+entry['material_origin'])
    for proof in partial:
        lines.append(f'- {proof["room"]}：{proof["verified_instances"]} 次真實零件擺放已量測座標及尺寸；灰色牆地仍為控制幾何，房間未完成。')
    lines+=['',f'完整房間正式組裝就緒 {queue["ready_primary_rooms"]} 間；展開配方仍需 {queue["required_distinct_model_files"]:,} 種獨立模型。數量不代表已生成。',
        '', '## 尚未驗收的範圍','',
        '全城 Tripo 模型、扶手、動態門扇／電梯、窗表、地形高度與入口高差、遊戲碰撞、導航及效能仍未完成。靜態零件通過不等於房間或遊戲完成。','',
        '全店研究與圖稿：[store-review.html](store-review.html)；店面本輪驗收：[store-redesign-acceptance.md](store-redesign-acceptance.md)。','',
        '總圖：[world-terrain.svg](../world-terrain.svg)；逐層配置：[review.html](review.html)；真實製作：[generation-progress.html](generation-progress.html)；待件：[production-queue.md](production-queue.md)；錯誤：[ERRORS_AND_PREVENTION.md](../ERRORS_AND_PREVENTION.md)。','',
        '重跑當前整合驗收：`python Tools/verify_street_city_current.py`。報告只接受現行來源 SHA 與實際檔案，不會因控制簡模存在而宣告正式模型完成。']
    write_text(OUT/'current-acceptance.md','\n'.join(lines)+'\n')
    print(json.dumps({k:receipt[k] for k in ('status','unit_tests','buildings','floors','control_glb_checks','actual_tripo_models_approved','ready_primary_rooms')},ensure_ascii=False))


if __name__=='__main__':main()
