"""Load semantic and manufacturing assets without changing their contracts."""
import hashlib
import json
from pathlib import Path


def load_asset_catalog(directory):
    directory=Path(directory)
    source=directory/'assembly-manifest.json'
    manifest=json.loads(source.read_text('utf-8'))
    manufacturing=json.loads((directory/'manufacturing-manifest.json').read_text('utf-8'))
    if manufacturing.get('source_manifest_sha256')!=hashlib.sha256(source.read_bytes()).hexdigest():
        raise ValueError('Manufacturing asset catalog has a stale source manifest')
    result={}
    for asset in manifest['assets']+manufacturing['assets']:
        if asset['id'] in result:raise ValueError('Duplicate asset identity: '+asset['id'])
        result[asset['id']]=asset
    return result
