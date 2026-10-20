"""Persist per-store evidence before design; cache public case photos for review."""
import argparse,concurrent.futures,hashlib,io,json,re,urllib.request,urllib.parse
from pathlib import Path
from html.parser import HTMLParser
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'Docs/StreetSocialCity/production';CACHE=ROOT/'output/street-social-city-production/store-research';CACHE.mkdir(exist_ok=True)
# These sources were searched/opened by the primary agent. Published facts and
# inferred requirements are deliberately different fields.
CASES={
 'S01':('https://2booksdesign.com.tw/works/2020/black-house','Black House 咖啡工作交流，40坪','開放咖啡座席、閱讀、吧台備餐；上層用途另排'),
 'S02':('https://knrndesign.co/project/rairai-tea','來日方茶，10坪','點單與取餐分流；封口、製冰、洗滌與煮茶工作線'),
 'S03':('https://www.weiwustudio.com/新北flourishbakery','Flourish Bakery，36m²，一樓展售、二樓製作','甜點冷藏、麵包展示、結帳；製作區另層'),
 'S04':('https://zhuyueinteriordesign.com.tw/restaurant-design/','設計者餐館案例及前後場配置說明','座席、點餐、出餐、熱廚、回盤洗滌、冷藏乾貨分開'),
 'S05':('https://www.b-studio.com.tw/en/portfolio_page/fruit-hunter/','獵果舖原設計者案例；英譯面積單位待核對，不作本案測量','入口傾斜木展示台、玻璃吧台、工作設備架、儲藏與少量座席；果台秤重結帳及包裝分區'),
 'S06':('https://www.7-11.com.tw/Company/esg/news_detail.aspx?id=607','統佳66坪；一樓超商、二樓健身，室內26席','冷藏牆、低島架、收銀熱食、微波、自助服務、收貨庫房'),
 'S07':('https://2booksdesign.com.tw/works/2020/black-house','咖啡與讀書共享空間案例；非純書局施工圖','周邊書牆、低展台、收銀、收書整理及少量閱讀座'),
 'S08':('https://muxin.tw/en/caldo-pets/','台北卡朵毛孩生活，開放層架及中島；未公開尺度','飼料低位、玩具用品層架、收銀及補貨；顧客與寵物通路留空'),
 'S09':('https://hoho-interior.com/portfolio-item/花波苑/','花波苑40.5坪；花藝桌檯、後方吧台、展示層架','水槽、修花工作檯、花桶、保鮮與包裝；陳列形式依花材設計'),
 'S10':('https://hibikidesign.com.tw/muura-chifeng-store/','Muura赤峰43坪；入口櫃台、周邊及中央陳列、後方3間試衣間','金屬吊桿與可調衣架、低台、鏡面、私密試衣與等候；不滿屋封閉房間'),
 'S11':('https://www.threerow.tw/haoai/','台南好愛髮廊15坪','剪髮鏡台、洗髮躺椅、接待等候、調色洗滌及毛巾庫；濕作業隔開'),
 'S12':('https://manlun.com.tw/portfolio-item/音樂教室-現代美式風/','苗栗竹南樂器行25坪；樂器展示、洽談、樂譜櫃','樂器實際支架、唱片翻閱箱、試聽與隔音試奏；不把樂器做成實心長方體'),
 'S13':('https://uggboardgame.com/','台中烏嘎嘎桌遊營運與場地介紹；未公開公尺平面','桌遊桌椅、開放教學、遊戲牆、飲水收銀；遊戲店與大型娛樂場不同'),
 'S14':('https://tw.canon/zh_TW/consumer/web/ImageSquare','Canon官方概念店；體驗商品、諮詢、代收維修','低玻璃展櫃、試機檯、沖印交件、修護收件、後場工作與耗材'),
 'S15':('https://www.giant-bicycles.com/tw/news/article/30594','2026信義旗艦店；展示、DCF量測、客製與騎行體驗','成車展示、配件牆、維修架與工具檯、量測；推車路徑避開座位'),
 'S16':('https://www.nhi.gov.tw/ch/dl-12856-0a701a6904e34f41977e8c2585c85c76-1.pdf','官方藥局功能平面要求；不能用作藥妝門市照片','日用藥妝開放陳列、收銀諮詢、補貨；如有調劑則另設受控空間'),
 'S17':('https://www.jetswash.com.tw/advantage.html','設備商例：20坪4洗、10烘（5組雙層）、兌幣機','洗烘設備沿牆、排水瓦斯排風維修帶、摺衣、付款與等候'),
 'S18':('https://www.kagaya.com.tw/en/rooms/','北投加賀屋官方和式房型；並非本案整棟民宿尺寸','一樓開放接待交誼與備餐，樓上多客房、私人衛浴、布草及清掃支援'),
 'S19':('https://www.gvrb.com.tw/en/service/露天風呂/','麗禧大眾湯含冷熱池、蒸氣、三溫暖、淋浴及休息','入口接待到更衣淋浴再入濕區；分性別公共湯、私人湯、機房、乾休息分隔'),
 'S20':('https://mingchenblog8.pixnet.net/blog/posts/16108641047','名辰台南彩券行店面裝潢案例；未公開公尺平面','銷售櫃台、選號刮票、螢幕公告、保管與排隊；非大型賭場'),
}
class Images(HTMLParser):
 def __init__(self):super().__init__();self.urls=[]
 def handle_starttag(self,tag,attrs):
  a=dict(attrs)
  if tag=='meta' and a.get('property')=='og:image':self.urls.insert(0,a.get('content',''))
  if tag=='img':
   for key in ('data-src','src'):
    if a.get(key):self.urls.append(a[key])
