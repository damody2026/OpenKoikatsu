"""Final current-source scene and compiled runtime language delivery gate."""
import json,datetime
from pathlib import Path
from street_city_scene_inventory import ROOT,OUT,BASE,read,sha

def main():
 checks=[]
 def check(ok,name):checks.append(dict(passed=bool(ok),name=name))
 inventory=read(OUT/'inventory.json');model_count=len(inventory['rows']);floor_count=sum(q['kind'] in ('shop','campus','mall','city_building') for q in inventory['rows']);native=read(OUT/'native-inventory.json');runtime=read(OUT/'runtime-acceptance.json')
 lang=read(BASE/'localization-v1/native-bindings.json');signs=read(BASE/'localization-v1/campus-signs/acceptance.json')
 mapsha=sha(ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap')
 check(native['status']=='passed' and not native['issues'],'all current native model actors passed')
 check(native['inventory_sha256']==sha(OUT/'inventory.json'),'current native inventory identity')
 check(native['map_sha256']==mapsha,'current saved map identity')
 programme=read(ROOT/'Docs/StreetSocialCity/production/building-floor-audit.json');expected_floors=sum(len(b['floors']) for b in programme['buildings'])
 check(sum(inventory['counts'].values())==model_count and floor_count==expected_floors,'all adopted scene models and planned floors accounted for')
 actual={q['id']:q for q in native['actors']}
 for q in inventory['rows']:
  check(sha(q['source'])==q['source_sha256'],'current source '+q['id'])
  check(q['id'] in actual and actual[q['id']]['source_sha256']==q['source_sha256'] and not actual[q['id']]['issues'],'current bound native '+q['id'])
 check(lang['status']=='passed_reopened_native_materials_and_runtime_parameter' and lang['actor_count']==floor_count and lang['native_map_sha256']==mapsha,'all floor language bindings reopened in current map')
 check(signs['status']=='passed' and signs['signs']==34 and signs['map_sha256']==mapsha,'all 34 campus exterior signs reopened in current map')
 check(runtime['status']=='passed' and not runtime['error'],'actual compiled runtime language audit passed')
 check(runtime['source_sha256']==sha(ROOT/'Tools/audit_street_city_language_runtime.py'),'current runtime verifier')
 for source,digest in runtime['plugin_sources'].items():check(sha(ROOT/source)==digest,'tested runtime plugin source '+source)
 plugin=ROOT/'StreetSocialCity/Plugins/CityLocalization';build=read(OUT/'plugin-build.json');built=Path(build['package']);check(build['status']=='passed','exact-source plugin build receipt')
 for source in (plugin/'Source').rglob('*'):
  if source.is_file():check(sha(source)==sha(built/source.relative_to(plugin)),'compiled source '+str(source.relative_to(plugin)))
 dll=plugin/'Binaries/Win64/UnrealEditor-CityLocalization.dll';check(sha(dll)==sha(built/'Binaries/Win64'/dll.name),'deployed compiled runtime binary')
 check('BUILD SUCCESSFUL' in (BASE/'localization-v1/runtime-plugin-build.log').read_text('utf8'),'Editor Development Shipping plugin build succeeded')
 descriptor=read(ROOT/'StreetSocialCity/StreetSocialCity.uproject');check(any(q['Name']=='CityLocalization' and q['Enabled'] for q in descriptor['Plugins']),'runtime plugin enabled in project')
 check((OUT/'sync.json').exists() and read(OUT/'sync.json')['status']=='passed','changed models actually saved into the map')
 walls=BASE/'world-wall-grok-v1'
 if (walls/'activation.json').exists():
  wall=read(walls/'native-acceptance.json');wr=read(walls/'native-render.json')
  check(wall['status']=='passed' and wall['map_sha256']==mapsha and wall['buildings']==len(programme['buildings']),'all active Grok walls reopened on current map')
  check(wall['source_install_sha256']==sha(walls/'native-install.json')==wr['source_install_sha256'],'wall overrides and real RHI use the same source receipt')
  check(wr['status']=='passed_real_rhi_captures' and len(wr['images'])>=11,'real D3D wall shader captures passed')
  for pic in wr['images']:check(sha(pic['image'])==pic['sha256'],'current actual wall capture '+pic['actor'])
 issues=[q for q in checks if not q['passed']]
 report=dict(status='failed' if issues else 'passed',completed_at=datetime.datetime.now().astimezone().isoformat(),map='/Game/UniversityCity/Maps/UniversityCity',map_sha256=mapsha,model_actors=model_count,floor_actors=floor_count,counts=inventory['counts'],locales=['zh-Hant','zh-Hans','en'],updated_floor_actors=sum(q['id'] in set(read(OUT/'sync.json')['changed']) and q['kind'] in ('shop','campus','mall','city_building') for q in inventory['rows']),updated_model_actors=len(read(OUT/'sync.json')['changed']),runtime_api='CityLanguageLibrary.SetCityLanguageByCode',runtime_checks=len(runtime['checks']),checks=len(checks),issues=issues,results=checks,plugin_binary_sha256=sha(dll),limitations=['Current adopted assembled models; historical/rejected prototypes are excluded.', 'This delivery adds runtime localization; shopping, elevator and escalator gameplay controllers are separate.'])
 if (walls/'activation.json').exists():report['active_wall_layer']=dict(buildings=wall['buildings'],floor_actors=wall['floor_actors'],material_slots=wall['wall_slots'],native_checks=wall['checks'],receipt=str((walls/'native-acceptance.json').relative_to(ROOT)))
 (OUT/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8');print(report['status'],len(checks),'integration checks',flush=True);assert not issues,issues[:5]
 summary_path=BASE/'localization-v1/acceptance.json';summary=read(summary_path);summary['native_map_sha256']=mapsha;summary['scene_integration']=dict(status='passed',model_actors=model_count,updated_floor_actors=report['updated_floor_actors'],runtime_api=report['runtime_api'],receipt=str((OUT/'acceptance.json').relative_to(ROOT)));summary_path.write_text(json.dumps(summary,ensure_ascii=False,indent=2),'utf8')
if __name__=='__main__':main()
