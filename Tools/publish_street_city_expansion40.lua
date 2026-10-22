package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local base='output/street-social-city-production/city-destinations-v3'
local old='output/street-social-city-production/city-destinations-v2'
local city='output/street-social-city-production/city-art-v1'
local function read(p)local f=assert(io.open(p,'rb'));local v=json.decode(f:read('*a'));f:close();return v end
local function write(p,s)local f=assert(io.open(p,'wb'));f:write(s);f:close()end
local function esc(s)return tostring(s):gsub('&','&amp;'):gsub('<','&lt;'):gsub('>','&gt;'):gsub('"','&quot;')end
local plan,install,audit,imports,extra,render=read(base..'/plan.json'),read(base..'/install.json'),read(base..'/audit.json'),read(base..'/imports.json'),read(base..'/expansion-audit.json'),read(base..'/render.json')
local cityaudit=read(city..'/audit.json')
local visual=read(base..'/visual-review-final.json');assert(visual.status=='passed' and visual.reviewed_images==70 and visual.map_sha256==install.map_sha256,'All seventy images must pass visual review')
for _,r in ipairs({install,audit,extra,render,cityaudit})do assert(r.status=='passed' and r.map_sha256==install.map_sha256,'Stale or failed native result')end
assert(read(city..'/dest_verify-launcher.json').process_success and read(city..'/dest_capture-launcher.json').process_success,'Verification and capture must exit normally')
assert(#install.instances==40 and render.count==70 and #render.images==70 and install.wall_edits==0 and extra.previous_models_preserved==30)
local unique={};local triangles=0
for id,r in pairs(imports.assets)do assert(not unique[r.job_url] and r.lod_triangles[1]<=30000 and #r.lod_triangles>=3);unique[r.job_url]=true;triangles=triangles+r.lod_triangles[1]end
for i=1,10 do assert(extra.mall_floors[tostring(i)]>=1)end
local imageids={};for _,r in ipairs(render.images)do assert(not imageids[r.id]);imageids[r.id]=true;local f=assert(io.open(r.path,'rb'));assert(f:seek('end')>5000);f:close()end
local models={};for _,r in ipairs(read(old..'/plan.json').assets)do models[r.id]=r end;for _,r in ipairs(plan.assets)do models[r.id]=r end
local assets={};for id,r in pairs(read(old..'/imports.json').assets)do assets[id]=r end;for id,r in pairs(imports.assets)do assets[id]=r end
local names=read(base..'/presentation-names.json')
local h={[1]=[=[<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>大學城 · 70 件景物 Unreal 實拍</title><style>
*{box-sizing:border-box}body{margin:0;background:#f5f0e8;color:#263a35;font:16px/1.7 system-ui}header,main{max-width:1500px;margin:auto;padding:32px}header{padding-top:60px}h1{font-size:clamp(30px,4vw,58px);margin:0}h2{margin-top:45px}p{max-width:1050px}a{color:#12675d}.stats{display:flex;flex-wrap:wrap;gap:15px}.stats span{padding:14px 20px;background:#fff;border-radius:12px}.filters{display:flex;gap:10px;flex-wrap:wrap;position:sticky;top:0;background:#f5f0e8ec;padding:15px 0;z-index:2}button{border:1px solid #a3b8ab;background:white;border-radius:24px;padding:9px 18px;font:inherit;cursor:pointer}button.active{background:#284b42;color:white}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(350px,1fr));gap:22px}article{background:#fff;border-radius:16px;overflow:hidden;box-shadow:0 5px 18px #213a3510}article img{width:100%;display:block;aspect-ratio:1.44;object-fit:cover}article .text{padding:20px}article h3{margin:0 0 5px}small{color:#586c62}table{border-collapse:collapse;width:100%;background:#fff}td,th{padding:12px;text-align:left;border-bottom:1px solid #e6e5db}code{overflow-wrap:anywhere}.note{padding:18px;background:#e5ece4;border-radius:12px}[hidden]{display:none!important}</style><header><small>UNIVERSITY CITY · ADDITIVE DESTINATIONS</small><h1>70 件景物，逐件看 Unreal 實景</h1><p>本輪新增 40 種 Tripo 模型：百貨十層各一件主題公共藝術，另 30 件分布校園、住宅、商店街、森林與海岸。保留先前 30 件景點與既有建築牆面個性。</p><div class="stats"><span>40 種全新模型</span><span>10 / 10 百貨樓層</span><span>70 張 Unreal 場景實拍</span><span>2K PBR · 三級 LOD</span></div></header><main><p class="note">以下是正式 Unreal 地圖內的實際擺放畫面，可點開原圖；模型預覽與示意圖不列為實拍。所有新模型先經指定 instant-meshes-rs，再匯入正式場景。</p><h2>新增配置與缺口補強</h2><table><tr><th>區域</th><th>補強內容</th></tr>]=]}
local needs={'各層一件不同主題的精緻公共藝術，保留店面與垂直交通前空間。','補強西側宿舍、研究館周邊與校園邊緣的植物、閱讀和動物意象。','沿既有公共設施補上運動、觀景、露營與海洋主題景物。','增加友善動物、工藝、餐飲與旅人迎賓裝置，延續不同街區個性。'}
for i,r in ipairs(plan.gap_inventory)do h[#h+1]='<tr><td>'..esc(r.area)..'</td><td>'..esc(needs[i])..'</td></tr>'end
h[#h+1]='</table><h2>逐件實拍圖庫</h2><div class="filters"><button class="active" data-filter="all">全部 70 件</button><button data-filter="mall">百貨 10 層</button><button data-filter="outdoor">新增戶外 30 件</button><button data-filter="previous">先前 30 件</button></div><div class="grid">'
local gallery={};for _,q in ipairs(render.images)do gallery[#gallery+1]=q end
table.sort(gallery,function(a,b)if a.id:sub(1,1)==b.id:sub(1,1)then return a.id<b.id end;return a.id:sub(1,1)=='E'end)
for _,q in ipairs(gallery)do
 local r=assets[q.id];local m=models[q.id];local name=names[q.id] or q.name;local floor=type(q.floor)=='number' and q.floor or nil;local category=q.id:sub(1,1)=='D' and 'previous' or (floor and 'mall' or 'outdoor');local location=floor and ('百貨 '..floor..'F') or q.zone;local prefix=q.id:sub(1,1)=='D' and '../city-destinations-v2/' or ''
 h[#h+1]='<article data-category="'..category..'"><a href="renders/'..q.id..'.png"><img loading="lazy" src="renders/'..q.id..'.png" alt="'..esc(name)..' Unreal 實景"></a><div class="text"><small>'..esc(q.id..' · '..location)..'</small><h3>'..esc(name)..'</h3><p>高度 '..m.height_m..'m · LOD 三角形 '..table.concat(r.lod_triangles,' / ')..'</p><a href="'..esc(r.job_url)..'">Tripo 原始生成</a> · <a href="'..prefix..'components/'..q.id..'/remesh/remesh-report.json">remesh 紀錄</a></div></article>'
end
h[#h+1]='</div><h2>原生驗收</h2><p>40 個新模型均具不同 Tripo 工作識別、2K 材質與三階 LOD；百貨逐層落位，140 條保留走道沒有新增阻擋。先前 30 件主物件及其庭園配件的保存狀態一致。本輪牆面改動為 0。</p><p>全城原有入口、材料與物件檢查沿用原始基準；既有歷史測線訊號留在報告中，未宣稱已修復所有歷史結構問題。未測量目標硬體 FPS，遊戲互動不在本輪範圍。</p><p><a href="expansion-audit.json">樓層與保留檢查</a> · <a href="audit.json">新資產原生檢查</a> · <a href="render.json">拍攝位置與原圖雜湊</a> · <a href="../city-art-v1/audit.json">全城基準檢查</a></p><p>地圖 SHA-256：<code>'..esc(install.map_sha256)..'</code></p></main><script>document.querySelectorAll("button[data-filter]").forEach(b=>b.onclick=()=>{document.querySelectorAll("button").forEach(x=>x.classList.toggle("active",x===b));document.querySelectorAll("article").forEach(x=>x.hidden=b.dataset.filter!=="all"&&x.dataset.category!==b.dataset.filter)})</script></html>'
write(base..'/index.html',table.concat(h,'\n'))
write(base..'/acceptance.json',json.encode({status='passed',new_unique_tripo_models=40,previous_models_preserved=30,mall_floors=10,native_images=70,wall_edits=0,new_lod0_triangles=triangles,map_sha256=install.map_sha256}))
write(city..'/index.html','<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><title>大學城景物實拍</title><body style="font:20px/1.8 system-ui;max-width:900px;margin:70px auto;padding:24px;background:#f7f0e5;color:#314137"><h1>新增40種景物，70件逐一實拍</h1><p>保留原有牆面個性與先前30件景物；百貨十層各一件新公共藝術，另30件加入戶外場景。</p><p><a href="../city-destinations-v3/index.html">查看全部70件 Unreal 實景照片與驗收</a></p></body></html>')
local previous=assert(io.open(old..'/index.html','rb'));local page=previous:read('*a');previous:close()
if not page:find('data-expansion40',1,true)then page=page:gsub('</style>','</style><aside data-expansion40 style="padding:20px;text-align:center;background:#fff3c8"><a href="../city-destinations-v3/index.html">新版：新增40種物件與全部70件 Unreal 實拍圖庫</a></aside>',1);write(old..'/index.html',page)end
print('Published forty new destinations and seventy Unreal photographs')
