"""80 premium concessions: authored programmes, material palettes and joinery.

All architectural pieces instance the certified Rust-remeshed panel. Material
maps are original procedural PBR, not photographs or product scans.
"""
import json,math,hashlib,re
from functools import lru_cache
from pathlib import Path
from street_city_localization import ROOT,LOCALES,path_for,render,sha
OUT=ROOT/'output/street-social-city-production/mall-v1/boutiques-v1'
REFERENCES=[dict(title='台北101官方精品旗艦店實景與設計介紹',url='https://stage.taipei101mall.com.tw/article/862',use='石材與展示架呼應、背光格紋、織物沙龍、木作與金屬細節；只取材質與工藝邏輯，不複製商標'),dict(title='Aesop 新光三越 A8 空間設計',url='https://shop.aesop.com/fr/en/r/aesop-mitsukoshi-a8/',use='台灣圖書館意象、整體材質敘事與品牌一致性'),dict(title='BELLAVITA 官方環境欣賞',url='https://www.bellavita.com.tw/cht/tour.php',use='精品百貨公共空間品質與櫃位的精緻協調')]
# Eight architectural languages, deliberately different constructions rather
# than eight paint colours on an otherwise identical booth.
FAMILIES=[('bookmatch','對花石材藝廊'),('fluted','垂直木作沙龍'),('tailored','織物鑲框沙龍'),('lacquer','漆面層疊展廊'),('precision','精密金屬格架'),('niche','壁龕收藏展間'),('arched','拱框工藝展廳'),('onyx','透光石材會客廳')]
PALETTES=[('ivory',[223,216,199],[154,126,77],[89,60,42],[174,165,147]),('ink',[61,66,70],[158,156,146],[77,61,50],[110,116,119]),('sage',[159,173,153],[133,113,77],[121,92,62],[187,193,174]),('rose',[196,163,151],[169,129,105],[116,75,61],[187,150,145]),('pearl',[224,224,215],[159,165,162],[164,145,116],[177,188,187]),('wine',[115,70,74],[163,131,75],[72,47,39],[144,110,112]),('navy',[75,99,115],[144,144,132],[115,79,48],[132,151,160]),('honey',[200,174,126],[142,117,69],[126,90,45],[204,188,157])]
# Rows correspond to the actual existing eight tenant programmes on each floor.
MOTIFS=[[7,3,0,4,5,1,6,2],[2,0,3,6,4,7,1,5],[1,2,3,4,5,0,6,7],[4,1,2,5,3,6,0,7],[1,7,2,4,0,6,3,5],[1,0,5,4,2,3,7,6],[4,0,3,2,1,7,5,6],[5,4,7,0,3,2,6,1],[1,7,5,4,6,0,3,2],[6,1,5,2,0,7,3,4]]
PALETTE_ROWS=[[5,1,2,4,0,7,3,6],[2,0,1,3,6,5,7,4],[7,6,1,4,5,0,2,3],[4,7,3,5,6,1,0,2],[7,0,3,1,4,6,2,5],[7,0,6,4,3,1,2,5],[4,1,6,7,0,5,2,3],[6,7,5,2,1,4,3,0],[7,5,2,1,3,0,4,6],[0,7,2,5,4,3,6,1]]

