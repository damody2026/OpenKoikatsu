"""Resumable premium mall production, with no unrelated geometry reimports."""
import sys,os,json,subprocess,time
from pathlib import Path
from street_city_mall import ROOT,OUT,plan,sha,estimate_triangles
from run_street_city_shop_street import run
from street_city_mall_boutiques import generate,OUT as BOUTIQUES,publish

def command(*args):subprocess.run(args,cwd=ROOT,check=True)

def native_language(script,group,receipt,status):
 start=time.time()
 command(str(ROOT/'Tools/lua/lua.exe'),'Tools/run_street_city_localization_unreal.lua','--script',script,'--group',group)
 assert receipt.stat().st_mtime>=start,('Missing fresh native receipt',receipt)
 result=json.loads(receipt.read_text('utf8'));assert result['status']==status,(receipt,result.get('status'))

def main():
 os.environ['STREET_CITY_BLENDER_THREADS']='4'
 generate();estimates=estimate_triangles(plan());assert max(estimates.values())<=100000
 from street_city_mall_merchandise import OUT as GOODS
 run('Tools/render_street_city_mall_products.py',GOODS)
 from review_street_city_mall_products import main as product_boards
 product_boards()
 from street_city_mall_architecture import generate as architecture,generate_graphics
 architecture(OUT/'materials');generate_graphics(OUT/'materials')
 valid=False
 try:
  a=json.loads((OUT/'acceptance.json').read_text('utf8'));r=json.loads((OUT/'assembly-report.json').read_text('utf8'))
  valid=a['status']=='passed' and a['model_sha256']==sha(OUT/'M1-complete-2k.glb') and a['verifier_sha256']==sha(ROOT/'Tools/accept_street_city_mall.py') and json.loads((OUT/'geometry.json').read_text('utf8'))==plan() and all(sha(p)==s for p,s in r['source_files'].items()) and r['boutiques_sha256']==sha(BOUTIQUES/'materials.json')
 except (OSError,KeyError,ValueError):pass
 if not valid:run('Tools/build_street_city_mall.py',OUT);run('Tools/accept_street_city_mall.py',OUT)
 run('Tools/accept_street_city_mall_stock.py',OUT)
 render=False
 try:
  r=json.loads((OUT/'render-receipt.json').read_text('utf8'));render=r['model_sha256']==sha(OUT/'M1-complete-2k.glb') and r['renderer_sha256']==sha(ROOT/'Tools/render_street_city_mall.py') and all(sha(OUT/p)==h for p,h in r['images'].items())
 except (OSError,KeyError,ValueError):pass
 if not render:run('Tools/render_street_city_mall.py',OUT)
 run('Tools/render_street_city_boutiques.py',OUT)
 run('Tools/render_street_city_mall_stock.py',OUT)
 from street_city_localization import OUT as LANG
 from refresh_street_city_mall_languages import main as refresh_languages
 refresh_languages()
 command(str(ROOT/'Tools/lua/lua.exe'),'Tools/run_street_city_mall_unreal.lua')
 native_language('install_street_city_localization_unreal.py','mall',LANG/'unreal-mall.json','passed')
 command(str(ROOT/'Tools/lua/lua.exe'),'Tools/run_street_city_mall_unreal.lua','--audit-only')
 native_language('audit_street_city_campus_signs_unreal.py','campus-signs',LANG/'campus-signs/acceptance.json','passed')
 from publish_street_city_mall import publish as publish_mall
 publish_mall();publish()
 from publish_street_city_mall_stock import main as publish_stock
 publish_stock()
 command(sys.executable,'Tools/audit_street_city_localization.py')
 from verify_street_city_boutique_delivery import main as verify
 verify()

if __name__=='__main__':main()
