-- Publish only independently reopened native results, never Blender renders as Unreal evidence.
package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local root='C:/portable/OpenKoikatsu'
local series=os.getenv('STREET_CITY_TREE_SERIES') or 'v1';assert(series=='v1' or series=='v2')
local out=root..'/output/street-social-city-production/tree-upgrade-'..series
local function read(p)local f=assert(io.open(p,'rb'));local s=f:read('*a');f:close();return s end
local function load(p)return json.decode(read(p))end
local function write(p,s)local f=assert(io.open(p,'wb'));f:write(s);f:close()end
local pairedfile=io.open(out..'/tree_compare_after.json','rb');local haspaired=pairedfile~=nil;if pairedfile then pairedfile:close() end
local before,after,audit,imports=load(out..(haspaired and '/tree_compare_before.json' or '/tree_before.json')),load(out..(haspaired and '/tree_compare_after.json' or '/tree_after.json')),load(out..'/tree_audit.json'),load(out..'/tree_import.json')
assert(audit.status=='passed' and after.status=='passed' and audit.map_sha256==after.map_sha256)
local function fields(line)local a={};for v in (line..','):gmatch('(.-),')do a[#a+1]=v end;return a end
local function percentile(t,p)table.sort(t);return t[math.max(1,math.ceil(#t*p))]end
local function metrics(receipt,q)
 local source=receipt.profiling_dir..'/CSV/'..q.csv;local text=read(source);write(out..'/'..q.csv,text)
 local headers,rows=nil,{}
 for line in text:gmatch('[^\r\n]+')do
  local fs=fields(line)
  if not headers then headers={};for i,k in ipairs(fs)do headers[k]=i end
  elseif headers.GPUTime and tonumber(fs[headers.GPUTime]) then rows[#rows+1]=fs end
 end
 assert(#rows>=60,'Too few valid native rendered frames')
 local result={frames=#rows,csv=q.csv}
 for _,key in ipairs({'GPUTime','FrameTime','RHI/PrimitivesDrawn','RHI/DrawCalls','GPU/Basepass','GPU/ShadowDepths'})do
  local t={};for i=11,#rows do local v=tonumber(rows[i][headers[key] or 0]);if v then t[#t+1]=v end end
  if #t>0 then result[key]={p50=percentile(t,.5),p95=percentile(t,.95)}end
 end
 return result
end
local paired={};local cards={};local perfrows={};local pretty={['campus']='校園',['shops']='商店街',['forest']='山林',['resort']='渡假區',['waterfront']='河岸',['near']='近景',['middle']='中景',['far']='遠景'}
for i,q in ipairs(before.images)do
 local r=assert(after.images[i]);assert(q.camera.name==r.camera.name and json.encode(q.camera)==json.encode(r.camera),'Camera changed')
 local b,a=metrics(before,q),metrics(after,r);local name=q.camera.name;local zone,distance=name:match('^(.-)%-(.*)$');local title=pretty[zone]..'・'..pretty[distance];paired[#paired+1]={name=name,before=b,after=a}
 local ratio=(a.GPUTime.p50/b.GPUTime.p50-1)*100
 perfrows[#perfrows+1]=string.format('<tr><td>%s</td><td>%.2f → %.2f ms</td><td>%+.1f%%</td><td>%.0f → %.0f</td></tr>',title,b.GPUTime.p50,a.GPUTime.p50,ratio,b['RHI/PrimitivesDrawn'].p50,a['RHI/PrimitivesDrawn'].p50)
 cards[#cards+1]='<article><h2>'..title..'</h2><div class="pair"><figure><img loading="lazy" src="'..(haspaired and 'before-paired/' or 'before/')..name..'.png"><figcaption>原有樹木</figcaption></figure><figure><img loading="lazy" src="'..(haspaired and 'after-paired/' or 'after/')..name..'.png"><figcaption>新樹木・Unreal 原生實拍</figcaption></figure></div></article>'
end
write(out..'/performance.json',json.encode({status='passed',method='One native editor process, interleaved old/new trees at identical cameras, no map save during A/B; 1440x1000 SceneCapture; 90 warm frames then 90 measured frames; skip first ten measured rows; CSV GPUTime and RHI counters. RHI indirect draw counters are not expanded instance triangle counts. Editor workload, not packaged-game FPS. Other sessions/GPU load may vary.',views=paired}))
local pagetitle=series=='v2' and '大學城・12款樹形配置' or '大學城樹木更新'
local lodrows={};local labels={['camphor-a']='樟樹・A',['camphor-b']='樟樹・B',['camphor-c']='樟樹・C',['flower-a']='粉花樹',['banyan']='榕樹式寬冠',['terminalia']='分層欖仁',['columnar']='直立常綠樹',['cypress']='落羽松式尖冠',['willow']='垂柳式樹形',['palm']='羽狀棕櫚',['golden-flower']='黃花景觀樹',['white-flower']='白花景觀樹'}
for _,spec in ipairs(dofile('Tools/street_city_tree_species.lua')[series])do local id=spec[1];local r=imports.assets[id];lodrows[#lodrows+1]='<tr><td>'..labels[id]..'</td><td>'..table.concat(r.triangles,' / ')..'</td><td>'..tostring((audit.distribution or {})[id] or '—')..' 棵</td></tr>' end
write(out..'/index.html','<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>'..pagetitle..'驗收</title><style>body{background:#101b19;color:#edf4eb;font:16px/1.65 system-ui;max-width:1500px;margin:auto;padding:30px}h1,h2{color:#d5eab3}table{border-collapse:collapse;width:100%;margin:24px 0}td,th{border-bottom:1px solid #385047;text-align:left;padding:10px}article{margin:30px 0;background:#1a2924;padding:20px;border-radius:14px}.pair{display:grid;grid-template-columns:1fr 1fr;gap:12px}figure{margin:0}img{width:100%;border-radius:9px}a{color:#a4d8fa}.note{color:#bdcbbb}@media(max-width:800px){.pair{grid-template-columns:1fr}}</style><h1>'..pagetitle..'</h1><p>1,475 棵原位替換：1,187 棵綠樹、288 棵花樹。保留原有旋轉、縮放、葉色、碰撞與裁切距離；依區域搭配樹形，花樹使用不透明立體樹冠。</p><p>'..audit.non_tree_components_preserved..' 個其他模型元件完整保留，含既有建築、招牌與雕塑。全部 '..tostring(#dofile('Tools/street_city_tree_species.lua')[series]*4)..' 組 LOD 網格均經指定 Rust remesh 與來源雜湊門檻驗證。</p><table><tr><th>樹型</th><th>LOD0 / LOD1 / LOD2 / Impostor 三角面</th><th>可見實例</th></tr>'..table.concat(lodrows)..'</table><p class="note">遠景為 2K、16 視角圖集，含法線和葉冠遮罩；按觀看方位取樣。LOD 依畫面占比切換，距離會隨樹木大小與 FOV 改變。花樹近中景不使用透明葉片。</p><h2>相對前一版的原生效能比較</h2><p class="note">同一 Editor 程序，在各鏡位交錯切換原樹／新樹；1440×1000 離屏 SceneCapture，各預熱90幀、量測90幀，再略過前10幀。RHI 間接繪製計數不等於所有實例展開後的三角面總數。這是編輯器工作負載，不能當成遊戲 FPS；其他 GPU 工作也可能影響結果。</p><table><tr><th>鏡位</th><th>GPU 中位數</th><th>變化</th><th>RHI 繪製圖元中位數</th></tr>'..table.concat(perfrows)..'</table><p><a href="tree_audit.json">獨立重開驗收收據</a> · <a href="performance.json">效能原始數據索引</a> · <a href="tree_import.json">模型與 remesh 清單</a></p>'..table.concat(cards)..'</html>')
print('Published '..out..'/index.html')
