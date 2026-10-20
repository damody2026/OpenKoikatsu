"""Recognisable plan symbols inside the same metre-sized furniture envelopes.

Symbols explain layout; they are not a generated Tripo mesh or collision shape.
"""
import html
from street_city_store_program import EQUIPMENT
def symbol(key,cx,cy,w,d,rotation=0,label=''):
 name=EQUIPMENT.get(key,(label or key,))[0];s='';stroke='#375b59'
 def rect(x,y,a,b,fill='#d8c6a6',r=0):return f'<rect x="{x}" y="{y}" width="{a}" height="{b}" rx="{r}" fill="{fill}"/>'
 def line(x,y,a,b):return f'<line x1="{x}" y1="{y}" x2="{a}" y2="{b}"/>'
 def ellipse(x,y,a,b,fill='#d8c6a6'):return f'<ellipse cx="{x}" cy="{y}" rx="{a}" ry="{b}" fill="{fill}"/>'
 def table():return rect(-.28,-.22,.56,.44,r=.04)+''.join(rect(x-.10,y-.10,.20,.20,'#f5eee0',.035) for x,y in ((-.36,0),(.36,0),(0,-.36),(0,.36)))
 def shelf():return ''.join(rect(-.46,y,.92,.045) for y in (-.46,-.15,.15,.415))
 text=name+label
 if key in ('dining_store','boardgame','craft','puzzle','lottery_desk','leather','fabric') or '桌椅組' in text:s=table()
 elif key=='garment' or '吊桿' in text:
  s=line(-.44,0,.44,0)+rect(-.48,-.16,.04,.32)+rect(.44,-.16,.04,.32)
  for x in (-.32,-.16,0,.16,.32):s+=f'<path d="M{x-.09},.24 L{x},-.06 L{x+.09},.24 Z" fill="#9cbaaf"/>'
 elif key in ('fruit_bin','flower_bucket','plant','bakery_case','cake_case','perfume'):
  s=rect(-.48,-.48,.96,.96,'#f5eee0',.04)
  for x in (-.3,0,.3):
   for y in (-.28,.08,.32):
    s+=ellipse(x,y,.09,.1,'#c18a65' if key=='fruit_bin' else '#97b291' if key in ('flower_bucket','plant') else '#d2ad74')
    if key=='perfume':s+=rect(x-.035,y-.13,.07,.035,'#496d66')
 elif key in ('washer_store','stack_dryer'):
  s=rect(-.48,-.48,.96,.96,'#d8e1df',.05)+ellipse(0,0,.30,.30,'#7eaaad')+ellipse(0,0,.21,.21,'#eff4ee')+rect(-.3,-.44,.6,.08,'#375b59')
 elif key=='bike_display' or '單車展示' in text:
  s=ellipse(0,-.32,.08,.15,'none')+ellipse(0,.32,.08,.15,'none')+f'<path d="M0,-.32 L-.2,0 L0,.12 L0,-.32 L.12,0 L0,.32 M-.2,0 L.12,0" fill="none"/>'+line(-.15,-.38,.15,-.38)
 elif key in ('salon_station','wash_chair','shoe_seat'):
  s=rect(-.24,-.1,.48,.42,'#a5b8ad',.08)+ellipse(0,.38,.24,.08)+ellipse(0,-.35,.36,.11,'#e6f2ee')
 elif key in ('audio','console_demo','listen'):
  s=rect(-.25,.18,.5,.27,'#c0cbbd',.07)+rect(-.42,-.42,.18,.28,'#52736e')+rect(.24,-.42,.18,.28,'#52736e')+rect(-.2,-.4,.4,.12,'#7eaaad')
 elif key in ('bed_display',) or '床' in text:s=rect(-.48,-.48,.96,.96,'#ece6d8',.07)+rect(-.42,-.43,.36,.2,'#fbfaf3',.04)+rect(.06,-.43,.36,.2,'#fbfaf3',.04)+rect(-.46,-.16,.92,.6,'#b4c7b9',.04)
 elif key in ('makeup','eyewear'):
  s=rect(-.46,-.46,.92,.28,'#d8c6a6',.035)+ellipse(0,.20,.22,.22,'#b5c9bd')+ellipse(0,-.4,.28,.055,'#cce3e1')
 elif key in ('jewelry','watch','camera_case','model','claw'):
  s=rect(-.48,-.48,.96,.96,'#cce3e1',.025)+rect(-.40,-.39,.8,.78,'#f5eee0')
  for x in (-.25,0,.25):s+=ellipse(x,0,.07,.17,'#c8ae7f')
 elif key in ('phone','computer_demo','appliance','repair_elec'):
  s=rect(-.47,-.46,.94,.92,'#d8c6a6',.03)
  for x in (-.28,0,.28):s+=rect(x-.07,-.12,.14,.24,'#456d70',.02)
 elif key in ('book','book_table','record_bin','pet_rack','drug_shelf','homeware','stationery','art','shoe','bag','gift') or any(x in text for x in ('書架','貨架','展示架','衣櫃')):
  s=shelf()
  for x in (-.35,-.18,0,.18,.35):s+=rect(x-.04,-.40,.08,.23,'#9ab5a6')+rect(x-.06,.20,.12,.19,'#c8ae7f')
 elif key=='lamp':s=ellipse(-.23,0,.17,.23,'#f1ddb5')+ellipse(.23,0,.17,.23,'#a6c6b8')
 elif '馬桶' in text:s=rect(-.35,-.45,.7,.28,'#f3f1e8',.03)+ellipse(0,.10,.31,.35,'#f3f1e8')+ellipse(0,.09,.20,.24,'#d0e2df')
 elif '沙發' in text:s=rect(-.46,-.4,.92,.8,'#aab9a4',.09)+rect(-.38,-.3,.76,.16,'#c2cdb9',.04)
 else:
  s=rect(-.46,-.46,.92,.92,'#d8c6a6',.025)+line(-.42,-.08,.42,-.08)
  if any(x in text for x in ('洗','水槽','清洗','小廚房')):s+=ellipse(-.15,.13,.18,.21,'#aac6c9')
 return f'<g transform="translate({cx},{cy}) rotate({rotation}) scale({w},{d})" stroke="{stroke}" stroke-width=".012"><title>{html.escape(name)}；外框僅作尺寸；圖示非成品模型</title>{s}</g>'
