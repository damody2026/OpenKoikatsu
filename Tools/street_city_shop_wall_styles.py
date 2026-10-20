"""Owner-specific, metric wall finishes on existing certified geometry.

No added collision surfaces, no geometric displacement and no painted doors.
All procedural graphics are original; reference projects inform material use.
"""
import json,hashlib,math
from pathlib import Path
from street_city_building_context import ROOT,load_manifest

OUT=ROOT/'output/street-social-city-production/shop-wall-styles-v1'
GROUPS={
 'coffee':[1,21,38,49,67,85], 'tea':[2,22,39,50,68,86],
 'bakery':[3,23,40,51,69,87], 'diner':[4,24,41,52,70,88],
 'produce':[5,25,42,53,71,89], 'convenience':[6,26,43,54,72,90],
 'book':[7,27,44,55,73,91], 'pet':[8,28,56,74,92],
 'floral':[9,29,45,57,75,93], 'fashion':[10,30,46,58,76,94],
 'salon':[11,31,47,59,77,95], 'music':[12,32,48,60,78,96],
 'games':[13,33,61,79], 'photo':[14,34,62,80], 'bike':[15,35,63,81],
 'pharmacy':[16,36,64,82], 'laundry':[17,37,65,83],
 'inn':[18], 'spa':[19], 'lottery':[20,66,84]}
# main pigment, contrasting feature, lower wall material, original graphic
FAMILIES={
 'coffee':('暖灰泥／胡桃木護牆／選書海報',[194,177,145],[66,88,76],'wood','READ / BREW'),
 'tea':('茶綠手工磚／米色礦物塗料／茶葉圖案',[219,210,177],[39,101,86],'tile','LEAF / TEA'),
 'bakery':('奶油抹灰／赤陶小磚／手繪麥穗',[231,210,175],[164,88,62],'tile','BAKE / DAILY'),
 'diner':('復古綠釉磚／米黃牆／手寫菜單',[219,209,174],[70,105,91],'tile','LOCAL / TABLE'),
 'produce':('淺橄欖灰泥／果色分格／產地圖案',[217,215,177],[115,139,64],'tile','SEASON / FRESH'),
 'convenience':('暖白耐洗牆／品牌色帶／社區告示',[219,223,207],[54,116,110],'tile','NEIGHBOURHOOD'),
 'book':('舊紙色灰泥／深木裙牆／書封版畫',[208,195,163],[86,76,62],'wood','BOOKS / STORIES'),
 'pet':('暖沙色牆／柔和色塊／原創爪印',[226,211,185],[95,137,125],'tile','PAWS / FRIENDS'),
 'floral':('石灰洗牆／苔綠拼色／植物線描',[202,210,181],[67,102,76],'plaster','FLOWER / STEM'),
 'fashion':('暖灰微水泥／陶粉拼色／服裝版型圖',[199,192,179],[133,94,82],'plaster','EDIT / FORM'),
 'salon':('奶茶礦物漆／灰藍護牆／幾何圖像',[212,199,181],[90,112,119],'wood','CUT / COLOUR'),
 'music':('暖磚牆／深色木板／唱片與演出海報',[179,144,115],[56,67,79],'brick','SOUND / VINYL'),
 'games':('深藍牆／紫橙色圖形／遊戲社群海報',[97,111,135],[99,73,119],'plaster','PLAY / TOGETHER'),
 'photo':('銀灰抹面／焦褐護牆／黑白相片圖組',[185,184,171],[84,82,75],'concrete','LIGHT / FRAME'),
 'bike':('清水模／工具色帶／自行車線稿',[169,174,168],[69,95,111],'concrete','RIDE / REPAIR'),
 'pharmacy':('淺綠耐洗牆／鼠尾草色磚／分類圖紋',[217,226,209],[111,143,123],'tile','CARE / EVERYDAY'),
 'laundry':('粉藍瓷磚／淺灰牆／洗滌圖形',[208,222,224],[86,139,157],'tile','WASH / FOLD'),
 'inn':('土色灰泥／木格護牆／靜謐山景版畫',[215,200,170],[116,103,77],'wood','STAY / SLOW'),
 'spa':('溫潤石紋／深礦物色／水波圖形',[196,190,168],[79,105,101],'stone','WATER / REST'),
 'lottery':('暖米牆／復古紅色帶／幾何幸運圖紋',[226,211,170],[155,67,56],'wood','LUCK / TODAY')}