@lru_cache(maxsize=1)
def profiles():
 from street_city_mall import contract
 rows={}
 for s in contract()['rooms']:
  f=s['floor'];i=int(s['id'][-2:])-1;family=MOTIFS[f-1][i];palette=PALETTE_ROWS[f-1][i];p=PALETTES[palette]
  rows[s['id']]=dict(id=s['id'],name=s['name'],floor=f,bounds=s['bounds'],family=FAMILIES[family][0],style=FAMILIES[family][1],palette=p[0],palette_index=palette,wall='lux_wall_'+s['id'].replace('-','_'),case='lux_case_'+p[0],trim='lux_trim_'+p[0],fabric='lux_fabric_'+p[0],floor_finish='lux_floor_'+p[0],stone='lux_stone_'+p[0],lighting_kelvin=3500 if f==7 else 3000,display_design=['背光香水陳列／試香檯','專業試妝鏡／漆面檯','石材試用島／收納櫃','精密金屬工具展示','低反射玻璃珠寶櫃／絨布托盤','木作鐘錶櫃／金屬收邊','階梯式花藝台／工藝屏風','玻璃精品展示／織物背板'][i] if f==1 else '織物襯板／金屬衣架／訂製展示檯' if f in (2,3) else '背光壁龕／皮革襯墊／玻璃陳列櫃' if f==4 else '情境展示／木作陳列／石材服務檯' if f==5 else '藏書式木作／文創收藏陳列' if f==6 else '精密金屬體驗桌／隱藏線槽／展品壁龕' if f==7 else '收藏級玻璃展示／精緻親子體驗桌' if f==8 else '訂製餐桌／織物座椅／餐飲主題牆／開放前場與獨立廚房',sources=[r['url'] for r in REFERENCES])
 return rows

def specifications():
 specs={}
 for name,wall,metal,wood,fabric in PALETTES:
  for role,color,rough,kind,repeat in [('case',wood,.32,'wood',[1.8,1.8]),('trim',metal,.24,'metal',[1,1]),('fabric',fabric,.76,'fabric',[.6,.6]),('floor',[min(238,int(v*.35+150)) for v in wall],.34,'stone',[1.2,1.2]),('stone',[min(240,int(v*.55+100)) for v in wall],.28,'stone',[1.6,1.6])]:
   if role=='case':
    kind,color,rough={'ivory':('stone',[220,212,193],.25),'ink':('lacquer',[32,35,37],.18),'sage':('stone',[171,185,167],.3),'rose':('lacquer',[208,175,161],.22),'pearl':('metal',[191,198,196],.23),'wine':('lacquer',[93,50,60],.18),'navy':('wood',wood,.32),'honey':('wood',wood,.32)}[name]
   specs['lux_'+role+'_'+name]=dict(color=color,roughness=rough,kind=kind,repeat_m=repeat,metallic=.82 if role=='trim' else 0)
   if role=='case' and kind=='metal':specs['lux_'+role+'_'+name]['metallic']=.65
 for j,p in enumerate(profiles().values()):
  base=PALETTES[p['palette_index']][1];shift=(p['floor']-5)*1.3
  specs[p['wall']]=dict(color=[max(0,min(255,round(v+shift))) for v in base],roughness=.52 if p['family'] in ('tailored','fluted','arched') else .27,kind='fabric' if p['family']=='tailored' else 'wood' if p['family'] in ('fluted','arched') else 'lacquer' if p['family']=='lacquer' else 'metal' if p['family']=='precision' else 'stone',repeat_m=[2,2],metallic=.25 if p['family']=='precision' else 0,seed=j+313)
 return specs

