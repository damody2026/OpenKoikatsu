"""Conservative selection of every input to the indoor control exporter."""
def control_inputs(manifest,parts):
    layouts=[q for q in manifest['layouts'] if q.get('kind')=='building' or q.get('room_id')]
    owners={q.get('room_id',q.get('building')) for q in layouts}
    instances=[q for q in manifest['instances'] if q['owner'] in owners]
    source_ids={q['asset_id'] for q in instances}
    recipes=[q for q in parts['assemblies'] if q['source_asset_id'] in source_ids]
    part_ids={c['asset_id'] for q in recipes for c in q['components']}
    return dict(layouts=layouts,instances=instances,assets=[q for q in manifest['assets'] if q['id'] in source_ids],
                assemblies=recipes,parts=[q for q in parts['assets'] if q['id'] in part_ids])