REFERENCES=[
 dict(title='HAO DESIGN — ODD ONE OUT',url='https://www.haodesign.tw/odd-one-out',use='茶磚質感、軟木與品牌圖形形成個性；未複製品牌或照片'),
 dict(title='琢隱設計 — SOMA',url='https://www.drawindesignstudio.com/project/soma/',use='木紋水泥板與藍綠藝術磚的層次'),
 dict(title='知冉設計 — 來日方茶',url='https://knrndesign.co/project/rairai-tea',use='橘紅小磚、金屬及品牌視覺協調'),
 dict(title='誠品行旅',url='https://www.eslitehotel.com/',use='紅磚、木作與藝術收藏；閱讀／住宿材質參考'),
 dict(title='IN-Xian — envy hair studio',url='https://www.inxian-design.com/projects/envy-hair-studio',use='髮廊材質與反射形成視覺層次'),
 dict(title='花意空間',url='https://flowerspacewedding.com/關於花意空間/',use='花店陳列與空間個性'),
 dict(title='think — 果果迪',url='https://brandbythink.com/we_create/projects/degugu_果果迪',use='品牌圖案與室內牆面共享設計語言')]

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def profile(bid):
 n=int(bid[1:]);family=next(k for k,v in GROUPS.items() if n in v)
 title,main,accent,lower,motto=FAMILIES[family];index=GROUPS[family].index(n)
 b=next(q for q in load_manifest()['layouts'] if q.get('building')==bid and q.get('kind')=='building')
 name=b.get('small_shop_design',{}).get('name',{'S18':'日式民宿','S19':'溫泉會館'}.get(bid,bid))
 # Deterministic owner choices, rather than identical shops of the same trade.
 def tint(rgb,d):return [max(30,min(242,c+d[k])) for k,c in enumerate(rgb)]
 main=tint(main,[(index%3-1)*7,((index*2)%5-2)*4,(index%4-1)*5])
 accent=tint(accent,[(index%4-1)*8,(index%3-1)*9,((index*3)%5-2)*6])
 floors=b.get('small_shop_design',{}).get('floors',b.get('floors',[]))
 result=dict(version=1,building=bid,name=name,family=family,description=title,
  main=main,accent=accent,lower=lower,motto=motto,owner_variant=index,
  wall_height_m=max(f['height_m']-.3 for f in floors),period_m=[4,max(f['height_m']-.3 for f in floors)],feature_period_m=[b['footprint_m'][1],max(f['height_m']-.3 for f in floors)],
  wear='輕微接縫、抹刀紋與局部修補；外牆另有褪色與雨痕；衛浴維持可清洗',
  graphics='一樓特色牆原創線稿、海報與店名；樓上使用安靜材質，無重複營業海報',
  geometry='Material slots and UV only; no changed vertices, topology or collision')
 from street_city_grok_brand_materials import active
 brand=active(bid)
 if brand:
  result.update(version=2,grok_branding=brand,main=brand.get('main_rgb',main),accent=brand.get('accent_rgb',accent),description=brand['direction']+'／'+brand['material'],graphics='Grok Build逐店原創Logo與實際生成材質；無統一雙海報構圖')
 return result

def profile_hash(bid):return hashlib.sha256(json.dumps(profile(bid),sort_keys=True,ensure_ascii=False).encode()).hexdigest()