def generate():
 import numpy as np
 from PIL import Image
 OUT.mkdir(parents=True,exist_ok=True);folder=OUT/'materials';folder.mkdir(exist_ok=True);specs=specifications();n=1024
 yy,xx=np.mgrid[:n,:n].astype('float32')/n;generator=sha(__file__);records={}
 for i,(key,s) in enumerate(specs.items()):
  signature=hashlib.sha256(json.dumps([s,generator],sort_keys=True).encode()).hexdigest();meta=folder/(key+'.json')
  if meta.exists():
   old=json.loads(meta.read_text('utf8'))
   if old.get('signature')==signature and all(Path(v['path']).exists() and sha(v['path'])==v['sha256'] for v in old['maps'].values()):records[key]=old;continue
  rng=np.random.default_rng(s.get('seed',i+701));noise=rng.normal(0,1,(n,n)).astype('float32');kind=s['kind']
  if kind=='wood':height=np.sin(yy*math.tau*24+np.sin(xx*math.tau*2)*1.7)+.3*np.sin(yy*math.tau*97+np.sin(xx*math.tau*3));tint=height*5+noise*.5;micro=.00004
  elif kind=='stone':
   vein=np.sin(xx*math.tau*3+yy*math.tau*2+np.sin(yy*math.tau*3)*.6);fine=np.maximum(0,1-abs(vein)*28);height=fine;tint=-fine*22+np.sin(yy*math.tau*7)*1.6+noise*.4;micro=.000008
  elif kind=='fabric':height=np.sin(xx*math.tau*192)*np.sin(yy*math.tau*192);tint=height*2+noise*.3;micro=.000006
  elif kind=='metal':height=np.sin(yy*math.tau*320);tint=height*.8+noise*.15;micro=.000002
  else:height=noise*.1;tint=noise*.15;micro=.000001
  rgb=np.clip(np.array(s['color'])[None,None,:]+tint[:,:,None],0,255).astype('uint8');height*=micro
  dx=(np.roll(height,-1,1)-np.roll(height,1,1))/(2*s['repeat_m'][0]/n);dy=(np.roll(height,-1,0)-np.roll(height,1,0))/(2*s['repeat_m'][1]/n)
  normal=np.stack([-dx,dy,np.ones_like(dx)],2);normal/=np.linalg.norm(normal,axis=2)[:,:,None]
  maps={}
  for suffix,array in [('basecolor',rgb),('roughness',np.clip(s['roughness']*255+noise*.6,0,255).astype('uint8')),('normal',np.clip((normal*.5+.5)*255,0,255).astype('uint8'))]:
   path=folder/(key+'-'+suffix+'-2k.png');Image.fromarray(array).save(path);maps[suffix]=dict(path=str(path),sha256=sha(path),size=[n,n])
  row=dict(signature=signature,specification=s,maps=maps);meta.write_text(json.dumps(row,indent=2),'utf8');records[key]=row
 report=dict(status='generated',generator_sha256=generator,materials=records,tenants=list(profiles().values()),references=REFERENCES,texture_max_px=2048,texture_actual_px=1024,provenance='original metric procedural PBR; not scans')
 (OUT/'materials.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf8');prepare_signs();publish()

def prepare_signs():
 import shutil
 from street_city_localization import OUT as LANG
 for p in profiles().values():
  bg=PALETTES[p['palette_index']][1];fg=(238,232,216) if sum(bg)<480 else (40,43,41)
  for l in LOCALES:render('tenant.'+p['id'],l,path_for('tenant.'+p['id'],l),background=tuple(bg),foreground=fg)
  target=ROOT/'output/street-social-city-production/mall-v1/signs'/(p['id']+'.png');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path_for('tenant.'+p['id'],'zh-Hant'),target)
 if (LANG/'assets.json').exists():
  a=json.loads((LANG/'assets.json').read_text('utf8'))
  for row in a['rows']:
   if row['id'] in {'tenant.'+p for p in profiles()}:
    for l in LOCALES:row['variants'][l]['sha256']=sha(path_for(row['id'],l))
  (LANG/'assets.json').write_text(json.dumps(a,ensure_ascii=False,indent=2),'utf8')

def materials():
 import bpy
 data=json.loads((OUT/'materials.json').read_text('utf8'));result={}
 assert data['generator_sha256']==sha(__file__),'Regenerate boutique materials after recipe edits'
 for key,r in data['materials'].items():
  m=bpy.data.materials.new(key);m.use_nodes=True;bs=m.node_tree.nodes['Principled BSDF'];bs.inputs['Metallic'].default_value=r['specification']['metallic']
  for suffix,socket in [('basecolor','Base Color'),('roughness','Roughness'),('normal','Normal')]:
   q=r['maps'][suffix];assert sha(q['path'])==q['sha256'];node=m.node_tree.nodes.new('ShaderNodeTexImage');node.image=bpy.data.images.load(q['path'],check_existing=True);node.image.pack()
   if suffix!='basecolor':node.image.colorspace_settings.name='Non-Color'
   if suffix=='normal':
    normal=m.node_tree.nodes.new('ShaderNodeNormalMap');m.node_tree.links.new(node.outputs['Color'],normal.inputs['Color']);m.node_tree.links.new(normal.outputs['Normal'],bs.inputs[socket])
   else:m.node_tree.links.new(node.outputs['Color'],bs.inputs[socket])
  m['premium_material']=True;m['repeat_m']=r['specification']['repeat_m'];result[key]=m
 return result

