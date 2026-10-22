-- Append stable art revision instances, preserving existing scene identifiers.
return function(plan,add,sign,light,view,base,out)
 plan.scope='Physical scenic environments with animated water and fireworks; no gameplay changes'
 plan.art_revision='art-v2'
 local function prop(s,a,p,yaw,scale,mat)return add(s,a,p,yaw,scale,{art_material=mat})end
 local function trim(s,p,size,mat,yaw)return prop(s,'platform',p,yaw,{size[1],size[2],size[3]/.12},mat)end
 local s='ACTSPACE-01';local z=41.46
 for _,q in ipairs(plan.instances)do
  if q.scene==s and q.asset=='R048'then q.art_hide=true end
  if q.scene==s and q.sign=='bear'then q.art_hide=true end
 end
 for _,q in ipairs(plan.lights)do if q.scene==s then q.lumens=900 end end
 prop(s,'R092',{599,474.65,z},0,{.85,1,.85},'brass')
 prop(s,'R010',{595.45,473,z},25,{.9,.9,.9},'boutique')
 for _,x in ipairs({595.8,602.2})do
  prop(s,'R057',{x,473.2,z},0,{.6,.6,.8},'boutique')
 end
 plan.models[#plan.models+1]={id='rose-bouquet',provider='reuse certified Tripo component',source=base..'/shop-street-components-v1/flowers/remesh/remeshed-candidate.glb',report=base..'/shop-street-components-v1/flowers/remesh/remesh-report.json'}
 for _,x in ipairs({596.6,601.4})do
  trim(s,{x,474.9,z},{1.4,.9,.58},'ivory')
  for k=-1,1 do add(s,'rose-bouquet',{x+k*.37,474.9,z+.58},k*32,{1.35,1.35,1.35})end
 end
 trim(s,{599,473.5,z+.006},{8.6,5.6,.012},'rose-rug')
 trim(s,{599,475.9,z},{9,.12,3.1},'rose')
 for i=0,17 do trim(s,{594.75+i*.5,475.8,z},{.035,.06,3.1},'brass')end
 sign(s,'bear','花語熊屋 · 收藏你的微笑','Rose & Bear · A Moment Together',{599,475.65,z+3.2},4.3,.4)
 for _,p in ipairs({{597,472,z+2.6},{601,472,z+2.6}})do local l=light(s,p,650,4,{1,.72,.65});l.cast_shadows=false end
 s='ACTSPACE-02';z=20.12
 for _,p in ipairs({{562.5,792.8,z},{582.5,792.8,z},{562.5,807.3,z},{582.5,807.3,z}})do prop(s,'R018',p,0,{.6,.6,.7},'boutique')end
 for i=0,7 do local a=i*math.pi/4;light(s,{572.5+2.3*math.cos(a),800+2.3*math.sin(a),z+.3},120,2,{.25,.7,1})end
 local v={name='02-fountain-night',eye={572.5,793,z+1.65},target={572.5,800,z+1.1},lighting='night'};plan.views[#plan.views+1]=v
 s='ACTSPACE-04';z=12.12
 -- Continue the existing sea beyond the finite city terrain so there is a true water horizon.
 prop(s,'platform',{550,-6000,-.12},0,{14000,12000,1},'ocean')
 for _,x in ipairs({616.5,623.5})do prop(s,'R057',{x,144.5,z},0,{.6,.6,.7},'boutique')end
 prop(s,'R021',{617.2,143.6,z},0,{.6,.6,.6},'boutique')
 s='ACTSPACE-06';z=19.7295113
 for _,p in ipairs({{752.8,849,z},{752.8,852,z},{762.2,849,z}})do prop(s,'R041',p,0,{.65,.65,.65},'boutique')end
 for _,y in ipairs({848.45,853.55})do trim(s,{757.5,y,z+.54},{8.5,.28,.035},'walnut')end
 for _,x in ipairs({753.4,761.6})do trim(s,{x,851,z+.54},{.28,5.35,.035},'walnut')end
 for _,x in ipairs({755,760})do light(s,{x,851,z+.3},100,2,{.35,.85,.75})end
 s='ACTSPACE-09';z=12
 for _,q in ipairs(plan.instances)do if q.scene==s and not q.sign then
  if q.asset=='umbrella'or q.asset=='flyer-stand'or q.asset=='R009'or q.asset=='R003'or q.supported_by then q.art_hide=true else q.art_material='boutique' end
 end end
 -- West boundary book wall; does not enter the offset entrance aisle x=286.6.
 for _,y in ipairs({472.8,475.25,477.7})do prop(s,'library-wall',{283.7,y,z},90,nil,'library')end
 trim(s,{289.2,474.5,z+.002},{11.3,6.4,.012},'walnut')
 for _,x in ipairs({284.6,289.2,293.7})do
  trim(s,{x,474.5,z+2.86},{.06,6,.06},'black')
  for _,y in ipairs({472.4,475.3,477})do trim(s,{x,y,z+2.8},{.12,.42,.025},'warm-lamp')end
 end
 prop(s,'R010',{284.6,477,z},90,{.65,.65,.7},'boutique')
 sign(s,'reading','書之間 · 文學與生活','Between Pages · Literature & Living',{283.98,475.5,z+2.3},3,.34,90)
 for _,p in ipairs({{284.8,473.7,z+2.65},{284.8,477,z+2.65},{291.7,475,z+2.65}})do local l=light(s,p,500,4,{1,.75,.5});l.cast_shadows=false end
 s='ACTSPACE-10';z=20.09500038
 prop(s,'practice-stage',{1023.2,750,z},0,nil,'stage')
 for _,y in ipairs({746.8,753.2})do prop(s,'stage-speaker',{1021.8,y,z+.45},-90,nil,'stage')end
 for _,y in ipairs({748.5,751.5})do prop(s,'music-stand',{1024,y,z+.45},-90,nil,'boutique')end
 for _,y in ipairs({744.2,755.8})do
  prop(s,'R005',{1017,y,z},0,{1.2,1,1},'boutique')
  prop(s,'R041',{1025.5,y,z},0,{.65,.65,.65},'boutique')
 end
 prop(s,'R014',{1013,755.5,z},0,{.8,.75,1},'boutique')
 prop(s,'staff-locker',{1011.5,745,z},90,nil,'stage')
 for _,y in ipairs({747,750,753})do light(s,{1024,y,z+3.6},950,6,{1,.65,.38})end
 trim(s,{1017.2,750,z+.008},{2,8,.014},'rose-rug')
 sign(s,'practice','星光排練舞台 · 音樂與表演','Starlight Rehearsal · Music & Performance',{1026.01,750,z+2.7},6,.65,-90)
 for _,q in ipairs(plan.views)do
  if q.name=='05-fireworks'then q.eye={620,187,15};q.target={620,80,70};q.lighting='night';q.art_time=2.4 end
  if q.name=='10-practice'then q.eye={1010.8,750,22.1};q.target={1024,750,21.7}end
  if q.name=='09-bookstore'then q.eye={286.6,471.9,13.65};q.target={283.9,476,13.4}end
  if q.name=='06-fish'then q.eye={755.6,855,21.8};q.target={757.8,850.8,20}end
 end
 plan.views[#plan.views+1]={name='05-fireworks-later',eye={620,187,15},target={620,80,70},lighting='night',art_time=4.0}
 local extras={}
 for _,q in ipairs(plan.views)do if q.name=='02-fountain' or q.name=='04-sunset' or q.name=='06-fish'then
  q.art_time=1.0;local later={};for k,v in pairs(q)do later[k]=v end;later.name=q.name..'-later';later.art_time=3.0;extras[#extras+1]=later
 end end
 for _,q in ipairs(extras)do plan.views[#plan.views+1]=q end
 for _,sc in ipairs(plan.scenes)do
  if sc.id=='ACTSPACE-01'then sc.notes='玫瑰香檳色合照角、花拱、軟座、木格柵背景與柔光；保留拍照站位。'end
  if sc.id=='ACTSPACE-05'then sc.notes='水岸觀賞座席與四組循環式三維擴散煙火，含金色垂落尾焰。'end
  if sc.id=='ACTSPACE-09'then sc.notes='參考誠品沉穩木質書牆、密集書籍陳列與軌道閱讀燈；維持兩公尺入口通道。'end
  if sc.id=='ACTSPACE-10'then sc.notes='實體排練舞台、側幕、音箱、樂譜架、觀眾長椅、遮棚休憩與設備櫃。'end
 end
 local filtered={};for _,q in ipairs(plan.instances)do if not q.art_hide then filtered[#filtered+1]=q end end;plan.instances=filtered
end
