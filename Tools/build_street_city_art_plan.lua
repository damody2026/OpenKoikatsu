-- City-wide palette and scenic placement rules. All geometry is certified reuse.
package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local base='C:/portable/OpenKoikatsu/output/street-social-city-production'
local out=base..'/city-art-v1'
local function read(p)local f=assert(io.open(p,'rb'));local v=json.decode(f:read('*a'));f:close();return v end
local probe=read(out..'/probe.json');local contract=read('Docs/StreetSocialCity/production/building-floor-audit.json').buildings
local plan={version='20261021-city-art-01',scope='All 122 buildings; walls, shared spaces and exterior scenic dressing; gameplay unchanged',decor={},corridors={},views={},buildings={}}
local buildings={};for _,b in ipairs(contract)do buildings[b.id]=b end
local function view(name,eye,target,lighting)plan.views[#plan.views+1]={name=name,eye=eye,target=target,lighting=lighting}end
local function add(bid,asset,p,yaw,scale)
 plan.decor[#plan.decor+1]={id=bid..'-'..#plan.decor,building=bid,asset=asset,position_m=p,yaw=yaw or 0,scale=scale or {1,1,1}}
end
local directions={N={0,1},S={0,-1},W={-1,0},E={1,0}}
for _,q in ipairs(probe.floors)do if q.floor==1 then
 local b=assert(buildings[q.bid]);local pos={q.position_cm[2]/100,q.position_cm[1]/100,q.position_cm[3]/100}
 plan.buildings[q.bid]={position_m=pos,footprint_m=b.footprint_m,name=b.name}
 for ei,e in ipairs(b.entrances)do
  local d=directions[e.facade_side];local p=e.position_m
  if d and p then
   local x,y=pos[1]+p[1],pos[2]+p[2];local tx,ty=-d[2],d[1]
   add(q.bid,'entry-strip',{x+d[1]*.03,y+d[2]*.03,pos[3]+(e.height_m or 2.4)+.15},math.deg(math.atan(d[2],d[1]))+90,{.65,.08,.25})
   plan.decor[#plan.decor].mount='lintel'
   add(q.bid,'canopy',{x+d[1]*.55,y+d[2]*.55,pos[3]+(e.height_m or 2.4)+.35},math.deg(math.atan(d[2],d[1]))+90,{(e.width_m or 2.4)+.8,1.25,.7})
   plan.decor[#plan.decor].mount='canopy'
   -- Preserve a straight approach and a full 2.4m threshold, including offset shop doors.
   plan.corridors[#plan.corridors+1]={id=e.id,building=q.bid,width_m=math.max(2,e.width_m or 2.4),a={x+d[1]*.15,y+d[2]*.15,pos[3]},b={x+d[1]*3.8,y+d[2]*3.8,pos[3]}}
   local shop=q.bid:match('^S%d+$')
   for _,side in ipairs({-1,1})do
    local offset=shop and 2.4 or 4.5
    local pos2={x+d[1]*1.1+tx*offset*side,y+d[2]*1.1+ty*offset*side,pos[3]}
    -- Small shops alternate warm seats / herb pots; larger entrances get layered seating.
    local aid=side==-1 and 'R042' or (shop and tonumber(q.bid:sub(2))%3~=0) and 'R042' or 'R009'
    add(q.bid,aid,pos2,math.deg(math.atan(d[2],d[1]))+90,aid=='R042' and {.65,.65,.75} or {.8,.8,.9})
    if not shop and side==-1 then add(q.bid,'rose-bouquet',{pos2[1]+tx*.85,pos2[2]+ty*.85,pos[3]},12,{1.05,1.05,1.05})end
   end
  end
 end
end end
-- Additional destination pockets, away from carriageways and existing activity access strips.
local pockets={
 {'campus-reading',{752,891,20},'R014','R009','R021'},
 {'campus-courtyard',{560,727,20},'R014','R005','R021'},
 {'campus-dorm',{490,890,20},'R013','R009','R022'},
 {'hotel-garden',{753,257,12},'R017','R004','R021'},
 {'riverside',{598,205,12},'R014','R005','R021'},
 {'residential',{163,388,12},'R013','R009','R021'},
 {'office-plaza',{861,409,12},'R014','R005','R021'},
 {'bay-garden',{991,349,12},'R013','R004','R021'},
 {'civic-garden',{648,303,12},'R014','R009','R021'},
 {'station-court',{173,207,12},'R014','R005','R021'}
}
for _,r in ipairs(pockets)do local id,p=r[1],r[2];add(id,r[3],p);for _,dx in ipairs({-1.25,1.25})do add(id,r[4],{p[1]+dx,p[2],p[3]},dx<0 and -90 or 90)end;add(id,r[5],{p[1],p[2],p[3]});add(id,'R042',{p[1]-2.6,p[2]+1.5,p[3]},0,{.9,.9,.9});add(id,'R057',{p[1]+2.5,p[2]+1.5,p[3]},0,{.6,.6,.8})end
view('world-overview',{620,280,220},{560,600,10})
view('campus-overview',{540,525,110},{540,800,22})
view('campus-engineering',{422,748,23},{465,805,29})
view('campus-library',{590,540,22},{625,580,27})
view('campus-dormitories',{440,890,22},{490,932,32})
view('campus-sports',{372,723,24},{300,770,27})
view('campus-administration',{675,625,22},{700,670,27})
view('shop-north',{225,465,13.65},{260,480,15})
view('shop-central',{342,402,13.65},{380,398,15})
view('shop-south',{310,335,13.65},{350,349,15})
view('mall-front',{568,412,16},{655,460,28})
view('mall-offices',{774,383,18},{824,410,25})
view('hotel-garden',{747,243,17},{735,286,18})
view('residential',{76,278,14},{125,335,20})
view('culture-centre',{550,274,14},{600,315,19})
view('transport-station',{68,170,14},{125,210,17})
for _,q in ipairs(read(base..'/resort-native-v1/render.json').images)do if q.view=='campus-lake'or q.view=='bay-camp'or q.view=='flower-detail'then view(q.view,q.eye,q.target)end end
view('street-night',{225,465,13.65},{260,480,15},'night')
view('hotel-night',{747,243,17},{735,286,18},'night')
view('campus-night',{590,540,22},{625,580,27},'night')
-- Retain the accepted activity art views as regression checks.
local activity=read(base..'/activity-scenes-v1/plan.json')
for _,q in ipairs(activity.views)do if q.name=='04-sunset'or q.name=='01-bear'or q.name=='09-bookstore'then view('retained-'..q.name,q.eye,q.target,q.lighting)end end
local f=assert(io.open(out..'/plan.json','wb'));f:write(json.encode(plan));f:close()
print(#plan.decor..' dressing candidates / '..#plan.corridors..' entrance strips / '..#plan.views..' views')
