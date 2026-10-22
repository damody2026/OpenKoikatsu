"""Inspectable wall library, actual model renders and versioned material downloads."""
import json,os,html
from PIL import Image,ImageOps
from street_city_world_wall_art import ROOT,OUT,sha
def rel(p):return os.path.relpath(p,OUT).replace('\\','/')
def link(p,label):return '<a href="'+html.escape(rel(p),quote=True)+'">'+html.escape(label)+'</a>'
def main():
 data=json.loads((OUT/'materials.json').read_text('utf8'));assert len(data['images'])==90;names={q['id']:q['name'] for q in json.loads((ROOT/'Docs/StreetSocialCity/production/building-floor-audit.json').read_text('utf8'))['buildings']};cards=[]
 installed=OUT/'native-install.json';used={q[k] for q in json.loads(installed.read_text('utf8')).get('slots',[]) for k in ['interior','exterior']} if installed.exists() else set()
 roles={'facade':'外牆','lobby':'大廳／公共藝術牆','quiet':'安靜區／住宅房間','wet':'濕區耐洗牆','interior':'室內主題牆','public':'百貨公共區','gallery':'精品樓層公共區','restaurant':'餐飲樓層公共區'}
 for q in data['images']:
  folder=OUT/'previews'/q['id'];folder.mkdir(parents=True,exist_ok=True);thumb=folder/'thumb.jpg';tile=folder/'tile-2x2.jpg'
  im=Image.open(q['maps']['basecolor']['path']).convert('RGB');ImageOps.contain(im,(600,600)).save(thumb,quality=91);small=im.resize((512,512),Image.Resampling.LANCZOS);sheet=Image.new('RGB',(1024,1024))
  for x in [0,512]:
   for y in [0,512]:sheet.paste(small,(x,y))
  sheet.save(tile,quality=94);label=names.get(q['building'],q['building'])+' · '+roles[q['role']]+(' · 已套用' if q['id'] in used else ' · 圖庫備選')
  cards.append('<article data-type="'+q['id'].split('/')[0]+'"><h3>'+html.escape(label)+'</h3><img loading="lazy" src="'+rel(thumb)+'"><p>2×2 公尺 · 2048² · '+html.escape(q['id'])+'</p><p>'+link(q['maps']['basecolor']['path'],'底色')+' · '+link(q['maps']['normal']['path'],'法線')+' · '+link(q['maps']['roughness']['path'],'粗糙度')+' · '+link(tile,'2×2 拼接檢視')+' · '+link(OUT/'assets'/q['id']/'concept.jpg','Grok 原圖')+'</p><details><summary>材質主題與用途</summary>'+html.escape(q['brief'])+'</details></article>')
 models=[]
 for receipt in (OUT/'buildings').glob('*/model.json'):
  r=json.loads(receipt.read_text('utf8'));assert r['status']=='passed' and sha(r['model'])==r['model_sha256'];models.append(r)
 modelcards=[]
 for r in sorted(models,key=lambda q:(q['building'] not in ['H1','R1','R2','O01','O02','O03','CV','TR'],q['building'])):
  bid=r['building'];lang=OUT/'models'/(bid+'-three-languages.glb');close=OUT/'buildings'/bid/'wall-closeup.png';hero=close if close.exists() else r['exterior']
  def im(path):return '<img loading="lazy" src="'+rel(path)+'">'
  previews=im(hero)+'<details><summary>查看外觀與首層剖視</summary>'+im(r['exterior'])+im(r['interior'])+'</details>'
  downloads=link(r['model'],'更新牆面 GLB（繁體）')+' · '+link(__import__('pathlib').Path(r['model']).with_suffix('.blend'),'Blender')+(' · '+link(lang,'三語 GLB') if lang.exists() else '')
  modelcards.append('<article><h3>'+html.escape(bid+' '+names[bid])+'</h3>'+previews+'<p>'+downloads+'</p><p>本棟 '+str(len(r['texture_ids']))+' 種新材質；牆厚、門洞、樓梯與幾何不變。</p></article>')
 final=OUT/'acceptance.json';complete=final.exists() and json.loads(final.read_text('utf8'))['status']=='passed';status='90 張新圖面已完成，122 棟建物套用與原生驗收通過。' if complete else f'90 張新圖面已完成；模型與原生驗收進行中，目前 {len(models)}/122 棟展示模型完成。'
 page='<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><title>大學城・Grok 主題牆面</title><style>body{margin:32px auto;max-width:1700px;padding:0 24px;font:17px/1.7 system-ui;background:#f1eee7;color:#29342e}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(330px,1fr));gap:20px}article{background:#fff;padding:18px;border-radius:14px}h1,h2,h3{line-height:1.35}img{width:100%;border-radius:8px}a{color:#256b60}button{padding:10px 18px;margin:5px;border:1px solid #aca;background:#fff;border-radius:8px;cursor:pointer}.note{background:#e0eadf;padding:20px;border-radius:12px}details{font-size:14px}</style><h1>大學城・Grok 主題牆面</h1><p class="note">'+status+'</p><p>8 棟城市新建築 ×4、17 棟校舍 ×2、商店街 20 種補充外牆、百貨公共區 4 種。既有商店品牌與 80 個高級專櫃的個別裝潢保留。新圖無文字，三語招牌另外切換。</p><p>底色由 Grok Build 真正生圖；法線與粗糙度為明確標示的近似輔助貼圖。2 公尺實際重複尺度；水平地板與天花保留原材質，圖案只套垂直牆面。</p><p>'+link(OUT/'visual-material-review.json','90 張圖面檢查')+' · '+link(ROOT/'Docs/StreetSocialCity/ERRORS_AND_PREVENTION.md','錯誤與預防紀錄')+(' · '+link(final,'完整驗收') if complete else '')+'</p><p><a href="#materials-heading">跳到90張材質圖庫</a> · <a href="#models-heading">建築套用效果</a></p><h2 id="models-heading">實際建築模型</h2><div class="grid">'+''.join(modelcards)+'</div><h2 id="materials-heading">90 張材質圖庫</h2><p><button onclick="filter(\'all\')">全部</button><button onclick="filter(\'city\')">飯店／住宅／辦公</button><button onclick="filter(\'campus\')">學校</button><button onclick="filter(\'shops\')">商店街</button><button onclick="filter(\'mall\')">百貨</button></p><div class="grid" id="materials">'+''.join(cards)+'</div><script>function filter(k){document.querySelectorAll("#materials article").forEach(e=>e.hidden=k!=="all"&&e.dataset.type!==k)}</script></html>'
 gpu=OUT/'native-render.json'
 if gpu.exists():
  g=json.loads(gpu.read_text('utf8'));page=page.replace('</html>','<h2>Unreal 真實 GPU 材質擷取</h2><p>BaseColor GBuffer 用於核對實際材質圖樣與編譯；上方 Blender 模型預覽呈現打光後的PBR效果。</p><div class="grid">'+''.join('<article><h3>'+q['actor']+'</h3><img loading="lazy" src="'+rel(q['image'])+'"></article>' for q in g['images'])+'</div></html>')
 (OUT/'index.html').write_text(page,'utf8');print('Wall gallery',len(models),'models,90 materials',flush=True)
if __name__=='__main__':main()
