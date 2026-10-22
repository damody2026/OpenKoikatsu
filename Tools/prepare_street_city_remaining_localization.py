"""Explicit translations for all newly adopted indoor identities; no untranslated fallback."""
import json,re,sys
from pathlib import Path
from street_city_remaining_contract import context,IDS,OUT
from street_city_localization import OUT as LANG,simplified,render,path_for,sha
EN={
 '住家':'Apartment','企業辦公室A':'Company Office A','企業辦公室B':'Company Office B','員工茶水休憩室':'Staff Pantry and Lounge','會議洽談室':'Meeting Room',
 '健康教育室':'Health Education','健身房':'Fitness Centre','全日餐廳':'All-day Dining','公共音樂室':'Music Room','公車候車室':'Bus Waiting Lounge','共享辦公室':'Coworking Space','商務會議室':'Business Meeting Room','城市歷史展廳':'City History Gallery','城市閱讀室':'City Reading Room','大堂咖啡廳':'Lobby Cafe','天空套房':'Sky Suite','婚宴廳':'Wedding Ballroom','家庭套房':'Family Suite','家庭活動室':'Family Activity Room','家庭醫學診間':'Family Medicine','小劇場':'Black Box Theatre','就業服務室':'Employment Services','市民服務室':'Citizen Services','庭園套房':'Garden Suite','志工辦公室':'Volunteer Office','挑高接待大廳':'Grand Reception Lobby','接待與訪客服務':'Reception and Visitor Services','接駁車候車室':'Shuttle Waiting Lounge','旅客休憩室':'Passenger Lounge','旅遊資訊室':'Tourist Information','景觀溫泉池':'Scenic Hot Spring Pool','桑拿休憩室':'Sauna Lounge','水族館紀念品店':'Aquarium Gift Shop','水母展廳':'Jellyfish Gallery','法律諮詢室':'Legal Advice','泳池會館':'Pool Club','海洋咖啡廳':'Ocean Cafe','海洋大水槽展廳':'Ocean Tank Gallery','海洋生態教室':'Marine Ecology Room','海洋研究展示室':'Marine Research Gallery','牙科診間':'Dental Clinic','物理治療室':'Physiotherapy','皇家套房':'Royal Suite','社區午睡室':'Community Rest Room','社區活動室':'Community Activity Room','社區藥局':'Community Pharmacy','社區診所':'Community Clinic','禮賓服務室':'Concierge','私人宴會廳':'Private Banquet Hall','私人雙人湯屋':'Private Hot Spring Bath','站前售票廳':'Ticket Hall','站務服務室':'Station Office','精油護理室':'Aromatherapy Room','總統套房':'Presidential Suite','美容護理室':'Beauty Treatment Room','美術展廳':'Art Gallery','自行車租借室':'Bicycle Rental','茶水休憩室':'Pantry and Lounge','藝術套房':'Art Suite','蜜月套房':'Honeymoon Suite','行政景觀客房':'Executive View Room','行李與客務中心':'Luggage and Guest Services','警務服務站':'Police Service Point','豪華景觀客房':'Deluxe View Room','車站咖啡廳':'Station Cafe','轉乘資訊室':'Transfer Information','郵局':'Post Office','銀行服務室':'Banking Services','鐵道候車室':'Rail Waiting Lounge','長者交誼室':'Senior Lounge','陶藝工坊':'Ceramics Workshop','電影放映室':'Screening Room','靜音午睡沙龍':'Quiet Rest Lounge','音樂套房':'Music Suite'
}

def english(value):
 floor=re.match(r'^(\d+)樓',value);prefix=''
 if floor:prefix='Level '+floor[1]+' · ';value=value[floor.end():]
 number=re.search(r'\d+$',value);suffix=''
 if number:suffix=' '+number[0];value=value[:number.start()]
 assert value in EN,('Missing explicit translation',value)
 return prefix+EN[value]+suffix

def main():
 path=LANG/'catalogue.json';catalogue=json.loads(path.read_text('utf8'));rows=[]
 for bid in IDS:
  for rid,r in context(bid)['rooms'].items():
   key='room.'+rid;text=r['display_name'];catalogue['entries'][key]=dict(id=key,building=bid,kind='room',size=[2048,512],translations={'zh-Hant':text,'zh-Hans':simplified(text),'en':english(text)})
   rows.append(key)
 path.write_text(json.dumps(catalogue,ensure_ascii=False,indent=2),'utf8')
 for key in rows:
  for locale in ['zh-Hant','zh-Hans','en']:render(key,locale,path_for(key,locale),background=(35,43,48),foreground=(239,223,181))
 (OUT/'localization-sources.json').write_text(json.dumps(dict(room_identities=len(rows),locales=['zh-Hant','zh-Hans','en'],catalogue_sha256=sha(path),entries=rows),indent=2),'utf8')
 print('232 indoor identities translated and rendered',flush=True)

if __name__=='__main__':main()
