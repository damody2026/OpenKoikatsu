"""Publish actual 80-store overview and stocked display detail evidence."""
import json,html
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from street_city_mall import OUT as MALL,sha
from street_city_mall_merchandise import OUT

def main():
 g=json.loads((MALL/'geometry.json').read_text('utf8'));s=g['contract']['merchandise'];r=json.loads((OUT/'stores/receipt.json').read_text('utf8'));assert r['status']=='passed' and r['model_sha256']==sha(MALL/'M1-complete-2k.glb');wide=json.loads((MALL/'boutiques-v1/render-receipt.json').read_text('utf8'));assert wide['status']=='passed' and wide['model_sha256']==r['model_sha256'];cards=[];boards=[]
 for room in g['contract']['rooms']:
  rid=room['id'];photo=r['images'][rid];overview=wide['images'][rid];path=Path(photo['path']);assert sha(path)==photo['sha256']
  goods=sorted({q['product_id'] for q in s['placements'] if q['tenant_id']==rid})
  cards.append('<article><h2>'+html.escape(rid+' '+room['name'])+'</h2><img loading="lazy" src="../boutiques-v1/previews/'+rid+'.png?v='+overview['sha256'][:12]+'"><img loading="lazy" src="stores/'+rid+'.png?v='+photo['sha256'][:12]+'"><p>'+str(s['tenant_counts'][rid])+' 件陳列實例 · '+html.escape('、'.join(goods))+'</p></article>')
 for floor in range(1,11):
  rows=[q for q in g['contract']['rooms'] if q['floor']==floor];board=Image.new('RGB',(1100,1650),'#20252d');draw=ImageDraw.Draw(board);font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',18)
  for i,room in enumerate(rows):
   im=Image.open(r['images'][room['id']]['path']).convert('RGB');im.thumbnail((545,375));x=(i%2)*550;y=(i//2)*410;board.paste(im,(x,y));draw.text((x+8,y+378),room['id'],font=font,fill='white')
  path=OUT/'stores'/f'F{floor:02d}-board.jpg';board.save(path,quality=93);boards.append(dict(path=str(path),sha256=sha(path)))
 (OUT/'stores/boards.json').write_text(json.dumps(dict(status='requires_visual_review',model_sha256=r['model_sha256'],boards=boards),indent=2),'utf8')
 (OUT/'stocked-stores.html').write_text('<!doctype html><meta charset="utf-8"><title>M1 真實商品陳列</title><style>body{background:#191d25;color:#eee;font:16px system-ui;margin:28px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(480px,1fr));gap:24px}article{background:#282f39;padding:18px;border-radius:12px}img{width:100%;margin:6px 0}p{line-height:1.5}a{color:#f3d999}</style><h1>百貨公司：80 櫃商品陳列</h1><p>'+str(s['unique_models'])+' 種商品模型 / 100 種上限；'+str(s['instances'])+' 件實際陳列。每櫃上圖為空間總覽，下圖為組裝後商品近景，全部由實際模型渲染。</p><p><a href="index.html">57 種商品模型及面數</a> · <a href="assembly-acceptance.json">尺寸與支撐驗收</a></p><main>'+''.join(cards)+'</main>','utf8')

if __name__=='__main__':main()
