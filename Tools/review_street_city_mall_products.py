"""Contact boards and honest production gallery for bounded merchandise."""
import json,html
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
from street_city_mall_merchandise import OUT,publish
from street_city_remesh_gate import sha

def main():
 data=publish();rows=data['components'];cards=[];boards=[];receipt=json.loads((OUT/'previews/receipt.json').read_text('utf8'))
 font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',22)
 for start in range(0,len(rows),9):
  board=Image.new('RGB',(1200,1290),'#20252d');draw=ImageDraw.Draw(board)
  for i,row in enumerate(rows[start:start+9]):
   x=(i%3)*400;y=(i//3)*430;path=OUT/'previews'/(row['id']+'.png')
   if not path.exists():continue
   assert sha(path)==receipt['images'][row['id']]['image_sha256']
   im=Image.open(path).convert('RGB');im.thumbnail((394,394));board.paste(im,(x,y));draw.text((x+8,y+396),row['id'],font=font,fill='white')
  path=OUT/'previews'/f'board-{start//9+1:02d}.jpg';board.save(path,quality=92);boards.append(dict(path=str(path),sha256=sha(path)))
 for q in rows:
  im=OUT/'previews'/(q['id']+'.png');record=receipt['images'].get(q['id'])
  cards.append('<article><h2>'+html.escape(q['id'])+'</h2>'+('<img loading="lazy" src="previews/'+q['id']+'.png?v='+record['image_sha256'][:12]+'">' if record else '<p>尚未完成模型檢視</p>')+'<p>'+html.escape(str(record['actual_size_m'] if record else q['size']))+' m</p><p>'+html.escape(q['provider'])+' · '+str(q.get('triangle_cap','reuse'))+' 三角面上限</p></article>')
 (OUT/'index.html').write_text('<!doctype html><meta charset="utf-8"><title>M1 商品模型</title><style>body{background:#181c23;color:#eee;font:16px system-ui;margin:32px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px}article{background:#262d37;padding:16px;border-radius:12px}img{width:100%}</style><h1>百貨商品模型 '+str(len(rows))+' / 100 種</h1><p>以下圖片是通過 remesh 的實際模型渲染，非概念圖。逐櫃組裝與驗收另有獨立狀態。</p><main>'+''.join(cards)+'</main>','utf8')
 (OUT/'previews/boards.json').write_text(json.dumps(dict(status='requires_visual_review',boards=boards),indent=2),'utf8')

if __name__=='__main__':main()
