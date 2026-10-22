-- Newly typeset glyphs; the original horizontal logos are not rotated.
-- Lua orchestration runs inline Pillow without adding a Python project script.
local code=[====[
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import json
p=Path('output/street-social-city-production/resort-architecture-v1')
names={'R1':{'zh-Hant':'青年租屋公寓','zh-Hans':'青年租屋公寓','en':'YOUTH APARTMENTS'},'R2':{'zh-Hant':'河景家庭公寓','zh-Hans':'河景家庭公寓','en':'RIVERSIDE HOMES'}}
rows=[]
for bid,locales in names.items():
 for locale,name in locales.items():
  im=Image.new('RGB',(512,2048),(244,238,219));d=ImageDraw.Draw(im)
  d.rounded_rectangle((22,22,490,2026),radius=24,outline=(145,108,65),width=6)
  chars=list(name.replace(' ',''));size=196 if locale!='en' else min(150,int(1740/len(chars)*.88));font=ImageFont.truetype('C:/Windows/Fonts/msjhbd.ttc',size)
  step=1740/len(chars)
  for i,ch in enumerate(chars):
   box=d.textbbox((0,0),ch,font=font);x=256-(box[2]+box[0])/2;y=154+(i+.5)*step-(box[3]+box[1])/2
   d.text((x,y),ch,font=font,fill=(28,66,59))
  dest=p/'vertical-signs'/locale/(bid+'.png');dest.parent.mkdir(parents=True,exist_ok=True);im.save(dest);rows.append({'building':bid,'locale':locale,'text':name,'path':str(dest),'layout':'upright stacked glyphs'})
(p/'vertical-lettering.json').write_text(json.dumps({'status':'passed','rows':rows},ensure_ascii=False,indent=2),'utf8')
print('Prepared 6 upright vertical sign textures')
]====]
local hex=code:gsub('.',function(c)return string.format('%02x',c:byte())end)
assert(os.execute('python -c "exec(bytes.fromhex(\''..hex..'\').decode(\'utf-8\'))"'))