def decorate(parts,c):
 by=profiles();original=list(parts)
 # Refinish existing furniture by ownership. Products, screens and sanitary
 # areas keep their purpose-specific materials; kitchens remain washable.
 for q in original:
  match=re.match(r'(M1-\d{4})-',q['name'])
  if not match:continue
  p=by[match[1]];q['tenant_id']=p['id']
  if q['kind']=='wall' or 'kitchen' in q['name'] or any(k in q['name'] for k in ('cookline','fridge','coffee-machine')):continue
  if q['kind']=='floor_finish':q['finish']=p['floor_finish']
  elif q['finish']=='wood':
   w,depth,h=q['size'];upholstered=(abs(w-.44)<.001 and ((abs(depth-.44)<.001 and h<.04) or (depth<.04 and abs(h-.32)<.001)))
   q['finish']=p['fabric'] if upholstered else p['stone'] if p['floor']==10 and p['palette'] in ('ivory','pearl','rose') and w>1 and depth>.6 and h<.06 else p['case']
  elif q['finish']=='steel':q['finish']=p['trim']
  elif q['finish']=='fabric':q['finish']=p['fabric']
  elif q['finish']=='ivory':q['finish']=p['stone']
 for p in by.values():
  a,b,cc,dd=p['bounds'];cx=(a+cc)/2;cy=(b+dd)/2;n=p['floor'];z=(n-1)*4.2;family=p['family']
  # Retail backwall is in a reserved rear strip beyond the furniture grid.
  # Restaurants receive a SIDE feature wall in the dining area, outside kitchens.
  yaw=0 if cy>0 else 180;ox,oy=cx,dd-.65 if cy>0 else b+.65
  width=12+(int(p['id'][-2:])%3)*.8
  if n>=9:ox,oy=cc-.65,cy+(-1 if cy>0 else 1);yaw=-90;width=8
  def add(tag,size,pos,finish=None,kind='boutique_detail',pitch=0):
   xx,yy,zz=transform_local(pos,[ox,oy,z],yaw)
   parts.append(dict(floor=n,name=p['id']+'-lux-'+tag,size=list(size),pos=[xx,yy,zz],finish=finish or p['wall'],kind=kind,yaw=yaw,primitive='panel',tenant_id=p['id'],pitch_y=pitch))
  # A complete branded interior: proper finished ceiling, side boundaries and
  # an OPEN front portal. No invented doors across the department-store aisle.
  parts.append(dict(floor=n,name=p['id']+'-lux-ceiling',size=[cc-a-.4,dd-b-.4,.035],pos=[cx,cy,z+3.89],finish='mallpaint',kind='boutique_ceiling',yaw=0,primitive='panel',tenant_id=p['id']))
  rear=dd-.4 if cy>0 else b+.4
  parts.append(dict(floor=n,name=p['id']+'-lux-rear-lining',size=[cc-a-.8,.12,3.89],pos=[cx,rear,z],finish=p['wall'],kind='boutique_wall',yaw=0,primitive='panel',tenant_id=p['id']))
  for x in [a+.45,cc-.45]:
   parts.append(dict(floor=n,name=p['id']+'-lux-side-lining',size=[.09,dd-b-2,1.1 if n==1 else 3.89],pos=[x,cy,z],finish=p['wall'],kind='boutique_wall',yaw=0,primitive='panel',tenant_id=p['id']))
  front=b+1 if cy>0 else dd-1
  for x in [a+.5,cc-.5]:parts.append(dict(floor=n,name=p['id']+'-lux-front-pier',size=[.16,.18,3.65],pos=[x,front,z],finish=p['trim'],kind='boutique_detail',yaw=0,primitive='panel',tenant_id=p['id']))
  parts.append(dict(floor=n,name=p['id']+'-lux-open-portal',size=[cc-a-1,.18,.12],pos=[cx,front,z+3.53],finish=p['trim'],kind='boutique_detail',yaw=0,primitive='panel',tenant_id=p['id']))
  # Every feature has a designed back, a proper base and an illuminated crown.
  add('feature-back',[width,.12,3.3],[0,0,.1],kind='boutique_wall')
  add('plinth',[width,.18,.12],[0,-.03,.1],p['trim'])
  add('crown',[width,.24,.1],[0,-.06,3.35],p['trim'])
  add('wall-wash',[width-.3,.045,.025],[0,-.14,3.32],'light','boutique_light')
  if family=='bookmatch':
   for x in [-width/4,width/4]:add('stone-leaf',[width/2-.08,.045,3],[x,-.09,.23],p['stone'])
   for x in [-width/2+.1,0,width/2-.1]:add('shadow-joint',[.025,.05,3],[x,-.12,.23],p['trim'])
  elif family=='fluted':
   for i in range(18):add('reed',[.055,.1,3],[(-.5+i/17)*(width-.4),-.12,.23],p['case'])
  elif family=='tailored':
   for x in [-width/3,0,width/3]:
    add('textile-panel',[width/3-.15,.045,2.9],[x,-.1,.3],p['fabric'])
    for dx in [-width/6+.07,width/6-.07]:add('panel-edge',[.025,.045,2.9],[x+dx,-.14,.3],p['trim'])
   for zz in [.28,3.2]:add('panel-rail',[width-.1,.045,.025],[0,-.14,zz],p['trim'])
  elif family=='lacquer':
   for i in range(5):
    h=2.3+.2*(i%3);add('stepped-panel',[width/5-.15,.08,h],[(i-2)*width/5,-.16,.4],p['wall']);add('floating-ledge',[width/5-.3,.35,.04],[(i-2)*width/5,-.25,.9+(i%2)*.45],p['trim'])
  elif family=='precision':
   for i in range(7):add('metal-upright',[.028,.1,3],[(i/6-.5)*(width-.2),-.15,.25],p['trim'])
   for zz in [.3,1.3,2.3,3.2]:add('metal-transom',[width-.2,.1,.028],[0,-.15,zz],p['trim'])
  elif family=='niche':
   for x in [-width*.3,0,width*.3]:
    for dx in [-1.15,1.15]:add('niche-cheek',[.09,.48,2.5],[x+dx,-.3,.45],p['case'])
    for zz in [.45,2.95]:add('niche-head',[2.4,.48,.09],[x,-.3,zz],p['case'])
    for zz in [1.2,2]:add('niche-glass',[2.2,.42,.022],[x,-.3,zz],'glass')
  elif family=='arched':
   for x in [-1.65,1.65]:add('arch-post',[.12,.14,1.35],[x,-.17,.25],p['trim'])
   for i in range(13):
    angle=math.pi*(i+.5)/13;add('arch-segment',[.43,.14,.12],[1.65*math.cos(angle),-.17,1.6+1.65*math.sin(angle)],p['trim'],pitch=90-math.degrees(angle))
   for x in [-width*.33,width*.33]:add('paper-screen',[width*.22,.05,2.8],[x,-.13,.35],p['fabric'])
  else:
   for i in range(3):add('onyx-leaf',[width/3-.18,.06,2.9],[(i-1)*width/3,-.13,.3],p['stone'])
   for x in [-width/6,width/6]:add('onyx-inlay',[.025,.03,2.9],[x,-.18,.3],p['trim'])
  # Two restrained ceiling rafts mark a branded zone without closing the bay.
  for x in [-3,3]:
   add('canopy',[2.4,2.8,.06],[x,-2.1,3.65],p['case'],'boutique_canopy')
   add('canopy-led',[2,.06,.025],[x,-2.7,3.62],'light','boutique_light')
   for dx in [-1,1]:add('canopy-hanger',[.015,.015,.18],[x+dx,-2.1,3.71],p['trim'],'boutique_detail')
  # Add genuine glass fronts and a velvet/stone tray to existing glass cases.
  groups={}
  for q in original:
   if q['name'].startswith(p['id']+'-display-'):
    group=re.sub(r'-\d+$','',q['name']);groups.setdefault(group,[]).append(q)
  for group,items in groups.items():
   top=next((q for q in items if q['finish']=='glass' and q['size'][2]<.025 and q['size'][0]>2),None)
   if top and n in (1,4):
    x,y,zz=top['pos'];w,d,_=top['size']
    for side in [-1,1]:parts.append(dict(floor=n,name=group+'-premium-glass',size=[w,.022,.3],pos=[x,y+side*(d/2-.011),zz-.3],finish='glass',kind='boutique_display',yaw=0,primitive='panel',tenant_id=p['id']))
    parts.append(dict(floor=n,name=group+'-premium-tray',size=[w-.15,d-.15,.012],pos=[x,y,zz-.285],finish=p['fabric'],kind='boutique_display',yaw=0,primitive='panel',tenant_id=p['id']))

