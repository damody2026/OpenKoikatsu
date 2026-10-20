"""Keep the two original hand-authored reviews consistent with accepted remesh exports."""
import json,re
from pathlib import Path
from street_city_building_context import ROOT
from street_city_remesh_gate import verify,ENGINE,sha

def publish():
 for bid in ['S01','S02']:
  folder=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building'
  a=json.loads((folder/'acceptance.json').read_text('utf8'));r=json.loads((folder/'assembly-report.json').read_text('utf8'))
  assert a['status']=='passed' and r['approved'] and sha(folder/f'{bid}-complete-2k.glb')==a['sha256']
  for q in r['sources'].values():assert verify(Path(q['remesh_report']).parent,q['sha256'])['tool_sha256']==q['tool_sha256']==sha(ENGINE)
  path=folder/'index.html';text=path.read_text('utf8');counts=[a['floor_triangles'][str(n)] for n in [1,2,3]];i=iter(counts)
  text,n=re.subn(r'[0-9][0-9,]*(\s*/\s*100,000)',lambda m:format(next(i),',')+m[1],text);assert n==3
  def status(m):
   part,n=re.subn(r'整棟[0-9,]+三角面','整棟'+format(sum(counts),',')+'三角面',m[0]);assert n==1
   part,n=re.subn(r'[0-9][0-9,]*(?=項)',format(a['checks'],','),part);assert n==1
   return part
  text,n=re.subn(r'<p class="status">.*?</p>',status,text,flags=re.S);assert n==1
  marker='<p id="remesh-current">全部元件已使用新版Rust remesh並重新組裝驗收。工具SHA：'+sha(ENGINE)+'。<a href="../../remesh-refresh-'+sha(ENGINE)[:12]+'/progress.json">本次逐元件執行記錄</a></p>'
  text=re.sub(r'<p id="remesh-current">.*?</p>','',text,flags=re.S);text=text.replace('</h1>','</h1>'+marker,1)
  path.write_text(text,'utf8');actual=[int(x.replace(',','')) for x in re.findall(r'([0-9][0-9,]*)\s*/\s*100,000',path.read_text('utf8'))];assert actual==counts
  from publish_street_city_shop_wall_styles import decorate_review
  decorate_review(bid,folder)
  print(bid,'published current',counts,flush=True)

if __name__=='__main__':publish()
