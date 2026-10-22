package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json');local root='C:/portable/OpenKoikatsu/output/street-social-city-production/tree-landscaping-v3/'
local function read(p)local f=assert(io.open(p,'rb'));local r=json.decode(f:read('*a'));f:close();return r end
local function write(p,s)local f=assert(io.open(p,'wb'));f:write(s);f:close()end
local install=read(root..'land_install.json');local audit=read(root..'land_audit.json');local render=read(root..'land_render.json')
assert(audit.status=='passed' and render.status=='passed' and audit.map_sha256==render.map_sha256 and audit.map_sha256==install.map_sha256)
local s={'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1100 1100"><rect width="1100" height="1100" fill="#edf0dc"/>'}
local plan=read('C:/portable/OpenKoikatsu/output/street-social-city-production/world-ground-v1/ground-plan.json')
for _,road in ipairs(plan.roads)do local p={};for _,v in ipairs(road.points_map_m)do p[#p+1]=v[1]..','..v[2]end;s[#s+1]='<polyline points="'..table.concat(p,' ')..'" fill="none" stroke="'..(road.kind=='asphalt' and '#9a9a92' or '#dbcfb7')..'" stroke-width="'..road.width_m..'"/>'end
local bs=read('C:/portable/OpenKoikatsu/output/street-social-city-production/city-art-v1/plan.json').buildings
for id,b in pairs(bs)do local x,y=b.position_m[1],1100-b.position_m[2];local w,d=b.footprint_m[1],b.footprint_m[2];s[#s+1]=string.format('<rect x="%f" y="%f" width="%f" height="%f" fill="#b8b0a3"/><text x="%f" y="%f" font-size="9" text-anchor="middle">%s</text>',x-w/2,y-d/2,w,d,x,y,id)end
for _,q in ipairs(install.trees)do s[#s+1]=string.format('<circle cx="%.2f" cy="%.2f" r="2.2" fill="%s"><title>%s / %s</title></circle>',q.position[1],1100-q.position[2],q.actor:find('LandscapeV3_Campus_') and '#c64d80' or '#27734c',q.variant,q.support.surface)end
s[#s+1]='</svg>';write(root..'planting-map.svg',table.concat(s,'\n'))
local labels={['shops']='商店街',['campus']='校園',['forest']='森林',['resort']='渡假區',['waterfront']='河岸',['near']='近景',['middle']='中景',['far']='遠景',['overview']='全景'}
local cards={}
for _,r in ipairs(render.images)do local name=r.camera.name;local zone,dist=name:match('^(.-)%-(.-)$');cards[#cards+1]='<figure><a href="after/'..name..'.png"><img loading="lazy" src="after/'..name..'.png"></a><figcaption>'..(labels[zone] or zone)..'・'..(labels[dist] or dist)..'</figcaption></figure>'end
local c=install.counts
local walk=install.walkway_revision
assert(walk,'Native sidewalk clearance refinement is required before publication')
local intro=string.format('目前 %d 棵；校園補植 %d 棵；商店街更換飽滿樹形 %d 棵；最終新增土壤花台 %d 個。初次修正 %d 個落點、退除 %d 棵；實拍後依 %d 個現場導盲磚實例再移植 %d 棵、退除 %d 棵，完整保留人行空間。',install.visible_instances,c.campus_added,install.shade_tree_replacements,c.new_soil_planters,c.relocated,c.retired_no_safe_site,walk.tactile_samples,walk.relocated,walk.retired)
write(root..'index.html',[[<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>大學城植栽修正・v3</title><style>body{background:#f4f1e8;color:#233e30;font:17px system-ui;max-width:1400px;margin:40px auto;padding:20px}h1{font-size:36px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(380px,1fr));gap:18px}figure{margin:0;background:white;padding:12px;border-radius:12px}img{width:100%;display:block}figcaption{padding:12px}p{line-height:1.9}.map{max-width:900px;margin:30px auto}a{color:#286147}</style><h1>大學城・植栽落地與密度修正</h1><p>以下均為保存後重新開啟 Unreal 的實際場景。商店街以飽滿榕樹取代稀疏棕櫚及窄冠樹；全圖樹根依實際碰撞面材質、規畫道路與現場導盲磚、建築入口檢查。原有土壤花台保留，都市植栽增加真正的土壤方花台。校園補植混合樹群，保留運動及通行空間。</p><p>]]..intro..[[</p><p>沿用已 remesh 的 12 款樹模與四級 LOD／遠景圖集，以 HISM 共用；未新增高面數樹模。粉紅點為新增校園樹，綠點為修正後既有樹。</p><div class="map"><a href="planting-map.svg"><img src="planting-map.svg"></a></div><p>]]..audit.checks..[[ 項檢查通過，含其他元件比對、逐樹落點與人行道淨空、碰撞樹幹數、四級 LOD 與 remesh 收據。本次為幾何／位置驗收，並非遊戲 FPS 測試。</p><div class="grid">]]..table.concat(cards)..[[</div><p><a href="land_audit.json">獨立重開驗收</a> · <a href="land_install.json">逐樹修正與位置記錄</a></p></html>]])
print('Published '..root..'index.html')