def transform_local(pos,origin,yaw):
 c,s=math.cos(math.radians(yaw)),math.sin(math.radians(yaw));return [origin[0]+pos[0]*c-pos[1]*s,origin[1]+pos[0]*s+pos[1]*c,origin[2]+pos[2]]

def publish():
 import html,os
 cards=[]
 for p in profiles().values():
  colors=PALETTES[p['palette_index']][1:];swatches=''.join('<i style="background:rgb'+str(tuple(c))+'"></i>' for c in colors)
  image=OUT/'previews'/(p['id']+'.png');preview='<img src="previews/'+p['id']+'.png">' if image.exists() else ''
  cards.append('<article><h2>'+p['id']+' '+html.escape(p['name'])+'</h2><p>'+p['style']+' · '+p['palette']+'</p><div>'+swatches+'</div><p>'+p['display_design']+'</p>'+preview+'</article>')
 OUT.mkdir(parents=True,exist_ok=True)
 (OUT/'index.html').write_text('<!doctype html><meta charset="utf-8"><title>80 櫃高級百貨室內設計</title><style>body{background:#181b1e;color:#eee;font:17px Microsoft JhengHei,sans-serif;max-width:1500px;margin:30px auto}main{display:grid;grid-template-columns:repeat(2,1fr);gap:18px}article{background:#252a2b;padding:24px;border:1px solid #756b56}i{display:inline-block;width:23%;height:35px}img{width:100%}a{color:#dbc496}</style><h1>十層 · 八十個精品化專櫃</h1><p>石材對花、木作格柵、織物鑲框、漆面、精密金屬、收藏壁龕、拱形工藝及透光石材，依商品用途配對。材質為原創公尺制 PBR，非實景掃描；下方圖片為實際模型渲染。</p><p><a href="../index.html">百貨全棟</a> · <a href="acceptance.json">風格與模型驗收</a> · <a href="materials.json">80 櫃設計與材質規格</a> · <a href="RESEARCH.md">官方參考</a></p><main>'+''.join(cards)+'</main>','utf8')
 (OUT/'RESEARCH.md').write_text('# 精品專櫃參考\n\n'+'\n'.join('- ['+r['title']+']('+r['url']+')：'+r['use'] for r in REFERENCES)+'\n\n虛構專櫃依商品性質設計；實際尺寸與動線沿用本案合約，不宣稱為原品牌或實測復刻。工藝差異包含背牆構造、展示櫃材質、金屬收邊、天花區域、玻璃櫃與織物托盤，並保留開放櫃位。','utf8')

if __name__=='__main__':generate()
