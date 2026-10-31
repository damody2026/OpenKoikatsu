-- Placement rules for the Blender landmarks (Tools/build_street_city_landmarks.lua), computed offline like the park
-- props (Tools/place_street_city_park_props.lua): anchors and walkways from Tools/street_city_anchors.lua, zone boxes from
-- Docs/STREET_CITY_WORLD_GAP_INVENTORY_20261029.md (world metres -> cm). Landmarks have no single street anchor, so each
-- kind samples its zone on a 5 m grid and keeps the spots that match its ground:
--   paved   (plaza_fountain, newsstand, cafe sets; Z3): a walkway / paving point within 3 m, no street furniture within
--           the prop's radius + margin
--   lawn    (gazebo Z6, playgrounds Z2 Z9, clock_tower Z5, stage Z5 Z2): no walkway within radius + 3 m but one within
--           40 m (reachable, seen from the path), no furniture near
--   coast   (lighthouse Z1): on the beach curve (world-edge-v1/coast-edge-probe.json) next to the river mouth
-- then farthest-point order inside the zone, spacing to every other landmark and to the park props, front facing the
-- nearest walkway. Candidates beyond the wanted count are spares: the native install (world_landmarks_install) tries
-- them in order when a spot is taken by a building or is uneven, and stops at limits[kind].
-- seawall is not placed here: it follows the river-mouth waterline (shore-grid probe, pending).
-- Output world-blender-v1/landmarks/placements.json.
-- Usage: lua Tools/place_street_city_landmarks.lua [--anchors <json>] [--out <json>]
package.path='Tools/lua/?.lua;'..package.path
local j=require('ok.json')
local base='C:/portable/OpenKoikatsu/output/street-social-city-production'
local opt={anchors=base..'/world-blender-v1/anchors.json',out=base..'/world-blender-v1/landmarks/placements.json'}
for i=1,#arg-1 do local k=arg[i]:match('^%-%-(%w+)$');if k and opt[k] then opt[k]=arg[i+1] end end
local S=dofile('Tools/street_city_anchors.lua')(opt.anchors);local A,nearest,read=S.A,S.nearest,S.read
local B=read(base..'/world-blender-v1/landmarks/build.json')
local C=read(base..'/world-edge-v1/coast-edge-probe.json')
local park=read(base..'/world-blender-v1/park-props/placements.json')
local Z={Z1={0,0,15000,105000},Z2={15000,0,30000,108000},Z3={28000,15000,48000,60000},Z5={45000,0,95000,90000},Z6={0,84000,110000,94000},Z9={95000,0,110000,105000}}
-- street furniture that a landmark must keep clear of (walkway points are handled by nearest())
local furn={}
for _,id in ipairs({'bench_oak','bench_stone','lamp_lantern','lamp_modern','cycle_rack','recycling','bus_shelter'})do for _,p in ipairs(A.anchors[id] or {})do furn[#furn+1]=p end end
local fg={};local function fk(x,y)return math.floor(x/1000)..','..math.floor(y/1000)end
for _,p in ipairs(furn)do local k=fk(p[1],p[2]);fg[k]=fg[k] or {};table.insert(fg[k],p)end
local function furn_within(x,y,r)
 local cx,cy=math.floor(x/1000),math.floor(y/1000);local R=math.ceil(r/1000)
 for i=-R,R do for k=-R,R do for _,p in ipairs(fg[(cx+i)..','..(cy+k)] or {})do if (p[1]-x)^2+(p[2]-y)^2<r*r then return true end end end end
 return false
end
local placed,report={},{}
for _,p in ipairs(park.placements)do placed[#placed+1]={kind='park:'..p.kind,cm=p.cm,r=150}end
local function radius(kind)local s=B.models[kind].size_m;return math.sqrt(s[1]^2+s[2]^2)*50 end
local function yaw(dx,dy)return math.deg(math.atan(dx,-dy))end
local function spaced(x,y,r,kind,same)
 for _,p in ipairs(placed)do local q=math.sqrt((p.cm[1]-x)^2+(p.cm[2]-y)^2);if q<r+(p.r or 0)+150 or (p.kind==kind and q<same) then return false end end
 return true
end
local function far(list,c)
 -- farthest-point order from the candidate nearest the zone centre (deterministic)
 local n=#list;if n==0 then return {}end
 local s=1;for i=2,n do if (list[i][1]-c[1])^2+(list[i][2]-c[2])^2<(list[s][1]-c[1])^2+(list[s][2]-c[2])^2 then s=i end end
 local order,used,md={},{},{};for i=1,n do md[i]=math.huge end;local cur=s
 for _=1,n do
  order[#order+1]=list[cur];used[cur]=true;local nb,nd=nil,-1
  for i=1,n do if not used[i] then local d=(list[i][1]-list[cur][1])^2+(list[i][2]-list[cur][2])^2;if d<md[i] then md[i]=d end;if md[i]>nd then nb,nd=i,md[i]end end end
  if not nb then break end;cur=nb
 end
 return order
end
local limits={}
local function rule(kind,zones,want,ground,same)
 local r=radius(kind);local list={};local why={}
 for _,zn in ipairs(zones)do local b=Z[zn]
  for x=b[1]+r,b[3]-r,500 do for y=b[2]+r,b[4]-r,500 do
   local d,dx,dy=nearest(x,y,5);local good
   if ground=='paved' then good=d and d<=300
   else good=d and d>=r+300 and d<=4000 end
   if not good then why.ground=(why.ground or 0)+1
   elseif furn_within(x,y,r+200) then why.furniture=(why.furniture or 0)+1
   else list[#list+1]={x,y,dx,dy} end
  end end
 end
 local c={0,0};for _,zn in ipairs(zones)do local b=Z[zn];c[1]=c[1]+(b[1]+b[3])/2/#zones;c[2]=c[2]+(b[2]+b[4])/2/#zones end
 local got=0
 for _,p in ipairs(far(list,c))do
  if got>=want*4 then break end
  -- spares (beyond the wanted count) only keep 30 m from their own kind: they stand in for a taken spot
  if spaced(p[1],p[2],r,kind,got<want and same or math.min(same,3000)) then placed[#placed+1]={kind=kind,cm={math.floor(p[1]+.5),math.floor(p[2]+.5)},yaw=yaw(p[3],p[4]),r=r};got=got+1 else why.spacing=(why.spacing or 0)+1 end
 end
 limits[kind]=want;report[kind]={candidates=#list,listed=got,wanted=want,tally=why}
end
rule('plaza_fountain',{'Z3'},2,'paved',15000)
rule('newsstand',{'Z3'},4,'paved',8000)
rule('cafe_set_a',{'Z3'},8,'paved',2000)
rule('cafe_set_b',{'Z3'},8,'paved',2000)
rule('clock_tower',{'Z5'},1,'lawn',1e9)
rule('stage',{'Z5','Z2'},1,'lawn',1e9)
rule('gazebo',{'Z6'},3,'lawn',20000)
rule('playground_a',{'Z2','Z9'},2,'lawn',20000)
rule('playground_b',{'Z2','Z9'},2,'lawn',20000)
-- lighthouse: the beach end next to the river mouth (the coast segment row nearest the mouth), 8 m inland, facing the sea
do
 local best
 for i,r in ipairs(C.rows)do local x=r.edge and r.edge.land_x
  local nxt=C.rows[i+1];local nx=nxt and nxt.edge and nxt.edge.land_x
  if x~=nil and x~=j.null and (nx==nil or nx==j.null) and r.y>60000 and r.y<90000 then best={x,r.y} end
 end
 assert(best,'river mouth not found on the coast probe')
 local list={}
 for k=0,7 do list[#list+1]={kind='lighthouse',cm={math.floor(best[1]+800),best[2]-1000-k*600},yaw=yaw(-1,0),r=radius('lighthouse')}end
 for _,p in ipairs(list)do placed[#placed+1]=p end
 limits.lighthouse=1;report.lighthouse={mouth_cm=best,listed=#list,wanted=1}
end
local out={status='computed_requires_native_install',anchors_map_sha256=A.map_sha256,plan_fit=S.planfit,coast_probe_map_sha256=C.map_sha256,limits=limits,report=report,placements={}}
for _,p in ipairs(placed)do if not p.kind:find('^park:') then out.placements[#out.placements+1]={kind=p.kind,cm=p.cm,yaw=math.floor(p.yaw*10+.5)/10}end end
local f=assert(io.open(opt.out,'wb'));f:write(j.encode(out));f:close()
for k,v in pairs(report)do print(k,j.encode(v))end
print('LISTED',#out.placements)
