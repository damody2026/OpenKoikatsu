"""Repeatable final integration; never reuse a stale native collision receipt."""
import json,os,re,subprocess,sys,shutil,traceback,hashlib,argparse
from pathlib import Path
from datetime import datetime
from street_city_campus_contract import OUT
from run_street_city_shop_street import ROOT,run
from street_city_atomic import write_text

os.environ['TEMP']=str((OUT/'scratch').resolve());os.environ['TMP']=os.environ['TEMP'];os.environ['STREET_CITY_BLENDER_THREADS']='6'
parser=argparse.ArgumentParser();parser.add_argument('--finish-only',action='store_true',help='Resume publication only; the full delivery gate still rejects any stale source or engine receipt');args=parser.parse_args()
phase='starting';completed=[]
def stage(name,action):
 global phase
 phase=name;write_text(OUT/'delivery-progress.json',json.dumps(dict(status='in_progress',phase=name,completed=completed),indent=2));print(name,flush=True);action();completed.append(name)
def shell(*args):subprocess.run(list(map(str,args)),cwd=ROOT,check=True)
def final_render():
 def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
 reuse=False
 try:
  r=json.loads((OUT/'world-render-receipt.json').read_text('utf8'));w=json.loads((OUT/'world-assembly.json').read_text('utf8'));g=json.loads((OUT.parent/'world-ground-v1/ground-acceptance.json').read_text('utf8'));terrain={q['id']:q for q in g['chunks']}
  reuse=len(w['buildings'])==17 and len(w['terrain'])==(8 if w.get('outdoor') else 6) and g['status']=='passed' and all(sha(OUT/n)==value for n,value in r['files'].items()) and sha(ROOT/'Tools/render_street_city_campus.py')==r['renderer_sha256'] and sha(OUT/'campus-overview.png')==r['image_sha256']
  if w.get('outdoor'):
   oa=json.loads((OUT/'outdoor/assembly.json').read_text('utf8'));reuse=reuse and oa['assembler_sha256']==sha(ROOT/'Tools/assemble_street_city_outdoor.py') and oa['model_sha256']==sha(OUT/'campus-complete-2k.glb') and all(sha(q['source'])==q['sha256'] for q in w['outdoor'])
  reuse=reuse and all(sha(q['source'])==q['sha256'] for q in w['buildings']+w.get('water',[])) and all(terrain[q['id']]['ground']['sha256']==q['sha256']==sha(q['source']) for q in w['terrain'])
 except (OSError,ValueError,KeyError):pass
 shell(sys.executable,ROOT/'Tools/run_street_city_campus_reviews.py',*(['--views-only'] if reuse else []))
 if reuse:print('Reused exact accepted world/render versions',flush=True)
