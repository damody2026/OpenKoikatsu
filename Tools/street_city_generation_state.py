"""Derive resumable work from evidence; never submit or approve a model."""
import hashlib
from pathlib import Path
from street_city_parts import geometry_signature


def checked_file(root, name, digest):
    if not name or not digest:
        return False
    path = Path(root) / name
    return path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == digest


def resume_state(spec, ledger, root, accepted=False):
    if accepted:
        return dict(next_action='reuse_verified_model', generate_again=False)
    entries = [e for e in ledger.get('entries', []) if e.get('asset_id') == spec['id']]
    matching = [e for e in entries if e.get('geometry_signature') == geometry_signature(spec)]
    if len(matching) > 1:
        return dict(next_action='resolve_duplicate_generation_evidence', generate_again=False)
    if not matching:
        return dict(next_action='review_stale_generation' if entries else 'prepare_isolated_reference',
                    generate_again=False)
    entry = matching[0]
    result = dict(generate_again=False, tripo_model_id=entry.get('tripo_model_id'),
                  tripo_url=entry.get('tripo_url'), blocking_reason=entry.get('blocking_reason'))
    reference_ok = entry.get('reference_status', '').startswith('approved') and checked_file(
        root, entry.get('tripo_input_file'), entry.get('tripo_input_sha256'))
    if not reference_ok:
        action = 'repair_reference_evidence'
    elif entry.get('tripo_model_id'):
        if checked_file(root, entry.get('downloaded_file'), entry.get('downloaded_sha256')):
            action = 'inspect_downloaded_candidate'
        elif entry.get('downloaded_file'):
            action = 'recover_missing_or_changed_download'
        else:
            action = 'resume_existing_export'
    else:
        action = 'review_reference_for_submission'
    result['next_action'] = action
    return result
