"""Independent reopened native instance, LOD, collision and entrance regression audit."""
import unreal,sys,math,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from street_city_streetscape import ROOT,BASE,OUT,read,write,sha
delivery=read(OUT/'delivery.json');installed=read(OUT/'native-install.json');assert installed['delivery_sha256']==sha(OUT/'delivery.json');checks=[];issues=[];frames=0
def check(ok,name,**data):
 row=dict(passed=bool(ok),name=name,**data);checks.append(row)
 if not ok:issues.append(row)
def xyz(p):return unreal.Vector(p[1]*100,p[0]*100,p[2]*100)
def material_property(material,name):
 if isinstance(material,unreal.Material):return material.get_editor_property(name)
 overrides=material.get_editor_property('base_property_overrides')
 if overrides.get_editor_property('override_'+name):return overrides.get_editor_property(name)
 return material_property(material.get_editor_property('parent'),name)
assert unreal.EditorLevelLibrary.load_level('/Game/UniversityCity/Maps/UniversityCity');unreal.AutomationLibrary.finish_loading_before_screenshot();unreal.EditorPythonScripting.set_keep_python_script_alive(True)
def audit():
 world=unreal.EditorLevelLibrary.get_editor_world();allactors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();actors={a.get_actor_label():a for a in allactors};api=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem);meshes={q['id']:q for q in installed['assets']};assets={q['id']:q for q in delivery['assets']};actualgroups=[a for a in allactors if 'StreetScenery-v1' in [str(t) for t in a.tags]]
 check(len(actualgroups)==len(delivery['groups']),'exact managed cluster set, no duplicate installation')
 for aid,row in meshes.items():
  mesh=unreal.load_asset(row['asset']);expected=assets[aid]['lods'];check(mesh is not None,'native mesh '+aid);check(api.get_lod_count(mesh)==len(expected),'native real LOD count '+aid)
  check([mesh.get_num_triangles(i) for i in range(len(expected))]==[q['triangles'] for q in expected],'native measured triangles '+aid)
  check(unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceSHA256')==expected[0]['sha256'],'native certified source identity '+aid)
  if len(expected)>1:check(not mesh.is_lod_screen_size_auto_computed() and all(abs(x-y)<.00001 for x,y in zip(api.get_lod_screen_sizes(mesh),row['lod_screen_sizes'])),'native automatic distance LOD thresholds '+aid)
  if aid in ('camphor','cherry'):
   foliage=[slot.get_editor_property('material_interface') for slot in mesh.get_editor_property('static_materials') if 'masked' in str(slot.get_editor_property('material_slot_name')).lower()]
   check(bool(foliage) and all(material_property(m,'blend_mode')==unreal.BlendMode.BLEND_MASKED and material_property(m,'two_sided') for m in foliage),'native cutout foliage shader '+aid)
   for material in foliage:
    mel=unreal.MaterialEditingLibrary;node=mel.get_material_property_input_node(material,unreal.MaterialProperty.MP_OPACITY_MASK);channel=mel.get_material_property_input_node_output_name(material,unreal.MaterialProperty.MP_OPACITY_MASK)
    check(isinstance(node,unreal.MaterialExpressionTextureSample) and channel=='A','actual texture alpha connected to opacity mask '+aid)
    check(unreal.EditorAssetLibrary.get_metadata_tag(material,'StreetLeafAlphaSHA256')==sha(OUT/'textures'/(aid+'-leaves.png')),'native original leaf texture identity '+aid)
  check(not mesh.get_editor_property('nanite_settings').get_editor_property('enabled'),'explicit low-poly LOD mesh, no Nanite dependency '+aid)
 for group in delivery['groups']:
  actor=actors.get(group['label']);check(actor is not None,'cluster exists '+group['label']);comp=actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent);check(comp is not None,'real HISM '+group['label'])
  check(comp.static_mesh.get_path_name()==meshes[group['asset']]['asset'],'shared native mesh '+group['label']);check(comp.get_instance_count()==len(group['instances']),'exact instance count '+group['label']);check(str(comp.get_collision_profile_name())==group['collision'],'collision policy '+group['label']);check(bool(comp.get_editor_property('visible'))==group['visible'] and bool(comp.get_editor_property('hidden_in_game'))== (not group['visible']),'visibility policy '+group['label']);check(comp.get_editor_property('instance_start_cull_distance')==group['cull_start_m']*100 and comp.get_editor_property('instance_end_cull_distance')==group['cull_end_m']*100,'actual instance distance culling '+group['label'])
  check(comp.get_editor_property('forced_lod_model')==0,'instance LOD remains automatic '+group['label'])
  check(all(comp.get_material(i) is not None for i in range(comp.get_num_materials())),'all HISM materials bound '+group['label'])
  if group['asset'] in ('camphor','cherry'):check(all(comp.get_material(i)==comp.static_mesh.get_material(i) for i in range(comp.get_num_materials())),'actual HISM leaf overrides match certified alpha graph '+group['label'])
  for i,q in enumerate(group['instances']):
   t=comp.get_instance_transform(i,True);p=t.translation;s=t.scale3d;r=t.rotation.rotator();want=xyz(q['position_m']);check(max(abs(p.x-want.x),abs(p.y-want.y),abs(p.z-want.z))<.1,'native metric placement '+q['id']);check(max(abs(x-y) for x,y in zip([s.x,s.y,s.z],q['scale']))<.0001 and abs((r.yaw-(90-q['yaw_deg'])+180)%360-180)<.01,'native instance scale and orientation '+q['id'])
 for q in delivery['legacy_replacements']:
  actor=actors[q['label']];comp=actor.static_mesh_component;check(unreal.EditorAssetLibrary.get_metadata_tag(comp.static_mesh,'StreetCitySourceSHA256')==q['source_sha256'],'immutable archived cluster '+q['label']);check(not comp.get_editor_property('visible') and actor.get_editor_property('hidden') and str(comp.get_collision_profile_name())=='NoCollision','legacy batch retired without double rendering/collision '+q['label'])
 for q in delivery['facility_sources']:
  mesh=actors[q['label']].static_mesh_component.static_mesh;check(unreal.EditorAssetLibrary.get_metadata_tag(mesh,'StreetCitySourceSHA256')==q['sha256'],'facility has no duplicated embedded trees '+q['label']);check(mesh.get_num_triangles(0)==q['triangles'],'all preserved facility parts remain '+q['label'])
 # Isolate the new layer to detect newly introduced obstructions without masking
 # an old, unrelated building that the schematic route passes underneath.
 ignore=[a for a in allactors if isinstance(a,unreal.StaticMeshActor)]
 for a,b,width,kind,label in read(OUT/'plan.json')['entrance_approaches_m']:
  dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy)
  if length<.01:continue
  for t in [.15,.4,.65,.9]:
   for lane in [-.9,0,.9]:
    x=a[0]+dx*t-dy/length*lane;y=a[1]+dy*t+dx/length*lane
    # All student shop approaches have the same accepted 12m terrace.
    z=12.15;h=unreal.SystemLibrary.line_trace_single(world,xyz([x,y,z]),xyz([x,y,z+2.05]),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignore,unreal.DrawDebugTrace.NONE,True);hit=h is not None and h.to_tuple()[0];check(not hit,'new street props preserve 2m entrance approach '+label+f' {t}/{lane}')
 result=dict(status='failed' if issues else 'passed',checks=len(checks),issues=issues,results=checks,tree_instances=delivery['tree_instances'],visible_instances=delivery['visible_instances'],clusters=len(delivery['groups']),delivery_sha256=sha(OUT/'delivery.json'),map_sha256=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap'),verifier_sha256=sha(__file__),scope='Reopened saved native HISM instances, actual mesh LOD counts/triangles/screen thresholds, culling, source identities, legacy replacement and new-layer entrance clearance')
 write(OUT/'native-acceptance.json',result);assert not issues,issues[:10];activation=read(OUT/'activation.json');activation.update(status='passed',native_acceptance_sha256=sha(OUT/'native-acceptance.json'),map_sha256=result['map_sha256']);write(OUT/'activation.json',activation)
def tick(dt):
 global frames
 frames+=1
 if frames<60:return
 unreal.unregister_slate_post_tick_callback(handle)
 try:audit()
 except Exception:write(OUT/'native-audit-error.json',dict(error=traceback.format_exc()));unreal.log_error(traceback.format_exc())
 finally:unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
