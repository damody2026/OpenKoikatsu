import unreal,json
from pathlib import Path
out=Path(__file__).resolve().parents[1]/'output/street-social-city-production/world-wall-grok-v1'
r=json.loads((out/'native-install.json').read_text('utf8'));rows=[]
try:
 seen=set()
 for q in r['slots']:
  if q['material'] in seen:continue
  seen.add(q['material'])
  m=unreal.EditorAssetLibrary.load_asset(q['material']);original=unreal.EditorAssetLibrary.load_asset(q['original'])
  row=dict(mat=q['material'],pins={str(p):unreal.MaterialEditingLibrary.get_material_property_input_node_output_name(original,p) for p in [unreal.MaterialProperty.MP_BASE_COLOR,unreal.MaterialProperty.MP_ROUGHNESS,unreal.MaterialProperty.MP_NORMAL]},textures=[])
  for n in unreal.MaterialEditingLibrary.get_material_expressions(m):
   if not isinstance(n,unreal.MaterialExpressionTextureObjectParameter):continue
   t=n.get_editor_property('texture');row['textures'].append(dict(parameter=str(n.get_editor_property('parameter_name')),path=t.get_path_name(),srgb=bool(t.get_editor_property('srgb')),compression=str(t.get_editor_property('compression_settings'))))
  rows.append(row)
 bad=[dict(mat=q['mat'],**t) for q in rows for t in q['textures'] if t['srgb']!=(t['parameter'].endswith('C'))]
 (out/'texture-probe-all.json').write_text(json.dumps(dict(materials=len(rows),issues=bad),indent=2),'utf8')
finally:unreal.SystemLibrary.quit_editor()
