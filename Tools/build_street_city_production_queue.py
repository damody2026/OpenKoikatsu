"""Read-only production dependencies from actual recipes, files and approval gates."""
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path
from street_city_io import write_json, write_text
from street_city_model_gate import inspect_glb, approval_errors
from street_city_generation_state import resume_state

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'Docs/StreetSocialCity/production'
LIB=ROOT/'Content/IndependentResources/StreetSocialCity'


def dependencies(spec, recipe):
    if recipe['components']:
        return {c['asset_id'] for c in recipe['components']}, ([recipe['unfinished_details']] if recipe['unfinished_details'] else [])
    if spec['role'] in ('circulation','facade_module','structural_band','outdoor_node'):
        return set(), ([recipe['unfinished_details']] if recipe['unfinished_details'] else [])
    return {spec['id']}, []


def main():
    source=OUT/'assembly-manifest.json'
    manifest=json.loads(source.read_text('utf-8'))
    digest=hashlib.sha256(source.read_bytes()).hexdigest()
    modular=json.loads((OUT/'manufacturing-manifest.json').read_text('utf-8'))
    if modular['source_manifest_sha256']!=digest:
        raise SystemExit('Stale manufacturing recipe')
    ledger=json.loads((OUT/'generation-ledger.json').read_text('utf-8'))
    specs={a['id']:a for a in manifest['assets']+modular['assets']}
    recipes={a['source_asset_id']:a for a in modular['assemblies']}
    room_layouts={a['room_id']:a for a in manifest['layouts'] if a.get('room_id')}
    owners=defaultdict(list)
    for instance in manifest['instances']:
        owners[instance['owner']].append(instance)
    needed=Counter()
    owner_dependencies={}
    for owner, instances in owners.items():
        parts=set(); blocked=set()
        for instance in instances:
            deps, issues=dependencies(specs[instance['asset_id']],recipes[instance['asset_id']])
            parts.update(deps);blocked.update(issues)
        owner_dependencies[owner]=(parts,blocked)
        needed.update(parts)
    accepted=set(); rejected={}
    for aid in needed:
        spec=specs[aid];model=LIB/spec['expected_file'];approval=LIB/spec['approval_file']
        if not model.is_file() and not approval.is_file():
            continue
        if not model.is_file() or not approval.is_file():
            rejected[aid]=['incomplete model/approval pair'];continue
        try:
            errors=approval_errors(spec,inspect_glb(model),json.loads(approval.read_text('utf-8')),ledger,ROOT)
        except (ValueError,KeyError,TypeError,OSError) as exc:
            errors=[str(exc)]
        if errors:rejected[aid]=errors
        else:accepted.add(aid)
    rooms=[]
    for rid, layout in room_layouts.items():
        deps,issues=owner_dependencies.get(rid,(set(),set()))
        missing=deps-accepted
        rooms.append(dict(room_id=rid,building=layout['building'],floor=layout['floor'],
            model_dependencies=sorted(deps),verified_models=sorted(deps&accepted),
            missing_models=sorted(missing),unresolved_details=sorted(issues),
            ready_for_strict_assembly=bool(deps) and not missing and not issues))
    queue=[]
    for aid,count in needed.most_common():
        if aid in accepted:continue
        a=specs[aid]
        queue.append(dict(asset_id=aid,kind=a.get('role',a.get('kind')),size_m=a['size_m'],
            owner_dependency_reuse=count,status='rejected_evidence' if aid in rejected else 'not_verified',
            recovery=resume_state(a,ledger,ROOT),
            submission_allowed=False,requires='approved isolated reference, actual download, dimensions, mesh and visual review'))
    report=dict(schema=1,source_manifest_sha256=digest,scope='actual manufacturing dependencies; no generation or automatic approval',
        semantic_assets=len(manifest['assets']),required_distinct_model_files=len(needed),
        verified_distinct_model_files=len(accepted),verified_models=sorted(accepted),
        ready_primary_rooms=sum(r['ready_for_strict_assembly'] for r in rooms),
        primary_rooms=len(rooms),rooms=rooms,queue=queue,rejected_evidence=rejected,
        resumable_existing_exports=[q for q in queue if q['recovery']['next_action']=='resume_existing_export'],
        world_complete=False,runtime_verified=False)
    write_json(OUT/'production-queue.json',report)
    lines=['# 實際模型製作佇列','',f'來源SHA：`{digest}`。','',
        f"語意總成{len(manifest['assets']):,}筆，展開後需要{len(needed):,}種獨立模型；真正通過檔案與證據驗收{len(accepted)}種。",
        f"{len(rooms):,}個主要室內單元中，可進入完整正式組裝者{report['ready_primary_rooms']}間。未解決的外牆、扶手及電梯細節另保留，不把控制幾何當生成模型。",'',
        '書桌等共用零件只生成一次，仍依每房原點及旋轉放置；房門與設備未具備相同用途／功能契約時，不只憑相同尺寸合併。','',
        '## 已驗證模型','']+[f'- {aid}' for aid in sorted(accepted)]+['','## 下一批依重用範圍排序','',
        '| 資產 | 尺寸m | 依賴此件的房間／建築數 |','|---|---|---:|']
    lines += [f"|{q['asset_id']}|{q['size_m']}|{q['owner_dependency_reuse']}|" for q in queue[:30]]
    lines += ['', '## 優先恢復既有生成，不重複扣點', '',
        '以下項目已有相同幾何版本的 Tripo 模型，應從既有頁面恢復匯出；僅下載並驗收成功後才可組裝。', '']
    lines += [f"- [{q['asset_id']}]({q['recovery'].get('tripo_url') or '#'})：{q['recovery'].get('blocking_reason') or '待取得實際下載檔'}" for q in report['resumable_existing_exports']]
    write_text(OUT/'production-queue.md','\n'.join(lines)+'\n')
    print(json.dumps({k:report[k] for k in ('required_distinct_model_files','verified_distinct_model_files','ready_primary_rooms','primary_rooms')}))


if __name__=='__main__':main()
