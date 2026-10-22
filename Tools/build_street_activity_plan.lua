package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local root='C:/portable/OpenKoikatsu'
local base=root..'/output/street-social-city-production'
local out=base..'/activity-scenes-v1'
local function read(p)local f=assert(io.open(p,'rb'));local v=json.decode(f:read('*a'));f:close();return v end
local function write(p,v)local f=assert(io.open(p,'wb'));f:write(json.encode(v));f:close()end
local function keys(t)local k={};for id in pairs(t)do k[#k+1]=id end;table.sort(k);return k end
local probe=read(out..'/probe.json');local terrain=read(out..'/terrain-samples.json')
local reused=read(base..'/resort-native-v1/imports.json').assets
local plan={schema=1,scope='Physical scene models only; gameplay and demo unchanged',scenes={},instances={},signs={},views={},corridors={},lights={},models=read(out..'/models.json').models}
local function add(scene,asset,pos,yaw,scale,extra)
 local q={id=scene..'-'..(#plan.instances+1),scene=scene,asset=asset,position_m=pos,yaw_deg=yaw or 0,scale=scale or {1,1,1}}
 for k,v in pairs(extra or {})do q[k]=v end
 plan.instances[#plan.instances+1]=q;return q
end
local function on_surface(scene,asset,support,offset,scale)
 local bounds=assert(reused[support.asset],'Need a verified support mesh').bounds_cm
 local top=support.position_m[3]+bounds.max[3]/100*support.scale[3]
 local a=math.rad(support.yaw_deg);local x=offset[1]*math.cos(a)-offset[2]*math.sin(a);local y=offset[1]*math.sin(a)+offset[2]*math.cos(a)
 return add(scene,asset,{support.position_m[1]+x,support.position_m[2]+y,top+.005},support.yaw_deg,scale,{supported_by=support.id,support_plane_z_m=top})
end
local function sign(scene,id,text,en,p,width,height,yaw)
 plan.signs[id]={id=id,translations={['zh-Hant']=text,['en']=en},size={1024,256}}
 add(scene,'sign',p,yaw,{width or 2,1,height or .5},{sign=id,collision='NoCollision'})
end
local function scene(id,name,anchor,notes)
 plan.scenes[#plan.scenes+1]={id=id,name=name,anchor_m=anchor,notes=notes}
end
local function view(id,eye,target)plan.views[#plan.views+1]={name=id,eye=eye,target=target}end
local function clear(scene,id,a,b,width)plan.corridors[#plan.corridors+1]={scene=scene,id=id,a=a,b=b,width_m=width or 2}end
local function light(scene,p,lumens,radius,color)local q={scene=scene,position_m=p,lumens=lumens or 1800,radius_m=radius or 7,color=color or {1,.72,.42}};plan.lights[#plan.lights+1]=q;return q end
-- Sources are immutable and their remesh receipts are verified again by native import.
for _,id in ipairs({'bear','koi'})do plan.models[#plan.models+1]={id=id,provider='Tripo Model',source=out..'/components/'..id..'/remesh/remeshed-candidate.glb',report=out..'/components/'..id..'/remesh/remesh-report.json',job=id=='bear' and '1b2d400d-7850-4015-b6d8-6544e2bea201' or '9bdffd4f-bb5b-40d5-8f0b-84c3cc0ccea1'}end
for _,id in ipairs({'books','art-kit','stationery','laptop','sneaker','tea','cake','ramen','sushi','toy-car'})do
 local folder=base..'/mall-v1/merchandise-v1/components/'..id..'/remesh'
 plan.models[#plan.models+1]={id=id,provider='reuse certified Tripo component',source=folder..'/remeshed-candidate.glb',report=folder..'/remesh-report.json'}
end
local sid='ACTSPACE-01';local mz=12.06+29.4
scene(sid,'8F 大熊合照點',{599,474,mz},'M1-0801 開放商店前緣；熊高2.8m，避開中庭與手扶梯。')
add(sid,'bear',{599,474,mz});add(sid,'R048',{596.5,475.5,mz});add(sid,'R048',{601.5,475.5,mz})
sign(sid,'bear','和大熊合照','Meet the Giant Bear',{599,475.6,mz+2.95},3.2,.4)
light(sid,{599,471.5,mz+3.2},3500,8,{1,.94,.85});add(sid,'platform',{599,471.5,mz+3.3},0,{.6,.4,.5},{emissive_panel=true,collision='NoCollision'});add(sid,'platform',{599,471.5,mz+3.36},0,{.025,.025,4.5},{collision='NoCollision'})
clear(sid,'photo-standing',{597.8,470.5,mz},{600.2,470.5,mz},2);view('01-bear',{599,467.5,mz+1.65},{599,474,mz+1.5})
sid='ACTSPACE-02';local z=20.12
scene(sid,'噴水池與寫生角',{572.5,800,z},'重整既有 PK-0102 平台；不把花壇當成噴水池。')
add(sid,'platform',{572.5,800,z-.12},0,{24,18,1},{replacement='World_PK_0102'})
add(sid,'fountain',{572.5,800,z})
for _,x in ipairs({-10,10})do for _,y in ipairs({-7,7})do add(sid,'R009',{572.5+x,800+y,z},y<0 and 0 or 180)end end
add(sid,'easel',{562.5,801.5,z},-90);add(sid,'R009',{561.3,801.5,z},-90);local sketch_table=add(sid,'R003',{561.5,803,z});on_surface(sid,'art-kit',sketch_table,{-.15,0});on_surface(sid,'sketchbook',sketch_table,{.15,0},{.6,.6,.6})
sign(sid,'fountain','噴泉花園 · 寫生角','Fountain Garden · Sketch Corner',{563,795,z+1.2},2.4,.4)
view('02-fountain',{572.5,793,z+1.65},{572.5,800,z+1.1});view('02-sketch',{560.8,797.5,z+1.65},{567,801,z+1})
-- Fountain perimeter doubles as a short, continuous 52m park jogging circuit.
local loop={{564.5,795,z},{580.5,795,z},{580.5,805,z},{564.5,805,z},{564.5,795,z}}
for i=1,4 do
 clear('ACTSPACE-07','jog-loop-'..i,loop[i],loop[i+1],2)
 local a,b=loop[i],loop[i+1];local dx,dy=b[1]-a[1],b[2]-a[2]
 add('ACTSPACE-07','platform',{(a[1]+b[1])/2,(a[2]+b[2])/2,z+.004},math.deg(math.atan(dy,dx)),{math.sqrt(dx*dx+dy*dy),.08,.015},{paint=true,collision='NoCollision'})
end
sid='ACTSPACE-03';z=12.0957843
scene(sid,'夜市八攤營業配置',{620,355,z},'八攤既有屋架與櫃台保留；商品疊在0.9m檯面，中央6m通路。')
local stalls={{-8,-5,'糰子與糖果','Dango & Sweets','dango'},{-4,-5,'拉麵小食','Ramen','ramen'},{4,-5,'手作冷飲','Fresh Tea','tea'},{8,-5,'甜點屋','Desserts','cake'},{-8,5,'壽司小食','Sushi','sushi'},{-4,5,'投環遊戲','Ring Toss','R064'},{4,5,'玩具小鋪','Toy Stall','toy-car'},{8,5,'藝文小物','Art & Gifts','art-kit'}}
for i,s in ipairs(stalls)do
 local x,y=620+s[1],355+s[2];local yaw=s[2]<0 and 180 or 0
 sign(sid,'stall-'..i,s[3],s[4],{x,y+(s[2]<0 and .55 or -.55),z+2.1},2.3,.38,yaw)
 for k=-1,1 do local aid=s[5];local pp={x+k*.65,y,z+.9};local sc={1,1,1};if aid=='R064'then pp={x,y+.8,z};sc={.7,.7,.7};if k~=0 then aid='candy';pp={x+k*.65,y,z+.9};sc={1,1,1}end end;add(sid,aid,pp,yaw,sc)end
 if i<6 then add(sid,'calculator',{x+.95,y,z+.9},yaw)end
 light(sid,{x,y,z+2.8})
end
add(sid,'R086',{630,359,z});add(sid,'R022',{620,347,z});add(sid,'R022',{620,363,z});add(sid,'staff-locker',{608.2,355,z},90)
add(sid,'flyer-stand',{609.5,351.5,z},90);sign(sid,'nightmarket','大學城夜市','University Night Market',{610,355,z+2.25},2.4,.5,90)
clear(sid,'market-main-aisle',{609,355,z},{631,355,z},6);view('03-nightmarket',{620,344,z+1.65},{620,356,z+1.4})
sid='ACTSPACE-04';z=12.12
scene(sid,'海邊日落觀景點',{620,143,z},'依 height_map 與 water_map 選定南方真正海岸；位於12m岸台、面向約100m北座標以下的海灣，不沿用偏內陸的舊區域標籤。')
add(sid,'platform',{620,143,z-.24},0,{10,6,2});add(sid,'binoculars',{620,141,z},180)
for _,x in ipairs({616.25,618.75,621.25,623.75})do add(sid,'R051',{x,140.1,z})end
for _,x in ipairs({615.1,624.9})do for _,y in ipairs({141.5,144})do add(sid,'R051',{x,y,z},90)end end
add(sid,'R009',{617.2,143,z},180);add(sid,'R009',{622.8,143,z},180)
sign(sid,'sunset','海灣日落展望台','Sunset Bay Lookout',{616,145,z+1.1},2.2,.45,90)
clear(sid,'lookout-approach',{620,147,z},{620,143,z},2)
view('04-sunset',{620,145.5,z+1.65},{620,95,1.2});plan.views[#plan.views].lighting='sunset'
sid='ACTSPACE-05';z=terrain[2].max+.12
scene(sid,'水岸煙火觀賞會場',{620,184,z},'既有舞台南側增設觀賞帶；面向南方海灣。只完成會場，不宣稱已完成煙火特效。')
add(sid,'platform',{620,184,z-.12},0,{14,10,1});add(sid,'festival-gate',{620,188,z})
sign(sid,'fireworks','水岸煙火觀賞區','Riverside Fireworks Viewing',{620,188.05,z+2.62},4.8,.45)
for _,x in ipairs({-4,-2,2,4})do for _,y in ipairs({-2,1})do add(sid,'R004',{620+x,184+y,z},180)end end
for _,x in ipairs({-6,6})do add(sid,'R056',{620+x,184,z});light(sid,{620+x,184,z+2.8})end
clear(sid,'festival-central-exit',{620,180,z},{620,191,z},2);view('05-fireworks',{615,188,z+2.8},{620,182,z+.5})
sid='ACTSPACE-06';z=19.7295113
scene(sid,'湖畔觀魚餵食區',{757.5,852.5,z},'既有湖畔平台增加可見池體、淡水錦鯉、飼料機與觀看空間；非釣魚區。')
add(sid,'fish-pond',{757.5,851,z})
for i=1,9 do add(sid,'koi',{755+(i%4)*1.4,850+math.floor((i-1)/4)*.8,z+.15},(i*73)%360,{.85+(i%3)*.08,.85+(i%3)*.08,.85+(i%3)*.08},{collision='NoCollision'})end
add(sid,'feed-dispenser',{763,853.8,z});add(sid,'R051',{757.5,853.7,z},0,{2,1,1});sign(sid,'fish','湖畔觀魚 · 請使用專用飼料','Koi Pond · Use Fish Feed Only',{763,853.8,z+1.6},2,.4)
clear(sid,'fish-standing',{754,855,z},{761,855,z},2);view('06-fish',{757.5,855,z+1.65},{757.5,851,z+.2})
sid='ACTSPACE-07';z=20.12
scene(sid,'公園晨練及慢跑環線',{600,700,z},'晨練使用 PK-0104；52m連續環線在噴泉花園周邊，寬2m。')
add(sid,'platform',{600,697,z+.005},0,{5,4,.04});add(sid,'R068',{606,699,z});add(sid,'R029',{593,699,z});add(sid,'R004',{593,701,z});add(sid,'sneaker',{593,701,z+.45})
sign(sid,'exercise','晨練區 · 慢跑環線 52m','Morning Exercise · 52m Jogging Loop',{594,695,z+1.2},2.8,.5)
sign(sid,'jogging','慢跑環線 52m →','Jogging Loop 52m →',{562.5,796,z+1.2},2.2,.4)
clear(sid,'exercise-entry',{600,692,z},{600,697,z},2);view('07-exercise',{600,690,z+1.65},{602,699,z+.7})
sid='ACTSPACE-08';scene(sid,'行政及系辦服務窗口',{700,670,20},'在既有辦公室內設置櫃台、文件、電腦與候位；不占中央2m走道。')
local adm={['AD-0101']='綜合服務',['AD-0102']='註冊與學分抵免',['AD-0103']='選課服務',['AD-0105']='出納繳費',['AD-0201']='學生事務',['AD-0204']='助學申請'}
local english={['AD-0101']='General Services',['AD-0102']='Registration & Credits',['AD-0103']='Course Services',['AD-0105']='Cashier',['AD-0201']='Student Affairs',['AD-0204']='Student Aid'}
local office_en={['E1-0106']='Mechanical Engineering Office',['E1-0206']='Materials Engineering Office',['E1-0306']='Civil Engineering Office',['E1-0406']='Chemical Engineering Office',['EE-0106']='Electronic Engineering Office',['EE-0206']='Electrical Engineering Office',['MA-0106']='Industrial Management Office',['MA-0206']='Business Administration Office',['MA-0306']='Information Management Office',['T4-0106']='Humanities & Social Sciences Office',['T4-0206']='Design Studio Office',['T4-0306']='Applied Foreign Languages Office',['T4-0406']='Digital Learning & Teacher Education',['T4-0506']='Computer Science Office',['RB-0106']='Architecture & Design Exhibition Office',['RB-0206']='Architecture Office',['RB-0306']='Prototyping Office',['RB-0406']='Design Department Office',['RB-0506']='Computer Center',['SC-0201']='Student Affairs Services',['IB-0106']='Language Center',['IB-0206']='International Affairs Office',['IB-0306']='Research & Development Office',['IB-0406']='Teaching Development Center',['IB-0506']='International Conference Office'}
for _,bid in ipairs({'AD','E1','EE','MA'})do
 local c=probe.campus[bid];local origin=c.building.world_position_m
 for _,rid in ipairs(keys(c.rooms))do local r=c.rooms[rid];if adm[rid] or r.display_name:find('系辦公室',1,true)then
  local rx,ry,rz=r.position_m[1],r.position_m[2],r.position_m[3];local x,y,z=origin[1]+rx+3.3,origin[2]+ry-2.8,origin[3]+rz
  if rid=='AD-0101'then x=origin[1]+rx-2.2;y=origin[2]+ry-2.375 else add(sid,'service-counter',{x,y,z})end
  local desktop,paper=1.05,.8
  if rid=='AD-0101'then for _,e in ipairs(r.dimensioned_programme.equipment_layout)do if e.equipment=='checkout'then desktop=e.size_m[3];paper=desktop end end end
  add(sid,'laptop',{x-.45,y+.15,z+desktop+.005},0,nil,{support_plane_z_m=z+desktop});add(sid,'sketchbook',{x+.7,y-.1,z+paper+.005},0,{.7,.7,.3},{support_plane_z_m=z+paper})
  if rid=='AD-0101'then add(sid,'flyer-stand',{x+.85,y+.22,z+desktop+.005},0,{.35,.35,.35},{support_plane_z_m=z+desktop})else add(sid,'flyer-stand',{x+1.9,y+.2,z},0,{.7,.7,.7})end
  sign(sid,rid,adm[rid] or r.display_name,assert(english[rid] or office_en[rid]),{x,y+.4,z+1.75},2.3,.35)
  local fill=light(sid,{x,y-.3,z+2.9},2500,5,{1,.96,.9});fill.cast_shadows=false
  clear(sid,rid..'-approach',{origin[1]+rx,origin[2]+ry-4,z},{origin[1]+rx,origin[2]+ry,z},2)
  if rid=='AD-0102'then view('08-admin',{x,y-1.75,z+1.65},{x,y,z+1})end
 end end
end
sid='ACTSPACE-09';scene(sid,'書局繪本與畫具區',{289.2,475.5,12},'S07開放營業區南側兩邊增設低展示；中央入口與主走道保持2m。')
for i,x in ipairs({284.2,293.2})do
 local y=i==1 and 474.1 or 472.8
 add(sid,'picture-rack',{x,y,12},0);for shelf=0,2 do for k=-1,1 do if shelf~=2 or k~=0 then add(sid,'books',{x+k*.45,y-.03+shelf*.06,12+.175+shelf*.3},0,{.8,.35,.75})end end end
end
add(sid,'art-display',{293.5,475.8,12},-90)
for _,z in ipairs({12.185,12.695,13.205})do add(sid,'art-kit',{293.5,475.8,z});add(sid,'sketchbook',{293.5,476.25,z},0,{.7,.7,.7})end
sign(sid,'picturebooks','繪本 · 故事與圖鑑','Picture Books · Stories & Nature',{284.2,474.1,13.3},1.6,.35)
sign(sid,'arts','畫具 · 素描與寫生','Art Supplies · Sketching',{293.5,475.8,13.28},1.6,.35,-90)
sign(sid,'lamb-book','小羊飄飄去旅行','Little Lamb Travels',{284.2,474.16,12.775},.34,.4)
sign(sid,'science-book','煙火與星空','Fireworks & Stars',{293.2,472.86,12.775},.34,.4)
plan.signs['lamb-book'].size={512,640};plan.signs['lamb-book'].cover='lamb';plan.signs['science-book'].size={512,640};plan.signs['science-book'].cover='fireworks'
add(sid,'R009',{284.2,475.6,12},90,{.8,.8,.8});local reading_table=add(sid,'R003',{284.2,476.7,12},0,{.8,.8,.8});on_surface(sid,'books',reading_table,{0,0});add(sid,'stationery',{293.5,475.35,12.695})
clear(sid,'bookstore-centre',{286.6,471.5,12},{286.6,477.5,12},2);view('09-bookstore',{286.6,473.15,13.65},{284.2,474.1,12.85});view('09-arts',{290.4,473,13.65},{293.4,475.8,13.1})
for _,p in ipairs({{285.7,474,14.9},{292.3,475,14.9}})do local fill=light(sid,p,3000,6,{1,.96,.9});fill.cast_shadows=false end
sid='ACTSPACE-10';z=terrain[1].max+.12
scene(sid,'表演練習空地',{1020,750,z},'觀看、施演與遠方效果落點分區；本次僅靜態場景與預留錨點，不含VFX。')
add(sid,'platform',{1020,750,z-.48},0,{20,16,4})
for _,x in ipairs({1012,1028})do for _,y in ipairs({744,756})do add(sid,'practice-marker',{x,y,z})end end
add(sid,'R004',{1013,746,z},90);add(sid,'R003',{1013,747.3,z});add(sid,'R029',{1012,747.8,z})
sign(sid,'practice','表演練習區 · 保持觀看距離','Performance Practice · Keep Clear',{1012,753,z+1.2},2.6,.45,90)
clear(sid,'practice-approach',{1010,750,z},{1016,750,z},2);view('10-practice',{1012,750,z+1.65},{1024,750,z+.5})
plan.effect_anchors={fireworks={620,100,90},magic_performer={1020,750,z},magic_effect={1080,750,z+.1},magic_viewer={1015,750,z}}
-- Staff / rain / errands props, rendered and installed as real visible objects.
add('ACTSPACE-03','candy',{612,350,13});add('ACTSPACE-09','calculator',{293.5,475.8,13.24});add('ACTSPACE-09','flyer-stand',{285,477,12});add('ACTSPACE-09','umbrella',{284.7,476.3,12})
add('ACTSPACE-03','R027',{616,348.2,12.0957843});add('ACTSPACE-03','R029',{624,361.8,12.0957843})
-- Current campus programme moved several primary offices to T4/RB/SC/IB.
-- Append rather than renumber the already built scene instance identities.
local supplement=read(out..'/campus-supplement.json')
for _,bid in ipairs({'T4','RB','SC','IB'})do local c=supplement[bid];local origin=c.building.world_position_m
 for _,rid in ipairs(keys(c.rooms))do local r=c.rooms[rid];if r.functional_programme.kind=='admin' or r.functional_programme.kind=='reception'then
  local rx,ry,rz=table.unpack(r.position_m);local x,y,z=origin[1]+rx+3.3,origin[2]+ry-2.8,origin[3]+rz
  add('ACTSPACE-08','service-counter',{x,y,z});add('ACTSPACE-08','laptop',{x-.45,y+.15,z+1.06});add('ACTSPACE-08','sketchbook',{x+.7,y-.1,z+.8},0,{.7,.7,.3});add('ACTSPACE-08','flyer-stand',{x+1.9,y+.2,z},0,{.7,.7,.7})
  sign('ACTSPACE-08',rid,r.display_name,assert(office_en[rid]),{x,y+.4,z+1.75},2.3,.35)
  local fill=light('ACTSPACE-08',{x,y-.3,z+2.9},2500,5,{1,.96,.9});fill.cast_shadows=false
  clear('ACTSPACE-08',rid..'-approach',{origin[1]+rx,origin[2]+ry-4,z},{origin[1]+rx,origin[2]+ry,z},2)
 end end
end
plan.tree_relocations={
 {id='LEGACY-PK-0102-camphor-000-0',position_m={562.5,808,20.12},reason='Keep the full 2m jogging strip; retain tree at garden corner'},
 {id='LEGACY-PK-0102-camphor-001-0',position_m={582.5,808,20.12},reason='Keep the full 2m jogging strip; retain tree at garden corner'},
 {id='ST-03281',position_m={1015.71493,735.5297,19.977},reason='Clear performance practice ground and approach; preserve tree and matching trunk'}
}
dofile('Tools/street_activity_art_plan.lua')(plan,add,sign,light,view,base,out)
write(out..'/plan.json',plan)
print(#plan.scenes..' scenes / '..#plan.instances..' instances / '..#plan.models..' models / '..#plan.views..' camera views')
