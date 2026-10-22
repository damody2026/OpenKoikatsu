"""Independently reopen saved wall bindings, image identities, text and exact geometry."""
import unreal,json,sys,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_world_wall_art import ROOT,OUT,sha
from street_city_world_wall_rules import read_materials
def main():
 r=json.loads((OUT/'native-install.json').read_text('utf8'));assert r['status']=='passed';assert r['installer_sha256']==sha(ROOT/'Tools/install_street_city_world_walls_unreal.py');inventory=json.loads((OUT.parent/'scene-integration-v1/inventory.json').read_text('utf8'));assert sha(OUT.parent/'scene-integration-v1/inventory.json')==r['source_inventory_sha256']
 assert unreal.EditorLevelLibrary.load_level(inventory['map']);actors={a.get_actor_label():a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()};checks=0;textures=set();materials=set();covered=set();images=read_materials();EAL=unreal.EditorAssetLibrary;MEL=unreal.MaterialEditingLibrary
 for q in r['slots']:
  mat=actors[q['actor']].static_mesh_component.get_material(q['slot']);assert mat.get_path_name()==q['material'];assert EAL.get_metadata_tag(mat,'CityWallIdentity')==q['identity'];assert not EAL.get_metadata_tag(mat,'CityTextID');covered.add(q['building']);checks+=3
  if q['material'] in materials:continue
  materials.add(q['material']);assert isinstance(MEL.get_material_property_input_node(mat,unreal.MaterialProperty.MP_BASE_COLOR),unreal.MaterialExpressionCustom);assert EAL.get_metadata_tag(mat,'CityWallPeriodM')=='2';checks+=2
  nodes={str(n.get_editor_property('parameter_name')):n for n in MEL.get_material_expressions(mat) if isinstance(n,unreal.MaterialExpressionTextureObjectParameter)}
  for label,key in [('I',q['interior']),('E',q['exterior'])]:
   for suffix,kind in [('C','basecolor'),('R','roughness'),('B','normal')]:
    tex=nodes[label+suffix].get_editor_property('texture');expected=images[key]['maps'][kind];assert sha(expected['path'])==expected['sha256'];assert EAL.get_metadata_tag(tex,'CityWallImageSHA')==expected['sha256'];assert max(tex.blueprint_get_size_x(),tex.blueprint_get_size_y())<=2048;assert bool(tex.get_editor_property('srgb'))==(kind=='basecolor'),(key,kind,tex.get_path_name(),bool(tex.get_editor_property('srgb')));textures.add(tex.get_path_name());checks+=4
 assert len(covered)==122 and r['floor_actors']==427
 for q in inventory['rows']:
  a=actors[q['id']];c=a.static_mesh_component;assert EAL.get_metadata_tag(c.static_mesh,'StreetCitySourceSHA256')==q['source_sha256'];p=a.get_actor_location();assert max(abs(v-w) for v,w in zip([p.x,p.y,p.z],q['position_cm']))<.5;assert str(c.get_collision_profile_name())==q['collision'];checks+=3
 report=dict(status='passed',checks=checks,buildings=len(covered),floor_actors=427,wall_slots=len(r['slots']),unique_materials=len(materials),unique_textures=len(textures),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),source_install_sha256=sha(OUT/'native-install.json'),verifier_sha256=sha(__file__),geometry_sources_unchanged=True,period_m=2,max_texture_resolution=2048)
 (OUT/'native-acceptance.json').write_text(json.dumps(report,indent=2),'utf8')
if __name__=='__main__':
 try:main()
 except Exception:(OUT/'native-audit-error.txt').write_text(traceback.format_exc(),'utf8');raise
 finally:unreal.SystemLibrary.quit_editor()
