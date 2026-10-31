-- Placement rules for the Blender park props (Tools/build_street_city_park_props.lua), computed offline from the
-- installed street dressing (world-blender-v1/anchors.json, native job world_anchor_probe) and the coast curve
-- (world-edge-v1/coast-edge-probe.json). World cm; yaw in the props' Blender frame (front = local -y), so a front
-- faces direction (dx,dy) at yaw = atan2(dx,-dy). The native job world_park_props_install maps each mesh's axes and
-- rejects placements on uneven ground, water or overlapping actors.
-- Rules (gap inventory section 3):
--   picnic_table      behind benches (away from the nearest walkway), on the lawn side        Z2 Z5 Z9
--   drinking_fountain beside benches                                                         Z5 Z7
--   campus_map / notice_board / signpost beside campus lantern lamps, facing the walkway     Z5
--   bike_shelter      behind cycle racks                                                      Z5
--   banner_pole / aframe_sign  beside modern lamps / recycling stations on the shop street   Z3
--   lifebuoy_post / beach_shower  along the beach, inland of the smoothed coast curve, facing the sea   Z1
-- Candidates are taken in farthest-point order so each kind spreads over the city; every placement keeps a walkway
-- clearance (half depth + margin from the nearest tactile / curb / paving instance) and a spacing to every other prop.
-- Output world-blender-v1/park-props/placements.json.
-- Usage: lua Tools/place_street_city_park_props.lua [--anchors <json>] [--out <json>]
package.path='Tools/lua/?.lua;'..package.path
local j=require('ok.json')
local base='C:/portable/OpenKoikatsu/output/street-social-city-production'
local function read(p)local f=assert(io.open(p,'rb'));local s=f:read('a');f:close();return j.decode((s:gsub('^\239\187\191','')))end
local opt={anchors=base..'/world-blender-v1/anchors.json',out=base..'/world-blender-v1/park-props/placements.json'}
for i=1,#arg-1 do local k=arg[i]:match('^%-%-(%w+)$');if k and opt[k] then opt[k]=arg[i+1] end end
local S=dofile('Tools/street_city_anchors.lua')(opt.anchors);local A,nearest,PLANFIT=S.A,S.nearest,S.planfit
local B=read(base..'/world-blender-v1/park-props/build.json')
local C=read(base..'/world-edge-v1/coast-edge-probe.json')
local placed={}
local function far(list)
 -- farthest-point order, starting from the anchor nearest the origin (deterministic)
 local n=#list;if n==0 then return {}end
 local order,used,md={},{},{}
 local s=1;for i=2,n do if list[i][1]+list[i][2]<list[s][1]+list[s][2] then s=i end end
 for i=1,n do md[i]=math.huge end
 local cur=s
 for _=1,n do
  order[#order+1]=list[cur];used[cur]=true
  local nb,nd=nil,-1
  for i=1,n do if not used[i] then local d=(list[i][1]-list[cur][1])^2+(list[i][2]-list[cur][2])^2;if d<md[i] then md[i]=d end;if md[i]>nd then nb,nd=i,md[i]end end end
  if not nb then break end;cur=nb
 end
 return order
end
local why={}
local function ok(kind,x,y,clear,same,any)
 local d=nearest(x,y);if d and d<clear then why[kind]=why[kind] or {};why[kind].walkway=(why[kind].walkway or 0)+1;return false end
 for _,p in ipairs(placed)do local q=(p.cm[1]-x)^2+(p.cm[2]-y)^2;if q<any*any or (p.kind==kind and q<same*same) then why[kind]=why[kind] or {};why[kind].spacing=(why[kind].spacing or 0)+1;return false end end
 return true
end
local function yaw(dx,dy)return math.deg(math.atan(dx,-dy))end
local function depth(kind)return B.models[kind].size_m[2]*50 end
local function inbox(p,b)return p[1]>=b[1] and p[1]<=b[3] and p[2]>=b[2] and p[2]<=b[4]end
local Z3={28000,15000,48000,60000}
local report={}
-- one rule: for anchors (farthest-point order), candidate = anchor + off(d,t) where d points to the walkway and t is
-- its left-hand perpendicular; front faces the walkway
local function rule(kind,ids,n,f,o)
 local list={}
 for _,id in ipairs(ids)do for _,p in ipairs(A.anchors[id] or {})do if not o.box or inbox(p,o.box) then list[#list+1]=p end end end
 local got=0
 for _,a in ipairs(far(list))do
  if got>=n then break end
  local d,dx,dy=nearest(a[1],a[2])
  -- no walkway near (plazas, forecourts): face along the anchor's own facing instead
  if not (d and d<(o.near or 1500)) then local r=math.rad(a[4] or 0);d,dx,dy=0,math.cos(r),math.sin(r);why[kind]=why[kind] or {};why[kind].faced_by_anchor=(why[kind].faced_by_anchor or 0)+1 end
  do
   local tx,ty=-dy,dx;local u,v=f(o)
   local x,y=a[1]+dx*u+tx*v,a[2]+dy*u+ty*v
   if ok(kind,x,y,depth(kind)+(o.clear or 60),o.same,o.any or 250) then
    -- face the anchor's walkway direction (the prop's own nearest point can be a path end or corner)
    placed[#placed+1]={kind=kind,cm={math.floor(x+.5),math.floor(y+.5)},yaw=yaw(dx,dy),anchor=a};got=got+1
   end
  end
 end
 report[kind]={anchors=#list,placed=got,wanted=n,tally=why[kind]}
end
local benches={'bench_oak','bench_stone'}
-- racks stand on paved plazas: the shelter goes over the rack row's back edge, kept only off the tactile/curb line
rule('bike_shelter',{'cycle_rack'},6,function()return -150,0 end,{clear=-40,same=5000})
rule('picnic_table',benches,40,function()return -450,0 end,{clear=300,same=1500,any=400})
rule('drinking_fountain',benches,15,function()return 0,180 end,{same=4000})
rule('campus_map',{'lamp_lantern'},10,function()return -30,150 end,{same=6000})
rule('notice_board',{'lamp_lantern'},10,function()return -30,-150 end,{same=6000})
rule('signpost',{'lamp_lantern'},25,function()return 0,100 end,{same=3000})
rule('banner_pole',{'lamp_modern'},40,function()return 0,90 end,{same=1200,box=Z3})
rule('aframe_sign',{'recycling'},40,function()return 0,120 end,{clear=10,same=1000,box=Z3})
-- beach: the beach props' smoothed curve (outermost land within +-12 m, seaward box smoothing, at most 4 m out)
do
 local segs={{}}
 for _,r in ipairs(C.rows)do local x=r.edge and r.edge.land_x
  if x==nil or x==j.null then if #segs[#segs]>0 then segs[#segs+1]={}end else table.insert(segs[#segs],{r.y,x})end end
 local got={lifebuoy_post=0,beach_shower=0}
 for _,s in ipairs(segs)do if #s>=40 then
  local cur={}
  for i,p in ipairs(s)do local m=math.huge;for _,q in ipairs(s)do if math.abs(q[1]-p[1])<=1200 and q[2]<m then m=q[2]end end;cur[i]=m end
  for _=1,3 do local nx={};for i=1,#cur do local a,b=math.max(1,i-4),math.min(#cur,i+4);local t=0;for k=a,b do t=t+cur[k]end;nx[i]=math.min(cur[i],t/(b-a+1))end;cur=nx end
  for i=1,#cur do cur[i]=math.max(cur[i],s[i][2]-400)end
  local function at(y,inl)
   local i=1;for k=1,#s do if math.abs(s[k][1]-y)<math.abs(s[i][1]-y) then i=k end end
   local a,b=cur[math.max(1,i-3)],cur[math.min(#cur,i+3)];local dy=s[math.min(#cur,i+3)][1]-s[math.max(1,i-3)][1]
   -- seaward normal of the curve x=f(y): (-1, f'(y)) normalised
   local fp=(b-a)/dy;local L=math.sqrt(1+fp*fp)
   return cur[i]+inl,s[i][1],-1/L,fp/L
  end
  local lo,hi=s[1][1]+1500,s[#s][1]-1500
  local y=lo+1200
  while y<=hi do local x,yy,nx,ny=at(y,600);if ok('lifebuoy_post',x,yy,0,4000,250) then placed[#placed+1]={kind='lifebuoy_post',cm={math.floor(x),yy},yaw=yaw(nx,ny)};got.lifebuoy_post=got.lifebuoy_post+1 end;y=y+5000 end
  y=lo+4000
  while y<=hi do local x,yy,nx,ny=at(y,800);if ok('beach_shower',x,yy,0,10000,250) then placed[#placed+1]={kind='beach_shower',cm={math.floor(x),yy},yaw=yaw(nx,ny)};got.beach_shower=got.beach_shower+1 end;y=y+17000 end
 end end
 report.lifebuoy_post={placed=got.lifebuoy_post};report.beach_shower={placed=got.beach_shower}
end
for _,p in ipairs(placed)do p.yaw=math.floor(p.yaw*10+.5)/10;p.anchor=nil end
local out={status='computed_requires_native_install',anchors_map_sha256=A.map_sha256,plan_fit=PLANFIT,coast_probe_map_sha256=C.map_sha256,report=report,placements=placed}
local f=assert(io.open(opt.out,'wb'));f:write(j.encode(out));f:close()
for k,v in pairs(report)do print(k,j.encode(v))end
print('PLACED',#placed)
