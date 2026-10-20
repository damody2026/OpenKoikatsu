"""Register a reviewed actual Tripo model, checking its full local evidence chain.

Approval is for a static reusable asset; runtime navigation/performance is separate.
Does not modify the dimensional assembly manifest or relabel city controls.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path

from street_city_io import write_json
from street_city_model_gate import inspect_glb, geometry_errors, normalization_errors, approval_errors
from street_city_parts import geometry_signature
from street_city_catalog import load_asset_catalog

ROOT = Path(__file__).resolve().parents[1]
PROD = ROOT/'Docs/StreetSocialCity/production'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def checked_file(value):
    path = Path(value)
    if not path.is_absolute():
        path = ROOT/path
    path = path.resolve()
    if not path.is_relative_to(ROOT) or not path.is_file():
        raise ValueError('Evidence file must exist inside this workspace: '+str(path))
    return path


def relative(path):
    return str(path.relative_to(ROOT)).replace('\\', '/')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--asset', required=True)
    p.add_argument('--model', required=True)
    p.add_argument('--normalization-report', required=True)
    p.add_argument('--finishing-report')
    p.add_argument('--mesh-audit', required=True)
    p.add_argument('--quality-review', required=True)
    args = p.parse_args()
    model = checked_file(args.model)
    normal_file = checked_file(args.normalization_report)
    audit_file = checked_file(args.mesh_audit)
    review_file = checked_file(args.quality_review)
    audit = json.loads(audit_file.read_text('utf-8'))
    review = json.loads(review_file.read_text('utf-8'))
    normal = json.loads(normal_file.read_text('utf-8'))
    spec = load_asset_catalog(PROD)[args.asset]
    actual = inspect_glb(model)
    signature = geometry_signature(spec)
    errors = geometry_errors(spec, actual)+normalization_errors(normal['original_size_m'], spec['size_m'])
    if normal.get('asset_id') != spec['id'] or normal.get('geometry_signature') != signature or normal.get('errors'):
        errors.append('normalization evidence missing/stale')
    ledger = json.loads((PROD/'generation-ledger.json').read_text('utf-8'))
    entry = next(e for e in ledger['entries'] if e['asset_id'] == args.asset and e.get('geometry_signature') == signature)
    download = checked_file(entry['downloaded_file'])
    if sha(download) != entry.get('downloaded_sha256') or normal.get('source_sha256') != sha(download):
        errors.append('original download hash mismatch')
    if checked_file(normal['source_file']) != download:
        errors.append('normalization did not use registered Tripo download')
    evidence = dict(normalization_report=relative(normal_file), mesh_audit_file=relative(audit_file),
                    downloaded_file=relative(download), downloaded_sha256=sha(download))
    finish = None
    if args.finishing_report:
        finish_file = checked_file(args.finishing_report)
        finish = json.loads(finish_file.read_text('utf-8'))
        canonical = checked_file(finish['source_file'])
        if canonical != normal_file.parent/'candidate.blend' or sha(canonical) != finish.get('source_sha256'):
            errors.append('finishing source does not match canonical normalization candidate')
        if finish.get('model_sha256') != actual['model_sha256'] or finish.get('errors') or finish.get('geometry_signature') != signature:
            errors.append('finishing evidence missing/stale')
        recipe = checked_file(finish['recipe_file'])
        if sha(recipe) != finish.get('recipe_sha256'):
            errors.append('finishing recipe changed')
        evidence.update(finishing_report=relative(finish_file), material_origin=finish.get('material_origin'))
    elif normal.get('model_sha256') != actual['model_sha256']:
        errors.append('model differs from canonical normalization output')
    if audit.get('source_sha256') != actual['model_sha256'] or audit.get('errors') or audit.get('status') != 'candidate_checks_passed_pending_visual_review':
        errors.append('actual mesh audit failed/missing/stale')
    flags = ('silhouette', 'topology_normals', 'materials_uv', 'submeshes', 'interaction_clearances')
    if review.get('model_sha256') != actual['model_sha256'] or not all(review.get(k) is True for k in flags):
        errors.append('completed visual/functional review missing/stale')
    if not review.get('notes') or len(review.get('view_files', [])) < 3:
        errors.append('three-view review evidence and notes required')
    for view in review.get('view_files', []):
        if sha(checked_file(view['file'])) != view.get('sha256'):
            errors.append('visual review image changed')
    if actual['normal_primitives'] != actual['primitives'] or actual['uv_primitives'] != actual['primitives']:
        errors.append('actual GLB primitive has missing normals or UVs')
    meta = dict(approved=True, asset_id=spec['id'], geometry_signature=signature,
        pivot=spec['pivot'], axes='X east,Y north,Z up', tripo_model_id=entry['tripo_model_id'],
        model_sha256=actual['model_sha256'], measured_size_m=actual['measured_size_m'],
        quality_review_file=relative(review_file), approval_scope='static reusable asset',
        runtime_collision_verified=False, runtime_performance_verified=False, **evidence)
    updated = copy.deepcopy(ledger)
    update = next(e for e in updated['entries'] if e['asset_id'] == args.asset and e.get('geometry_signature') == signature)
    update.update(actual_model_approved=True, tripo_status='approved_geometry_with_locally_authored_PBR' if finish else 'approved_static_asset',
        approved_model_file=relative(model), approved_model_sha256=actual['model_sha256'],
        candidate_report_file=relative(checked_file(args.finishing_report)) if finish else relative(normal_file),
        candidate_preview_file=review['view_files'][0]['file'], quality_review_file=relative(review_file),
        mesh_audit_file=relative(audit_file), material_origin=meta.get('material_origin', 'Downloaded source materials'),
        blocking_reason='靜態零件通過尺寸、網格、UV/PBR及預留使用空間檢查；引擎碰撞及效能另驗。')
    errors += approval_errors(spec, actual, meta, updated, ROOT)
    if errors:
        raise SystemExit('\n'.join(errors))
    destination = ROOT/'Content/IndependentResources/StreetSocialCity'/spec['expected_file']
    approval = ROOT/'Content/IndependentResources/StreetSocialCity'/spec['approval_file']
    if not destination.resolve().is_relative_to(ROOT/'Content/IndependentResources/StreetSocialCity') or not approval.resolve().is_relative_to(ROOT/'Content/IndependentResources/StreetSocialCity'):
        raise SystemExit('Asset destination outside expected library')
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Identical reruns are safe. Replacing a different approved model requires a
    # new explicit revision path, so previous accepted evidence stays recoverable.
    if destination.exists() and sha(destination) != actual['model_sha256']:
        raise SystemExit('A different approved model already exists; preserve it and create a new asset revision')
    destination.write_bytes(model.read_bytes())
    write_json(approval, meta)
    write_json(PROD/'generation-ledger.json', updated)
    final = approval_errors(spec, inspect_glb(destination), json.loads(approval.read_text('utf-8')), updated, ROOT)
    if final:
        raise SystemExit('Registered artifact failed verification: '+str(final))
    print(json.dumps(dict(asset_id=args.asset, model=relative(destination), status='approved_static_asset',
        model_sha256=actual['model_sha256'], runtime_verified=False)))


if __name__ == '__main__':
    main()
