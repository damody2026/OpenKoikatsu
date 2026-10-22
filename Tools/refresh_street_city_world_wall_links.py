"""Keep the original city review navigable while distinguishing pre-wall geometry evidence."""
import json,os
from pathlib import Path
from street_city_world_wall_art import OUT
def main():
 page=OUT.parent/'world-completion-v1/index.html';s=page.read_text('utf8');models={q['building']:q for q in json.loads((OUT/'models.json').read_text('utf8'))['buildings']}
 for bid in ['H1','R1','R2','O01','O02','O03','CV','TR']:
  q=models[bid]
  for old,new in [(f'buildings/{bid}/{bid}-complete-2k.glb',OUT/'models'/(bid+'-three-languages.glb')),(f'buildings/{bid}/{bid}-complete-2k.blend',Path(q['model']).with_suffix('.blend')),(f'../localization-v1/models/{bid}-three-languages.glb',OUT/'models'/(bid+'-three-languages.glb'))]:
   s=s.replace('href="'+old+'"','href="'+os.path.relpath(new,page.parent).replace('\\','/')+'"')
 s=s.replace('>本層模型</a>','>本層原始幾何</a>')
 note='<p class="wall-version-note">以下逐層剖視圖保留換牆前的幾何驗收記錄；最新牆面圖樣與首層預覽請看<a href="../world-wall-grok-v1/index.html">Grok 主題牆面</a>，完整模型下載已連到新版三語材質。</p>'
 if 'class="wall-version-note"' not in s:s=s.replace('<div class="floors">',note+'<div class="floors">')
 page.write_text(s,'utf8')
if __name__=='__main__':main()
