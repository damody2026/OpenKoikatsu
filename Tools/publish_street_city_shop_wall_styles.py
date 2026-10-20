"""Browse every actual shop finish, its research and unchanged-geometry proof."""
import json,html,re
from pathlib import Path
from street_city_shop_wall_styles import OUT,REFERENCES,profile,verify_metadata,sha
from street_city_building_context import ROOT

def decorate_review(bid,folder):
 path=folder/'index.html'
 if not path.exists():return
 r=json.loads((folder/'assembly-report.json').read_text('utf8'));verify_metadata(bid,r);p=r['wall_style']['profile']
 block='<section id="owner-wall-style"><h2>店主牆面風格</h2><p>'+html.escape(p['description'])+'。'+html.escape(p['wear'])+'。</p><p><a href="wall-style-report.json">逐面材質與原始幾何證據</a> · <a href="../../shop-wall-styles-v1/index.html">96間店風格總覽</a> · <a href="../../shop-brand-grok-v2/index.html">Grok Logo與材質原圖</a></p><img style="width:100%" src="'+bid+'-wall-detail.png"></section>'
 text=re.sub(r'<section id="owner-wall-style">.*?</section>','',path.read_text('utf8'),flags=re.S)
 text=text.replace('</h1>','</h1>'+block,1);path.write_text(text,'utf8')

def publish():
 from street_city_shop_street import inventory
 rows=[];cards=[]
 for b in inventory():
  bid=b['building'];folder=ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building';p=profile(bid);status='待驗收'
  try:
   r=json.loads((folder/'assembly-report.json').read_text('utf8'));a=json.loads((folder/'acceptance.json').read_text('utf8'));verify_metadata(bid,r);assert a['status']=='passed' and r['approved'] and a['sha256']==sha(folder/f'{bid}-complete-2k.glb')
   view=json.loads((folder/'wall-style-render.json').read_text('utf8'));assert view['model_sha256']==a['sha256'] and all(sha(folder/f)==v for f,v in view['images'].items());status='驗收通過'
  except (OSError,KeyError,ValueError,AssertionError):pass
  rows.append(dict(building=bid,status=status,profile=p))
  rel='../'+folder.parent.name+'/assembly-building/'
  before='before/'+bid+'/'+bid+'-F01-cutaway.png'
  cards.append('<article><h2>'+bid+' '+html.escape(p['name'])+'</h2><p>'+html.escape(p['description'])+'</p><p>'+status+' · 店主配色 '+str(p['owner_variant']+1)+'</p><img loading="lazy" src="'+rel+bid+'-wall-detail.png"><details><summary>一樓全景／初始版本</summary><img loading="lazy" src="'+rel+bid+'-F01-cutaway.png"><p>初始版本</p><img loading="lazy" src="'+before+'"></details><p><a href="'+rel+'index.html">整棟與各樓層</a> · <a href="'+rel+'wall-style-report.json">材質／尺寸證據</a></p></article>')
 passed=sum(q['status']=='驗收通過' for q in rows)
 if passed==96:
  from PIL import Image,ImageDraw,ImageFont
  board=Image.new('RGB',(2000,1800),(28,40,38));dr=ImageDraw.Draw(board);font=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',19)
  for n in range(1,21):
   bid=f'S{n:02d}';q=profile(bid);image=Image.open(ROOT/f'output/street-social-city-production/{bid}-modular-components-v1/assembly-building/{bid}-wall-detail.png').convert('RGB');image.thumbnail((490,315));x=(n-1)%4*500;y=(n-1)//4*360;board.paste(image,(x+(500-image.width)//2,y));dr.text((x+8,y+320),bid+' '+q['name'],font=font,fill=(235,230,210))
  board.save(OUT/'style-contact-sheet.jpg',quality=92)

 text='<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>商店街96店牆面風格</title><style>body{font:17px/1.7 system-ui;background:#1c2826;color:#eee7d7;margin:30px}a{color:#a7dcc1}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(420px,1fr));gap:22px}article{background:#2d3c37;padding:20px;border-radius:10px}img{width:100%;height:auto}summary{cursor:pointer}header{max-width:1200px;margin:auto auto 30px}</style><header><h1>商店街｜96間店的個人風格</h1><p>實際匯出與渲染驗收：'+str(passed)+'／96。20類用途；第二版由Grok Build逐店生成Logo及材質，以24種設計方向和不同字標配置拉開差異。特色牆圖像按整面牆長定位，樓上工作與居住空間安靜處理，衛浴使用可清洗表面。牆面使用公尺UV，貼圖最大2K；原始remesh幾何、門窗、走道、樓梯與面數不變。</p><p><a href="../shop-street-v1/index.html">商店街整合模型</a> · <a href="../shop-brand-grok-v2/index.html">Grok逐店原圖與證據</a> · <a href="RESEARCH.md">台灣實景參考與改編</a> · <a href="acceptance.json">本次總驗收</a> · <a href="progress.json">逐店進度</a></p></header>'+('<img style="width:100%" src="style-contact-sheet.jpg">' if passed==96 else '')+'<main>'+''.join(cards)+'</main></html>'
 OUT.mkdir(exist_ok=True);(OUT/'index.html').write_text(text,'utf8');(OUT/'inventory.json').write_text(json.dumps(dict(count=96,passed=passed,buildings=rows),ensure_ascii=False,indent=2),'utf8')
 research='# 商店街逐店牆面風格\n\n參考台灣店家及設計單位公開案例的材質、配色與品牌語言；未複製照片、商標或圖像。依20類用途做原創遊戲場景改編，不宣稱96間店分別是實店測繪。\n\n'+'\n'.join('- ['+q['title']+']('+q['url']+')：'+q['use'] for q in REFERENCES)+'\n\n## 96間店的個別配置\n\n'+'\n'.join('- '+q['building']+' '+q['profile']['name']+'：'+q['profile']['description']+'；主色'+str(q['profile']['main'])+'、特色牆'+str(q['profile']['accent']) for q in rows)
 (OUT/'RESEARCH.md').write_text(research,'utf8');return passed

if __name__=='__main__':publish()
