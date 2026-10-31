-- Street dressing anchors in world cm for the offline placement scripts (place_street_city_park_props.lua,
-- place_street_city_landmarks.lua): world-blender-v1/anchors.json (native job world_anchor_probe) completed from
-- streetscape-v1/plan.json, plus a walkway lookup.
-- The street dressing is mostly merged meshes in the map (10-31 probe: only the 4688 curb instances are instanced), so
-- anchors come from streetscape-v1/plan.json, mapped into world by the plan->world axis map that puts the plan's curbs
-- on the probed world curbs (8 axis swaps/signs, translation from the centroids; at least 95 % within 50 cm).
-- Usage: local S=dofile('Tools/street_city_anchors.lua')(anchors_json) -> {A=anchors, planfit=..., nearest=fn(x,y)}
package.path='Tools/lua/?.lua;'..package.path
local j=require('ok.json')
local base='C:/portable/OpenKoikatsu/output/street-social-city-production'
local function read(p)local f=assert(io.open(p,'rb'));local s=f:read('a');f:close();return j.decode((s:gsub('^\239\187\191','')))end
return function(anchors)
 local A=read(anchors or base..'/world-blender-v1/anchors.json');local PLANFIT
 do
  local P=read(base..'/streetscape-v1/plan.json');local pc,wc={},A.anchors.curb or {}
  for _,i in ipairs(P.instances)do if i.asset=='curb' then pc[#pc+1]=i.position_m end end
  assert(#pc>100 and #wc>100,'need plan and world curbs to fit the plan frame')
  local wg={};for _,p in ipairs(wc)do local k=math.floor(p[1]/100)..','..math.floor(p[2]/100);wg[k]=true end
  local function cen(t,f)local x,y=0,0;for _,p in ipairs(t)do local a,b=f(p);x,y=x+a,y+b end;return x/#t,y/#t end
  local best
  for _,sw in ipairs({false,true})do for _,sx in ipairs({1,-1})do for _,sy in ipairs({1,-1})do
   local function m(p)local a,b=p[1]*100,p[2]*100;if sw then a,b=b,a end;return sx*a,sy*b end
   local px,py=cen(pc,m);local wx,wy=cen(wc,function(p)return p[1],p[2]end);local tx,ty=wx-px,wy-py
   local hit=0
   for _,p in ipairs(pc)do local a,b=m(p);a,b=a+tx,b+ty
    local found=false;for di=-1,1 do for dk=-1,1 do if wg[(math.floor(a/100)+di)..','..(math.floor(b/100)+dk)] then found=true end end end
    if found then hit=hit+1 end end
   if not best or hit>best.hit then best={hit=hit,m=m,tx=tx,ty=ty,sw=sw,sx=sx,sy=sy}end
  end end end
  assert(best.hit>=.95*#pc,('plan->world fit too weak: %d/%d curbs'):format(best.hit,#pc))
  local map={['bench-oak']='bench_oak',['bench-stone']='bench_stone',['lamp-lantern']='lamp_lantern',['lamp-modern']='lamp_modern',['cycle-rack']='cycle_rack',recycling='recycling',['bus-shelter']='bus_shelter',tactile='tactile',paving='paving'}
  local n=0
  for _,i in ipairs(P.instances)do local id=map[i.asset]
   if id and #(A.anchors[id] or {})==0 or (id and A.anchors[id].from_plan) then
    A.anchors[id]=A.anchors[id] or {};A.anchors[id].from_plan=true
    -- world yaw: map the plan's facing vector through the same axis map
    local a,b=best.m(i.position_m);local r=math.rad(i.yaw_deg or 0);local fx,fy=best.m({math.cos(r)/100,math.sin(r)/100})
    table.insert(A.anchors[id],{a+best.tx,b+best.ty,0,math.deg(math.atan(fy,fx))});n=n+1
   end end
  PLANFIT={curbs_matched=best.hit,curbs=#pc,swap_xy=best.sw,sign={best.sx,best.sy},translate_cm={best.tx,best.ty},anchors_from_plan=n}
  print('plan fit',best.hit..'/'..#pc,'swap',best.sw,best.sx,best.sy,'anchors',n)
 end
 -- walkway points (grid hash, 10 m cells): nearest(x,y) -> distance, unit direction to it
 local cell,grid=1000,{}
 local function key(x,y)return math.floor(x/cell)..','..math.floor(y/cell)end
 for _,id in ipairs({'tactile','curb','paving'})do for _,p in ipairs(A.anchors[id] or {})do local k=key(p[1],p[2]);grid[k]=grid[k] or {};table.insert(grid[k],p)end end
 local function nearest(x,y,reach)
  local best,bx,by=math.huge
  local cx,cy=math.floor(x/cell),math.floor(y/cell);local R=reach or 2
  for i=-R,R do for k=-R,R do for _,p in ipairs(grid[(cx+i)..','..(cy+k)] or {})do local d=(p[1]-x)^2+(p[2]-y)^2;if d<best then best,bx,by=d,p[1],p[2]end end end end
  if bx==nil then return nil end
  local d=math.sqrt(best);if d<1 then return d,1,0 end
  return d,(bx-x)/d,(by-y)/d
 end
 return {A=A,planfit=PLANFIT,nearest=nearest,read=read}
end
