"""Version-pinned mall remodel acceptance, including actual 80-view evidence."""
import json,datetime
from pathlib import Path
from PIL import Image
from street_city_mall import OUT as MALL,ROOT,plan,sha
from street_city_mall_boutiques import OUT,profiles
from street_city_localization import OUT as LANG
from street_city_remesh_gate import verify

def main():
 checks=[];files={}
 def check(value,label):
  checks.append(dict(passed=bool(value),name=label))
 def pin(path,digest=None):
  path=Path(path);actual=sha(path);files[str(path.resolve())]=actual
  if digest:check(actual==digest,'current '+str(path))
  return actual
 a=json.loads((MALL/'acceptance.json').read_text('utf8'));g=json.loads((MALL/'geometry.json').read_text('utf8'));b=json.loads((MALL/'assembly-report.json').read_text('utf8'));n=json.loads((MALL/'unreal-acceptance.json').read_text('utf8'));r=json.loads((OUT/'render-receipt.json').read_text('utf8'));overall=json.loads((MALL/'render-receipt.json').read_text('utf8'));langs=json.loads((LANG/'native-bindings.json').read_text('utf8'))
 check(g==plan(),'current exact 80-tenant plan')
 for name,receipt in [('geometry',a),('native collision',n),('80 boutique renders',r),('full mall renders',overall)]:check(receipt['status']=='passed',name+' passed')
 pin(MALL/'M1-complete-2k.glb',a['model_sha256']);pin(MALL/'M1-complete-2k.blend');pin(MALL/'geometry.json',b['geometry_sha256']);pin(MALL/'assembly-report.json',a['assembly_sha256']);pin(ROOT/'Tools/accept_street_city_mall.py',a['verifier_sha256']);pin(MALL/'acceptance.json',n['acceptance_sha256']);pin(ROOT/'Tools/assemble_street_city_unreal_mall.py',n['source_sha256'])
 check(max(a['floor_triangles'].values())<=100000,'all ten floors <=100k triangles')
 from street_city_mall_merchandise import OUT as GOODS,folder_for
 from street_city_mall_stock import catalogue
 goods=catalogue();stock=json.loads((GOODS/'assembly-acceptance.json').read_text('utf8'))
 check(stock['status']=='passed' and stock['model_sha256']==a['model_sha256'],'current real stocked-product assembly passed')
 check(stock['stocked_tenants']==80 and stock['unique_product_models']==len(goods['components'])<=100,'all eighty tenants stocked using at most one hundred product types')
 pin(GOODS/'assembly-acceptance.json');pin(ROOT/'Tools/accept_street_city_mall_stock.py',stock['verifier_sha256'])
 photos=json.loads((GOODS/'previews/receipt.json').read_text('utf8'));check(photos['status']=='complete' and set(photos['images'])=={q['id'] for q in goods['components']},'all actual merchandise model review renders present')
 for q in goods['components']:
  source=verify(folder_for(q)/'remesh');photo=photos['images'][q['id']]
  check(photo['source_sha256']==source['sha256'],'product photo matches current remesh '+q['id']);pin(GOODS/'previews'/(q['id']+'.png'),photo['image_sha256']);pin(ROOT/'Tools/render_street_city_mall_products.py',photo['renderer_sha256'])
  pin(ROOT/'Tools/street_city_product_repairs.py',photo['repairs_sha256'])
 reviewed_goods=json.loads((GOODS/'previews/visual-acceptance.json').read_text('utf8'));check(reviewed_goods['status']=='reviewed' and reviewed_goods['source_hashes']==g['contract']['merchandise']['source_hashes'],'all current product models visually reviewed')
 check(reviewed_goods['repairs_sha256']==sha(ROOT/'Tools/street_city_product_repairs.py'),'visual review includes current certified structural repairs')
 lods=json.loads((GOODS/'lods/acceptance.json').read_text('utf8'));lod_photos=json.loads((GOODS/'lod-previews/receipt.json').read_text('utf8'));lod_review=json.loads((GOODS/'lod-previews/visual-acceptance.json').read_text('utf8'))
 check(len(goods['components'])+len(lods['products'])<=100,'at most one hundred adopted product meshes including display variants')
 check(lods['status']=='passed' and lod_photos['status']=='complete' and set(lods['products'])==set(lod_photos['images']),'all adopted display LODs remeshed and rendered')
 check(lod_review['status']=='reviewed' and lod_review['source_hashes']==g['contract']['merchandise']['display_lod_hashes'],'all adopted display LODs visually reviewed')
 for aid,record in lods['products'].items():
  source=verify(GOODS/'lods'/aid/'remesh');check(record['sha256']==source['sha256']==lod_photos['images'][aid]['source_sha256'],'current display LOD '+aid);pin(GOODS/'lod-previews'/(aid+'.png'),lod_photos['images'][aid]['image_sha256'])
 pin(GOODS/'lods/acceptance.json');pin(GOODS/'lod-previews/visual-acceptance.json')
 for board in reviewed_goods['boards']:pin(board['path'],board['sha256'])
 for name in ['catalogue.json','jobs.json','revisions.json','source-receipts.json','previews/receipt.json','previews/visual-acceptance.json','index.html']:pin(GOODS/name)
 displays=json.loads((GOODS/'stores/receipt.json').read_text('utf8'));check(displays['status']=='passed' and displays['model_sha256']==a['model_sha256'] and set(displays['images'])==set(profiles()),'eighty current assembled merchandise close-ups')
 pin(ROOT/'Tools/render_street_city_mall_stock.py',displays['renderer_sha256'])
 for photo in displays['images'].values():pin(photo['path'],photo['sha256'])
 display_review=json.loads((GOODS/'stores/visual-acceptance.json').read_text('utf8'));check(display_review['status']=='reviewed' and display_review['model_sha256']==a['model_sha256'],'all eighty stocked displays visually reviewed')
 for board in display_review['boards']:pin(board['path'],board['sha256'])
 pin(GOODS/'stocked-stores.html')
 for source,digest in b['source_files'].items():pin(source,digest)
 for q in b['sources'].values():verify(Path(q['remesh_report']).parent,q['sha256']);pin(q['path'],q['sha256'])
 pbr=json.loads((OUT/'materials.json').read_text('utf8'));pin(OUT/'materials.json',b['boutiques_sha256']);pin(ROOT/'Tools/street_city_mall_boutiques.py',pbr['generator_sha256'])
 for p in pbr['materials'].values():
  for q in p['maps'].values():
   pin(q['path'],q['sha256'])
   with Image.open(q['path']) as im:check(max(im.size)<=2048,'PBR <=2K '+Path(q['path']).name)
 check(set(r['images'])==set(profiles()),'all 80 actual tenant views')
 check(r['model_sha256']==overall['model_sha256']==a['model_sha256'],'all images show current exported model')
 pin(ROOT/'Tools/render_street_city_boutiques.py',r['renderer_sha256']);pin(ROOT/'Tools/render_street_city_mall.py',overall['renderer_sha256'])
 pin(ROOT/'Tools/street_city_render_device.py',r['device_config_sha256'])
 for q in r['images'].values():
  pin(q['path'],q['sha256'])
  with Image.open(q['path']) as im:im.verify()
 for name,digest in overall['images'].items():pin(MALL/name,digest)
 check(len(n['imports'])==10,'ten current native floor imports')
 lights=json.loads((OUT/'native-lighting.json').read_text('utf8'));check(lights['status']=='passed' and lights['count']==160,'160 saved native boutique luminaires');pin(ROOT/'Tools/street_city_mall_lighting_unreal.py',lights['installer_sha256']);pin(OUT/'native-lighting.json')
 for q in n['imports']:pin(q['source'],q['source_sha256'])
 for q in n['saved_assets']:pin(q['path'],q['sha256'])
 mapfile=ROOT/'StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap';pin(mapfile,langs['native_map_sha256'])
 check(langs['status']=='passed_reopened_native_materials_and_runtime_parameter' and langs['actor_count']==387,'all 387 floor language materials independently reopened')
 mall=next(q for q in json.loads((LANG/'models.json').read_text('utf8'))['buildings'] if q['building']=='M1');pin(mall['source'],mall['source_sha256']);pin(mall['model'],mall['model_sha256'])
 check({q['semantic_id'] for q in mall['bindings'] if q['semantic_id'].startswith('tenant.')}=={'tenant.'+i for i in profiles()},'80 premium tenant sign materials remain trilingual')
 native_keys={slot['semantic_id'] for actor in langs['actors'] if actor['building']=='M1' for slot in actor['slots']}
 check(native_keys=={q['semantic_id'] for q in mall['bindings']},'every mall source text identity actually bound in native scene')
 for name in ['index.html','RESEARCH.md','programme.json','geometry.json','acceptance.json','unreal-acceptance.json']:pin(MALL/name)
 for name in ['run_street_city_mall.py','run_street_city_mall_boutiques.py','verify_street_city_mall_delivery.py','verify_street_city_boutique_delivery.py','refresh_street_city_mall_languages.py']:pin(ROOT/'Tools'/name)
 for name in ['index.html','RESEARCH.md','render-receipt.json']:pin(OUT/name)
 review=OUT/'review/visual-acceptance.json'
 if review.exists():
  reviewed=json.loads(review.read_text('utf8'));check(reviewed['status']=='reviewed' and reviewed['model_sha256']==a['model_sha256'],'current 80-view visual review');pin(review)
  for board in reviewed['boards']:pin(board['path'],board['sha256'])
 errors=[q for q in checks if not q['passed']]
 report=dict(status='failed' if errors else 'passed',completed_at=datetime.datetime.now().astimezone().isoformat(),building='M1',floors=10,tenants=80,checks=len(checks),issues=errors,results=checks,files=files,maximum_floor_triangles=max(a['floor_triangles'].values()),mesh_checks=a['checks'],native_checks=n['checks'],scope='current premium mall: geometry, collision, PBR, 80 rendered concessions, 387 floor language bindings; existing non-mall mesh transforms/collision preserved by native importer',limitations=g['contract']['limitations'])
 (OUT/'acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8');(MALL/'delivery-acceptance.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8');print(report['status'],len(checks),'delivery checks',flush=True);assert not errors,errors[:8]
 # Refresh the human-facing aggregate only after the current native map,
 # language bindings, product sources and every display have passed above.
 summary_path=LANG/'acceptance.json'
 summary=json.loads(summary_path.read_text('utf8'))
 summary['native_map_sha256']=langs['native_map_sha256']
 summary['mall_premium_fitout']=dict(status='passed',floors=10,tenants=80,native_lights=160,maximum_floor_triangles=report['maximum_floor_triangles'],receipt=str((OUT/'acceptance.json').relative_to(ROOT)))
 summary['mall_merchandise']=dict(status='passed',unique_product_models=stock['unique_product_models'],instances=stock['instances'],stocked_tenants=stock['stocked_tenants'],model_sha256=a['model_sha256'],receipt=str((GOODS/'assembly-acceptance.json').relative_to(ROOT)))
 summary_path.write_text(json.dumps(summary,ensure_ascii=False,indent=2),'utf8')
 (GOODS/'production-status.json').write_text(json.dumps(dict(status='complete',unique_product_models=len(goods['components']),limit=100,new_tripo_models=sum(q['provider']=='Tripo Model' for q in goods['components']),reused_models=sum(q['provider']=='reuse' for q in goods['components']),adopted_display_variants=len(lods['products']),total_adopted_product_meshes=len(goods['components'])+len(lods['products']),placed_instances=stock['instances'],stocked_tenants=80,maximum_floor_triangles=report['maximum_floor_triangles'],delivery_checks=len(checks),model_sha256=a['model_sha256'],delivery_receipt=str((MALL/'delivery-acceptance.json').relative_to(ROOT))),ensure_ascii=False,indent=2),'utf8')

if __name__=='__main__':main()