def draw_owner_print(art,p,j):
 from PIL import ImageDraw,ImageFont
 dr=ImageDraw.Draw(art);col=tuple(p['accent']) if j==0 else (222,207,170);family=p['family']
 dr.rectangle((3,3,296,321),outline=(57,65,57),width=5)
 def line(points,width=4):dr.line(points,fill=col,width=width)
 def oval(box,width=4):dr.ellipse(box,outline=col,width=width)
 if family=='coffee':
  if j==0:
   dr.rounded_rectangle((65,120,210,225),radius=12,outline=col,width=5);dr.arc((175,133,253,200),270,90,fill=col,width=5);line((48,242,235,242));dr.arc((90,50,130,125),260,100,fill=col,width=3);dr.arc((150,45,190,120),260,100,fill=col,width=3)
  else:
   line([(40,85),(145,110),(260,80),(260,230),(145,255),(40,230),(40,85)]);line((145,110,145,255))
   for y in [130,155,180]:line((62,y,123,y+13),2);line((168,y,235,y-12),2)
 elif family in ('tea','floral'):
  line((150,250,150,55))
  for k in range(5):
   y=65+k*31;side=1 if k%2 else -1;dr.arc((85 if side<0 else 150,y,150 if side<0 else 215,y+38),0,300,fill=col,width=4)
  if family=='floral':
   for dx,dy in [(0,-22),(-23,0),(23,0),(0,22)]:oval((135+dx,45+dy,165+dx,75+dy))
 elif family=='bakery':
  if j==0:
   for dx in [-43,0,43]:
    line((150+dx,250,140+dx,70),3)
    for k in range(5):oval((117+dx,80+k*25,140+dx,102+k*25),3);oval((140+dx,69+k*25,163+dx,91+k*25),3)
  else:
   dr.rounded_rectangle((48,110,252,225),radius=54,outline=col,width=5)
   for x in [90,145,200]:dr.arc((x-20,112,x+20,195),190,330,fill=col,width=5)
 elif family=='diner':
  font=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',24);dr.text((46,45),'今日菜單' if j==0 else '家常好味',font=font,fill=col)
  for k,word in enumerate(['季節小菜','今日湯品','家常定食']):dr.text((38,112+k*43),word,font=font,fill=col);line((40,148+k*43,255,148+k*43),1)
 elif family=='produce':
  for x,y,r in [(105,150,58),(193,185,52)]:oval((x-r,y-r,x+r,y+r),5);line((x,y-r,x+9,y-r-25),4);dr.arc((x+5,y-r-36,x+50,y-r-9),0,280,fill=col,width=4)
 elif family=='convenience':
  font=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',23);dr.text((42,35),'社區小訊息',font=font,fill=col)
  for k in range(3):
   y=90+k*54;dr.rectangle((38,y,260,y+43),outline=col,width=2);line((55,y+12,235,y+12),2);line((55,y+25,185,y+25),2)
 elif family=='book':
  line([(40,75),(145,103),(260,75),(260,220),(145,248),(40,220),(40,75)],5);line((145,103,145,248))
  for y in [123,151,179,207]:line((59,y,124,y+15),2);line((166,y,238,y-15),2)
 elif family=='pet':
  dr.ellipse((105,145,210,235),fill=col)
  for x,y in [(88,120),(125,92),(170,90),(213,119)]:dr.ellipse((x-18,y-25,x+18,y+25),fill=col)
 elif family=='fashion':
  line([(150,60),(150,86),(55,145),(245,145),(150,86)],4)
  line([(105,125),(85,180),(110,197),(115,250),(193,250),(198,197),(225,180),(204,125)],4)
 elif family=='salon':
  oval((50,190,112,252));oval((172,190,234,252));line((90,197,211,55),5);line((191,197,70,55),5);oval((137,139,155,157),3)
 elif family=='music':
  for r in [92,80,67,28,10]:oval((150-r,160-r,150+r,160+r),3 if r!=92 else 5)
  dr.text((40,35),'LIVE SESSIONS' if j else 'SIDE A / SIDE B',font=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',18),fill=col)
 elif family=='games':
  line([(65,95),(170,60),(245,115),(245,225),(140,260),(65,205),(65,95),(140,150),(245,115)],5);line((140,150,140,260));line((170,60,140,150))
  for x,y in [(97,145),(110,200),(175,180),(211,167),(175,225),(211,211)]:dr.ellipse((x-7,y-8,x+7,y+8),fill=col)
 elif family=='photo':
  dr.rectangle((35,75,265,240),outline=col,width=5);line([(48,218),(105,135),(147,185),(183,120),(253,218)],4);oval((68,96,103,131));dr.rectangle((55,38,119,63),outline=col,width=3)
 elif family=='bike':
  oval((24,163,116,255));oval((186,163,278,255));line([(70,209),(126,115),(179,209),(70,209),(199,110),(232,209)],5);line((126,115,112,99,145,99));line((199,110,195,85,222,85));oval((169,199,189,219),3)
 elif family=='pharmacy':
  dr.polygon([(130,55),(170,55),(170,105),(220,105),(220,145),(170,145),(170,195),(130,195),(130,145),(80,145),(80,105),(130,105)],outline=col,width=4)
  dr.rounded_rectangle((85,220,215,252),radius=16,outline=col,width=3);line((150,220,150,252),3)
 elif family=='laundry':
  dr.rounded_rectangle((55,55,245,250),radius=10,outline=col,width=5);line((55,95,245,95),3);oval((93,115,207,229),5);oval((74,67,88,81),2);line((145,75,218,75),3);dr.arc((110,145,190,210),0,180,fill=col,width=3)
 elif family=='inn':
  line([(25,210),(85,115),(130,172),(190,75),(275,210)],4);oval((40,65,77,102));line((35,240,265,240),3)
 elif family=='spa':
  for k in range(5):
   y=80+k*37;dr.arc((28,y,150,y+45),180,360,fill=col,width=4);dr.arc((149,y-1,270,y+44),0,180,fill=col,width=4)
 elif family=='lottery':
  for x,y in [(117,110),(184,110),(117,177),(184,177)]:oval((x-35,y-35,x+35,y+35),4)
  line((150,172,150,250));dr.text((38,38),'GOOD LUCK',font=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',24),fill=col)
 else:raise ValueError('Missing original graphic for '+family)
 word=p['motto'].split(' / ')[j%len(p['motto'].split(' / '))];dr.text((25,280),word,font=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',20),fill=col)

def generate(bid):
 import numpy as np
 from PIL import Image,ImageDraw,ImageFont,ImageFilter
 p=profile(bid);folder=OUT/'textures'/bid;folder.mkdir(parents=True,exist_ok=True)
 rng=np.random.default_rng(int(bid[1:])+20261018);n=1024
 coarse=rng.normal(0,1,(64,64));noise=np.array(Image.fromarray(coarse.astype('float32')).resize((n,n),Image.Resampling.BICUBIC))
 yy,xx=np.mgrid[:n,:n];height_m=p['wall_height_m'];height=(n-1-yy)/n*height_m;xm=xx/n*4
 normal_height=noise*.00035
 font=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',34);small=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',21)
 for role in ['main','feature','quiet','facade','wet']:
  n=2048 if role=='feature' else 1024;period=p['feature_period_m'] if role=='feature' else p['period_m']
  noise=np.array(Image.fromarray(coarse.astype('float32')).resize((n,n),Image.Resampling.BICUBIC));yy,xx=np.mgrid[:n,:n];height=(n-1-yy)/n*height_m;xm=xx/n*period[0];normal_height=noise*.00035
  base=np.array(p['main'] if role in ('main','quiet') else p['accent'] if role=='feature' else [224,226,212] if role=='wet' else [(c+180)//2 for c in p['main']],dtype=float)
  arr=np.broadcast_to(base,(n,n,3)).copy()+noise[:,:,None]*3.2
  lower=(height<1.05);pattern=p['lower'];relief=normal_height.copy()
  if role=='quiet':arr=arr*.4+np.array([230,221,204])*.6
  if role=='wet':pattern='tile';lower=np.ones((n,n),bool)
  if pattern=='wood':
   wood=np.array([116,89,62])+(np.sin(xm*80+noise*.8)*3+noise*4)[:,:,None]
   seams=np.minimum(xm%.18,.18-xm%.18)<.003;wood[seams]=[71,61,49]
   arr[lower]=wood[lower];relief[lower]+=np.where(seams,-.0015,0)[lower]
  elif pattern in ('tile','brick','stone'):
   tw,th=(.24,.075) if pattern=='brick' else (.15,.15) if role=='feature' else (.3,.3)
   row=np.floor(height/th);u=(xm+(row%2)*tw/2)%tw;v=height%th
   seams=(u<.0025)|(v<.0025);col=np.array(p['accent'] if role!='wet' else [216,224,217],dtype=float)
   if pattern=='brick':col=np.array([160,98,73])
   tiled=col+noise[:,:,None]*4+np.sin(row*2.8)[:,:,None]*3
   tiled[seams]=[173,171,153];arr[lower]=tiled[lower];relief[lower]+=np.where(seams,-.0012,0)[lower]
  elif pattern=='concrete':
   joints=(xm%.8<.003)|(height%1.2<.003);arr+=noise[:,:,None]*5;arr[joints]-=16;relief[joints]-=.001
  # A restrained cap and skirting are texture relief, never protruding into routes.
  if role not in ('wet','quiet'):
   trim=abs(height-1.05)<.012;arr[trim]=np.array(p['accent'])*.68
   baseboard=height<.1;arr[baseboard]=np.array(p['accent'])*.65
  if role=='facade':
   streak=np.maximum(0,np.sin(xm*31+1.5))*np.exp(-height*.7)*4
   arr-=streak[:,:,None];arr+=np.sin(xm*4)[:,:,None]*2
   patch=(xm>.24)&(xm<.78)&(height>1.4)&(height<1.64)
   arr[patch]+=5
  im=Image.fromarray(np.clip(arr,0,255).astype('uint8'));draw=ImageDraw.Draw(im)
  if role=='feature':
   # Original artwork is composed in physical aspect ratio, then resampled
   # into metric UV space. It appears once per wall, never every four metres.
   def paste_m(art,x0,z0,width,height):
    box=(round(x0/period[0]*n),round((1-(z0+height)/height_m)*n),round((x0+width)/period[0]*n),round((1-z0/height_m)*n))
    im.paste(art.resize((box[2]-box[0],box[3]-box[1]),Image.Resampling.LANCZOS),(box[0],box[1]))
   for j,x0 in enumerate([1.0,3.2]):
    art=Image.new('RGB',(300,325),(231,220,192) if j==0 else (43,62,61));draw_owner_print(art,p,j)
    paste_m(art,x0,1.35,1.2,1.3)
   plaque=Image.new('RGB',(600,90),tuple(p['accent']));dr=ImageDraw.Draw(plaque);dr.text((10,0),p['name'],font=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',38),fill=(236,227,205));dr.text((12,56),bid+' / INDEPENDENT SHOP',font=ImageFont.truetype('C:/Windows/Fonts/msjh.ttc',18),fill=(226,211,180));paste_m(plaque,1,2.88,3,.45)
  im.save(folder/f'{role}-basecolor.png')
  dx=(np.roll(relief,-1,1)-np.roll(relief,1,1))/(2*period[0]/n)
  dy=(np.roll(relief,-1,0)-np.roll(relief,1,0))/(2*height_m/n)
  normal=np.stack([-dx,dy,np.ones_like(dx)],2);normal/=np.linalg.norm(normal,axis=2)[:,:,None]
  Image.fromarray(((normal*.5+.5)*255).astype('uint8')).save(folder/f'{role}-normal.png')
  rough=np.clip((105 if role=='wet' else 170 if p['lower']=='wood' else 200)+noise*7,0,255).astype('uint8')
  Image.fromarray(rough).save(folder/f'{role}-roughness.png')
 from street_city_grok_brand_materials import bake
 bake(bid,folder,p)
 (folder/'profile.json').write_text(json.dumps(p,ensure_ascii=False,indent=2),'utf8')
 (folder/'generation.json').write_text(json.dumps(dict(generator_sha256=sha(__file__),profile_sha256=profile_hash(bid))),'utf8')
 return folder

def apply(scene,bid,building):
 import bpy
 p=profile(bid);folder=OUT/'textures'/bid;assert json.loads((folder/'profile.json').read_text('utf8'))==p
 generation=json.loads((folder/'generation.json').read_text('utf8'));assert generation==dict(generator_sha256=sha(__file__),profile_sha256=profile_hash(bid))
 placement=None
 if p.get('grok_branding'):
  from street_city_grok_brand_placement import select
  import subprocess,os,shutil
  placement=select(scene,building,p)
  placement_file=folder/'feature-placement.json';placement_file.write_text(json.dumps(placement,indent=2),'utf8')
  host=os.environ.get('STREET_CITY_ASSET_PYTHON') or shutil.which('python')
  assert host,'Host Python with Pillow required for material baking'
  subprocess.run([host,str(ROOT/'Tools/bake_street_city_grok_feature.py'),'--building',bid,'--output',str(folder),'--placement',str(placement_file)],check=True)
 mats={}
 for role in ['main','feature','quiet','facade','wet']:
  mat=bpy.data.materials.new(f'{bid} wall {role} {p["family"]}');mat.use_nodes=True;bs=mat.node_tree.nodes.get('Principled BSDF');nodes=mat.node_tree.nodes;links=mat.node_tree.links
  for filename,socket in [(role+'-basecolor.png','Base Color'),(role+'-normal.png','Normal'),(role+'-roughness.png','Roughness')]:
   tex=nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(folder/filename),check_existing=True);tex.image.pack();tex.extension='REPEAT'
   if p.get('grok_branding'):tex.image.reload();tex.image.pack()
   if socket!='Base Color':tex.image.colorspace_settings.name='Non-Color'
   if socket=='Normal':
    nm=nodes.new('ShaderNodeNormalMap');links.new(tex.outputs['Color'],nm.inputs['Color']);links.new(nm.outputs['Normal'],bs.inputs[socket])
   else:links.new(tex.outputs['Color'],bs.inputs[socket])
  mat['shop_wall_style']=True;mat['style_building']=bid;mat['wall_role']=role;mat['finish_role']='wet-wall' if role=='wet' else 'paint';mat['repeat_m']=p['feature_period_m'] if role=='feature' else p['period_m'];mats[role]=mat
  if p.get('grok_branding') and (folder/(role+'-metallic.png')).exists():
   tex=nodes.new('ShaderNodeTexImage');tex.image=bpy.data.images.load(str(folder/(role+'-metallic.png')),check_existing=True);tex.image.pack();tex.image.colorspace_settings.name='Non-Color';links.new(tex.outputs['Color'],bs.inputs['Metallic'])
 floors=building.get('small_shop_design',{}).get('floors',building.get('floors',[]));w,d=building['footprint_m'];rows=[]
 scene.frame_set(1);bpy.context.view_layer.update()
 for obj in list(scene.objects):
  if obj.type!='MESH' or obj.get('part_kind') not in ('wall','wall_corner'):continue
  floor=next(f for f in floors if f['floor']==obj.get('floor'));z=floor['z_m'];cells=floor.get('cells',[]);wet=[q['bounds_m'] for q in cells if q['kind'] in ('restroom','bathroom','shower')]
  if not cells:
   for core in floor.get('vertical_circulation',[]):
    if core['kind']=='restroom':
     x,y,_=core['position_m'];cw,cd,_=core['size_m'];wet.append([x-cw/2,y-cd/2,x+cw/2,y+cd/2])
  obj.data=obj.data.copy();indices={}
  for role,mat in mats.items():indices[role]=len(obj.data.materials);obj.data.materials.append(mat)
  uv=obj.data.uv_layers.active or obj.data.uv_layers.new(name='Metric wall UV');counter={}
  groups={}
  for poly in obj.data.polygons:
   normal=(obj.matrix_world.to_3x3().inverted().transposed()@poly.normal).normalized();axis=max(range(3),key=lambda k:abs(normal[k]));center=obj.matrix_world@poly.center;key=(axis,1 if normal[axis]>0 else -1,round(center[axis],3));groups.setdefault(key,[]).append(poly)
  centres={}
  for key,polys in groups.items():
   total=sum(poly.area for poly in polys);centres[key]=sum(((obj.matrix_world@poly.center)*poly.area for poly in polys),polys[0].center*0)/total
  for poly in obj.data.polygons:
   raw=obj.matrix_world@poly.center;nn=(obj.matrix_world.to_3x3().inverted().transposed()@poly.normal).normalized();axis=max(range(3),key=lambda k:abs(nn[k]));center=centres[(axis,1 if nn[axis]>0 else -1,round(raw[axis],3))];normal=(obj.matrix_world.to_3x3().inverted().transposed()@poly.normal).normalized()
   if abs(normal.z)>.5:continue
   exterior=(abs(center.x)>w/2-.23 and normal.x*center.x>0) or (abs(center.y)>d/2-.23 and normal.y*center.y>0)
   inwet=any(a-.21<=center.x<=c+.21 and b-.21<=center.y<=e+.21 for a,b,c,e in wet)
   role='facade' if exterior else 'wet' if inwet else 'feature' if obj.get('floor')==1 and normal.x>.5 and (not cells or any(q['kind']=='activity' and q['bounds_m'][0]-.21<=center.x<=q['bounds_m'][2]+.21 and q['bounds_m'][1]-.21<=center.y<=q['bounds_m'][3]+.21 for q in cells)) else 'quiet' if obj.get('floor')==3 else 'main'
   if placement:
    if obj.name==placement['object'] and poly.index in placement['polygons']:role='feature'
    elif role=='feature':role='main'
   poly.material_index=indices[role];counter[role]=counter.get(role,0)+1
   axes=[a for a in range(3) if a!=max(range(3),key=lambda a:abs(normal[a]))]
   for li in poly.loop_indices:
    point=obj.matrix_world@obj.data.vertices[obj.data.loops[li].vertex_index].co
    period=p['feature_period_m'] if role=='feature' else p['period_m'];offset=d/2 if role=='feature' else 0
    uv.data[li].uv=((point[axes[0]]+offset)/period[0],(point[axes[1]]-z)/period[1])
  obj['shop_wall_style']=bid;obj['metric_pbr_surface']=True;rows.append(dict(object=obj.name,floor=obj.get('floor'),faces=counter))
 result=dict(status='applied_requires_export_acceptance',profile=p,profile_sha256=profile_hash(bid),generator_sha256=sha(__file__),texture_directory=str(folder.resolve()),files={q.name:sha(q) for q in folder.glob('*.png')},objects=rows,geometry_changed=False)
 if placement:result['feature_placement']=placement
 return result

def verify_metadata(bid,report):
 r=report['wall_style'];assert r['profile_sha256']==profile_hash(bid) and r['generator_sha256']==sha(__file__)
 for name,digest in r['files'].items():assert sha(Path(r['texture_directory'])/name)==digest

def verify_export(scene,bid,report,check):
 """Inspect imported GLB assignments, channels and packed image pixels."""
 verify_metadata(bid,report);roles=set();floors=set()
 for obj in scene.objects:
  if obj.type!='MESH' or not obj.get('shop_wall_style'):continue
  check(obj.get('shop_wall_style')==bid,obj.name+' owner identity')
  used={poly.material_index for poly in obj.data.polygons}
  for i in used:
   mat=obj.data.materials[i]
   if not mat.get('shop_wall_style'):continue
   roles.add(mat['wall_role']);floors.add(obj.get('floor'));check(mat.get('style_building')==bid,mat.name+' owner material')
   bs=next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED')
   for socket in ['Base Color','Roughness','Normal']:check(bool(bs.inputs[socket].links),mat.name+' packed PBR '+socket)
 check({'main','feature','quiet','facade','wet'}<=roles,bid+' all five actual surface roles')
 check(floors=={1,2,3},bid+' styled walls on all three floors')
 check(report['wall_style']['geometry_changed'] is False,bid+' geometry preserved')
 if report['wall_style']['profile'].get('grok_branding'):
  from street_city_grok_brand_placement import verify
  verify(scene,bid,report,check)

if __name__=='__main__':
 from street_city_shop_street import inventory
 OUT.mkdir(parents=True,exist_ok=True)
 for b in inventory():generate(b['building']);print(b['building'],'wall textures',flush=True)