def publish_notes():
 g=json.loads((OUT.parent/'world-ground-v1/ground-acceptance.json').read_text('utf8'));u=json.loads((OUT/'unreal-acceptance.json').read_text('utf8'));w=json.loads((OUT/'world-acceptance.json').read_text('utf8'));old=json.loads((OUT.parent/'world-ground-v1/unreal-interioraudit.json').read_text('utf8'))
 assert all(q['status']=='passed' for q in [g,u,w,old])
 note=f'''\n\n### 校園本批完成驗收（{datetime.now().astimezone().date()}）\n\n17棟、89層模組化校園已完成，包含教學／研究、系辦與行政、學生中心、圖書館、國際交流、四棟宿舍及運動館。每棟完整GLB／Blender、89份逐層GLB與附長寬及1／5／10m比例尺SVG已發布。以台科大官方用途、館舍關係和公開設備尺寸為主要依據，整館尺寸、樓層及三座50m泳池是既有遊戲地塊改編，不當成原校實測。\n\n最後重新匯入完整校園檢查{w['checks']:,}項，16塊地形檢查{g['checks']:,}項，實際Unreal校園碰撞檢查{u['checks']:,}項，既有96店／288層碰撞複驗{old['checks']:,}項，皆通過。完整交付以campus-v1/delivery-acceptance.json的當前SHA與逐項結果為準；舊歷史報告保留，不能視為新版場景驗收。\n\n入口、通道、樓梯銜接、門框支撐和設備方向已按實際匯出模型與原生碰撞驗收。門／電梯動作資料保留在原模型；Unreal中的互動控制器及游泳玩法尚未接入。成果入口：output/street-social-city-production/campus-v1/index.html；獨立Unreal工程仍由OpenCity.cmd開啟。\n'''
 if (OUT/'outdoor/unreal-acceptance.json').exists():
  outdoor=json.loads((OUT/'outdoor/unreal-acceptance.json').read_text('utf8'));assert outdoor['status']=='passed';note+=f'\n校內運動館南側新增400m一圈、8道操場，中央90×60m足球場，北側坡道連接校內主步道；保留河東800m一圈操場、4面網球場、4面排球場、兩處學生公園、活動草坪與接橋緩坡／落地支柱。戶外實際Unreal碰撞另通過{outdoor["checks"]}項；使用已remesh板件／柱件組裝，曲線跑道單獨Rust處理。詳見campus-v1/outdoor/index.html及同目錄驗收。\n'
 for path in [ROOT/'model3d.md',ROOT/'Docs/StreetSocialCity/ERRORS_AND_PREVENTION.md']:
  text=path.read_text('utf8');start='<!-- campus-delivery-summary:start -->';end='<!-- campus-delivery-summary:end -->';block=start+note+'\n'+end
  if start in text and end in text:text=re.sub(re.escape(start)+r'.*?'+re.escape(end),lambda _:block,text,flags=re.S)
  else:text+='\n\n'+block+'\n'
  write_text(path,text)
 page=OUT.parent/'world-ground-v1/index.html'
 if page.exists():
  text=page.read_text('utf8');text=text.replace('本批組裝店面與地面；學校、百貨等其他建物仍保留規畫位置。','目前已加入17棟／89層校園，既有96店／288層維持原位；百貨等其他建物仍依原計畫分批製作。')
  marker='<p id="campus-current"><a href="../campus-v1/index.html">最新校園模型、逐層平面與完整交付驗收</a></p>'
  if 'id="campus-current"' not in text:text=text.replace('</html>',marker+'</html>')
  for row in g['chunks']:
   n=sum(json.loads(Path(row[k]['remesh_report']).read_text('utf8'))['triangles'] for k in ['ground','water'] if row.get(k));x,y,_=row['world_origin_m'];cid=row['id'];new=f'<tr><td>{cid}</td><td>{x:g}, {y:g}</td><td>275×275m</td><td>{n:,}</td><td><a href="chunks/{cid}/remesh/remeshed-candidate.glb">地面GLB</a></td></tr>'
   text=re.sub(r'<tr><td>'+cid+r'</td>.*?</tr>',lambda _:new,text)
  write_text(page,text)
try:
 if args.finish_only:
  for path in [OUT/'world-acceptance.json',OUT/'terrain-paving-acceptance.json',OUT/'ground-overlap-acceptance.json',OUT/'unreal-acceptance.json',OUT.parent/'world-ground-v1/unreal-interioraudit.json']:
   assert json.loads(path.read_text('utf8'))['status']=='passed',str(path)
  completed.append('resume existing geometry/engine evidence; exact versions checked again by complete delivery gate')
 else:
  stage('actual certified paving overlap preflight',lambda:run('Tools/accept_street_city_paving_geometry.py',OUT))
  stage('final complete campus render and Blender geometry acceptance',final_render)
  stage('observed coplanar paving regression',lambda:run('Tools/diagnose_street_city_ground_overlap.py',OUT))
  lua=str(ROOT/'Tools/lua/lua.exe')
  stage('Unreal current campus and terrain import/collision',lambda:shell(lua,ROOT/'Tools/run_street_city_unreal.lua','--stage','Campus'))
  stage('Unreal preserved shop collision regression',lambda:shell(lua,ROOT/'Tools/run_street_city_unreal.lua','--stage','InteriorAuditCollision','--interior-after'))
 stage('publish final methods and ground navigation',publish_notes)
 stage('bind and verify complete current delivery',lambda:shell(sys.executable,ROOT/'Tools/verify_street_city_campus_delivery.py'))
 write_text(OUT/'delivery-progress.json',json.dumps(dict(status='passed',completed=completed),indent=2));print('Complete current school delivery verified',flush=True)
except Exception:
 write_text(OUT/'delivery-progress.json',json.dumps(dict(status='failed',phase=phase,completed=completed,error=traceback.format_exc()),indent=2));raise
