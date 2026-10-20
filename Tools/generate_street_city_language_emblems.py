"""Six Grok image atlases of 96 text-free symbols, shared by all locales."""
import concurrent.futures,json,shutil
from pathlib import Path
from PIL import Image
from street_city_localization import OUT,sha
from generate_street_city_grok_brands import spec
import generate_street_city_store_concepts as engine

def main():
 records=[]
 for batch in range(6):
  folder=OUT/'grok-emblems'/f'{batch+1:02d}';folder.mkdir(parents=True,exist_ok=True)
  designs=[spec(f'S{i:02d}') for i in range(batch*16+1,batch*16+17)]
  instructions='\n'.join(f'Cell {j+1}, row {j//4+1}, column {j%4+1}: {p["name"]}, a {p["family"]}; {p["language"]}; ORIGINAL SYMBOL ONLY, omit ALL typography.' for j,p in enumerate(designs))
  prompt='Use image_gen exactly ONCE. No shell, no subagents. Generate ONE square 4 by 4 icon atlas. Exactly 16 EQUAL square cells, perfectly aligned without gaps. Ivory solid background in every cell. Each symbol is centered in its cell, occupies at most 65 percent of cell width and height, generously inset. No grid lines, no frames, NO text, NO letters, NO numbers, NO Chinese characters, NO watermarks. Professional expressive original brand emblems with distinct silhouettes, bold colors, no gradients or shadows. Each cell is an independent original shop symbol, not an illustration of a room. Order is LEFT TO RIGHT, TOP TO BOTTOM. Follow these 16 unique art directions:\n'+instructions+'\nReturn only the absolute image file path.'
  (folder/'grok-prompt.txt').write_text(prompt,'utf8');(folder/'brief.json').write_text(json.dumps(designs,ensure_ascii=False,indent=2),'utf8')
  records.append(dict(id=f'{batch+1:02d}',tools='image_gen',design_sha256=sha(folder/'brief.json'),prompt_sha256=sha(folder/'grok-prompt.txt')))
 engine.CACHE=OUT/'grok-emblems';exe=shutil.which('grok.exe') or shutil.which('grok');assert exe
 def one(r):
  for _ in range(3):
   result=engine.one(r,exe)
   if result['status']=='generated_review_pending':return result
  raise RuntimeError(r['id'])
 with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:results=list(pool.map(one,records))
 crops=[]
 for batch,result in enumerate(results):
  source=OUT/'grok-emblems'/f'{batch+1:02d}'/'concept.jpg'
  with Image.open(source) as im:
   for j in range(16):
    x,y=j%4,j//4;box=(round(x*im.width/4),round(y*im.height/4),round((x+1)*im.width/4),round((y+1)*im.height/4));bid=f'S{batch*16+j+1:02d}';target=OUT/'emblems'/(bid+'.png');target.parent.mkdir(exist_ok=True);im.crop(box).save(target);crops.append(dict(building=bid,path=str(target),sha256=sha(target),atlas_sha256=sha(source),crop_px=list(box),generation=result))
 (OUT/'emblems.json').write_text(json.dumps(dict(status='generated_review_pending',emblems=crops),ensure_ascii=False,indent=2),'utf8')
if __name__=='__main__':main()
