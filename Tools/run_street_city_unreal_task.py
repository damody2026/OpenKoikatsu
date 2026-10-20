"""Full-editor script runner; always exit, and never use exit code as sole proof."""
import unreal,re,runpy,json,traceback
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'output/street-social-city-production/world-ground-v1';match=re.search(r'-CityStage=(Assemble|Render|InteriorAudit|ImportShops|PrepareShops|Campus|InteriorAuditCollision)\b',unreal.SystemLibrary.get_command_line());assert match;stage=match[1];script,report={'Assemble':('assemble_street_city_unreal.py','unreal-acceptance.json'),'Render':('render_street_city_unreal.py','unreal-render.json'),'InteriorAudit':('audit_street_city_unreal_interiors.py','unreal-interioraudit.json'),'ImportShops':('import_street_city_unreal_shops.py','unreal-shops-import.json'),'PrepareShops':('prepare_street_city_unreal_shop_assets.py','unreal-shops-prepare.json'),'Campus':('assemble_street_city_unreal_campus.py','unreal-campus.json'),'InteriorAuditCollision':('audit_street_city_unreal_interiors.py','unreal-interioraudit.json')}[stage];path=OUT/('unreal-'+stage.lower()+'-task.json');status=dict(status='running',stage=stage,script=script);path.write_text(json.dumps(status,indent=2),'utf-8')
deferred=False
try:
 namespace=runpy.run_path(str(ROOT/'Tools'/script),run_name='__main__');deferred=bool(namespace.get('CITY_ASYNC_RENDER'))
 if not deferred:
  result=json.loads((OUT/report).read_text('utf-8'));assert result['status']=='passed';status['status']='succeeded'
except Exception:
 status.update(status='failed',error=traceback.format_exc());unreal.log_error(status['error'])
finally:
 if not deferred:
  path.write_text(json.dumps(status,indent=2),'utf-8');unreal.SystemLibrary.quit_editor()