def fetch(key,case):
 url,fact,needs=case;folder=CACHE/key;folder.mkdir(exist_ok=True)
 previous=folder/'evidence.json'
 if previous.exists():
  cached=json.loads(previous.read_text('utf-8'))
  if cached['url']==url and cached.get('http_status') in ('downloaded','read_via_web'):return cached
 result=dict(family=key,url=url,published_fact=fact,inferred_design_requirements=needs,photos=[],retrieved_date='2026-10-12',photo_visual_review='pending',dimensions_from_source=False)
 try:
  req=urllib.request.Request(urllib.parse.quote(url,safe=':/?=&%'),headers={'User-Agent':'Mozilla/5.0'})
  with urllib.request.urlopen(req,timeout=25) as r:data=r.read(8*1024*1024);ctype=r.headers.get('Content-Type','')
  file=folder/('source.pdf' if 'pdf' in ctype else 'source.html');file.write_bytes(data);result.update(http_status='downloaded',source_file=file.relative_to(ROOT).as_posix(),source_sha256=hashlib.sha256(data).hexdigest())
  if 'pdf' not in ctype:
   parser=Images();parser.feed(data.decode('utf-8','replace'));urls=list(dict.fromkeys(urllib.parse.urljoin(url,u) for u in parser.urls if not re.search(r'logo|icon|line[-_]|qr[-_]|spinner|pixel|\.svg|data:',u,re.I)))
   for target in urls[:18]:
    try:
     with urllib.request.urlopen(urllib.request.Request(target,headers={'User-Agent':'Mozilla/5.0','Referer':url}),timeout=12) as r:img=r.read(32*1024*1024+1)
     if len(img)>32*1024*1024:continue
     if len(img)<18000 or not(img.startswith(b'\xff\xd8')or img.startswith(b'\x89PNG')or img.startswith(b'RIFF')):continue
     from PIL import Image
     with Image.open(io.BytesIO(img)) as probe:probe.verify()
     suffix='.jpg' if img.startswith(b'\xff\xd8') else '.png' if img.startswith(b'\x89PNG') else '.webp';image=folder/(f'photo-{len(result["photos"])+1:02d}'+suffix);image.write_bytes(img)
     result['photos'].append(dict(url=target,file=image.relative_to(ROOT).as_posix(),sha256=hashlib.sha256(img).hexdigest(),visually_reviewed=False))
     if len(result['photos'])>=3:break
    except Exception:continue
 except Exception as e:result.update(http_status='not_downloaded',retrieval_error=type(e).__name__+': '+str(e)[:220])
 (folder/'evidence.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),'utf-8');print(key+': '+result['http_status']+f', {len(result["photos"])} photos',flush=True);return result
if __name__=='__main__':
 from street_city_store_program import SPECIAL_SOURCES,category,reference
 extra={f'REF-{k}':v for k,v in SPECIAL_SOURCES.items()}
 with concurrent.futures.ThreadPoolExecutor(max_workers=5) as pool:
  results=list(pool.map(lambda kv:fetch(*kv),list(CASES.items())+list(extra.items())))
 cases=results[:len(CASES)]
 plan=json.loads((OUT/'effective-city-plan.json').read_text('utf-8'));refs=json.loads((OUT.parent/'building-references.json').read_text('utf-8'));by={q['building']:q for q in refs['buildings']}
 from street_city_site import EXTRA_SHOPS
 families={f'S{i:02d}':f'S{i:02d}' for i in range(1,21)};families.update({f'S{i:02d}':base for i,(_,base) in enumerate(EXTRA_SHOPS,21)})
 stores=[dict(building=b['id'],name=b['name'],family=families[b['id']],reference=next(q for q in cases if q['family']==families[b['id']]),current_footprint_m=b['footprint_m'],programme_status='dimensioned_three_shop_profile' if b['id'] in ('S01','S02','S03') else 'pending_store_specific_redesign',source_measurement_limitations='來源用途與照片供參考，本案仍需獨立尺寸核對；不得宣稱來源照片提供未標示的公尺。') for b in plan['buildings'] if re.fullmatch('S[0-9]{2}',b['id'])]
 malls=[dict(id=r['id'],name=r['name'],floor=r['floor'],type=r['type'],category=category('M1',r['id'],r['name']),reference=reference('M1',r['id'],r['name']),status='store_specific_programme_implemented_visual_review_pending') for r in plan['rooms'] if r['building']=='M1']
 for s in stores:s['functional_reference']=reference(s['building'],s['building']+'-0101',s['name'])
 report=dict(schema=2,scope='48 street shops including inn and hot-spring house; 80 mall stores separately tracked',store_count=len(stores),mall_store_count=len(malls),cases=results,stores=stores,mall_stores=malls,all_stores_finished=False)
 (OUT/'store-research-register.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),'utf-8')
