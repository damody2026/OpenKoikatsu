package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json');local root='C:/portable/OpenKoikatsu/output/street-social-city-production/grass-hills-v1/'
local function read(name)local f=assert(io.open(root..name,'rb'));local v=json.decode(f:read('*a'));f:close();return v end
local install=read('grass_install.json');local audit=read('grass_audit.json');local render,stamp
for _,name in ipairs({'grass_render.json','grass_merge.json'})do local f=io.open(root..name,'rb');if f then local r=json.decode(f:read('*a'));f:close();local t=r.run_id:match('(%d+%-%d+)$');if r.status=='passed' and r.map_sha256==audit.map_sha256 and (not stamp or t>stamp)then render,stamp=r,t end end end
assert(render and install.status=='passed' and audit.status=='passed' and install.map_sha256==audit.map_sha256);assert(#render.images==6)
local labels={['forest-near']='山腳・近景',['forest-middle']='山坡・中景',['forest-far']='北側山地・遠景',['waterfront-far']='河岸遠眺綠山',['campus-far']='校園後方綠山',['grass-detail']='草皮近距離・normal 細節'}
local cards={}
for _,r in ipairs(render.images)do local name=r.camera.name;local before=name~='grass-detail' and '<figure><a href="../tree-landscaping-v3/after/'..name..'.png"><img loading="lazy" src="../tree-landscaping-v3/after/'..name..'.png"></a><figcaption>修改前</figcaption></figure>' or '';cards[#cards+1]='<section><h2>'..labels[name]..'</h2><div class="pair">'..before..'<figure><a href="after/'..name..'.png"><img loading="lazy" src="after/'..name..'.png"></a><figcaption>Unreal 實拍・綠色草坡</figcaption></figure></div></section>'end
local textures={};for _,t in ipairs({{'basecolor','草地 Base Color'}, {'normal','草地 Normal'}, {'roughness','草地 Roughness'}})do textures[#textures+1]='<figure><a href="../world-ground-v1/assets/grass/'..t[1]..'-2k.png"><img loading="lazy" src="../world-ground-v1/assets/grass/'..t[1]..'-2k.png"></a><figcaption>'..t[2]..'・2K</figcaption></figure>'end
local f=assert(io.open(root..'index.html','wb'));f:write([[<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>大學城・綠色草坡與 Normal</title><style>body{font:17px system-ui;background:#f2f2e8;color:#294736;margin:35px auto;max-width:1450px;padding:20px}h1{font-size:36px}p{line-height:1.9}.pair,.textures{display:grid;grid-template-columns:repeat(auto-fit,minmax(350px,1fr));gap:16px}figure{margin:0;background:white;padding:10px;border-radius:10px}img{width:100%;display:block}figcaption{padding:10px}.textures img{aspect-ratio:1;object-fit:cover}a{color:#285e45}</style><h1>大學城・綠色草坡與 Normal</h1><p>山坡與土丘改成有草地紋理的綠色表面，使用 2K Base Color、Normal、Roughness。世界座標三向投影避免陡坡貼圖拉伸；normal 以線性法線取樣、方向轉換後混合為世界空間法線。草皮細節會隨光照呈現，不只是把土色塗綠。</p><p>已替換 ]]..audit.changed_slots..[[ 個自然地表材質槽；地形面數增加 0，其他元件與植栽配置比對通過。獨立重開驗收 ]]..audit.checks..[[ 項通過。Shader：]]..audit.shader.pixel..[[ pixel instructions／]]..audit.shader.samplers..[[ samplers。本次未宣稱遊戲 FPS 測試。</p>]]..table.concat(cards)..[[<h2>實際使用的貼圖來源</h2><div class="textures">]]..table.concat(textures)..[[</div><p><a href="grass_audit.json">原生驗收</a> · <a href="grass_install.json">材質槽與貼圖記錄</a></p></html>]]);f:close();print('Published '..root..'index.html')
-- Keep historical images intact, but make the latest review discoverable from the user's old tab.
local legacy=root..'../tree-upgrade-v2/index.html';local old=io.open(legacy,'rb')
if old then
 local html=old:read('*a');old:close()
 if not html:find('latest-terrain-review',1,true) then
  local backup=assert(io.open(root..'../tree-upgrade-v2/index-before-grass-note.html','wb'));backup:write(html);backup:close()
  local note=[[<aside id="latest-terrain-review" style="padding:18px;background:#e5efdf;border:1px solid #7c9c70;margin-bottom:20px">此頁保留 v2 歷史照片。最新：<a href="../grass-hills-v1/index.html">綠色草坡與 normal 實拍對照</a> · <a href="../tree-landscaping-v3/index.html">植栽位置修正</a></aside>]]
  local n;html,n=html:gsub('<h1',note..'<h1',1);assert(n==1);local out=assert(io.open(legacy,'wb'));out:write(html);out:close()
 end
end
