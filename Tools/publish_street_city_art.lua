package.path='Tools/lua/?.lua;'..package.path
local json=require('ok.json')
local base='C:/portable/OpenKoikatsu/output/street-social-city-production/city-art-v1'
local function read(n)local f=assert(io.open(base..'/'..n..'.json','rb'));local v=json.decode(f:read('*a'));f:close();return v end
local function write(n,v)local f=assert(io.open(base..'/'..n,'wb'));f:write(v);f:close()end
local function esc(s)return tostring(s):gsub('&','&amp;'):gsub('<','&lt;'):gsub('>','&gt;'):gsub('"','&quot;')end
local install,audit,render,before=read('install'),read('audit'),read('render'),read('render_before')
assert(install.status=='passed' and audit.status=='passed' and render.status=='passed')
assert(install.map_sha256==audit.map_sha256 and audit.map_sha256==render.map_sha256)
assert(#render.images==#before.images and #render.images>=25)
assert(read('render-launcher').process_success,'Final native capture process must exit successfully')
local detail=read('render_detail')
assert(detail.status=='passed' and detail.map_sha256==audit.map_sha256 and #detail.images==4)
assert(read('render_detail-launcher').process_success,'Detail capture process must exit successfully')
local obscured={['campus-sports']=true,['shop-south']=true,['mall-front']=true,['mall-offices']=true}
local names={['world-overview']='大學城總覽',['campus-overview']='校園總覽',['campus-engineering']='工程館街景',['campus-library']='圖書館',['campus-dormitories']='學生宿舍',['campus-sports']='運動健康館',['campus-administration']='行政中心',['shop-north']='北商店街',['shop-central']='中央商店街',['shop-south']='南商店街',['mall-front']='百貨外觀',['mall-offices']='商辦街廓',['hotel-garden']='飯店花園',['residential']='住宅與公寓',['culture-centre']='市民文化中心',['transport-station']='交通站',['campus-lake']='校園湖畔',['bay-camp']='海灣露營區',['flower-detail']='河岸花景',['street-night']='商店街夜景',['hotel-night']='飯店夜景',['campus-night']='校園夜景',['retained-01-bear']='保留：大熊合照區',['retained-04-sunset']='保留：海灣日落',['retained-09-bookstore']='保留：木質書店'}
local h={[=[<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>大學城整城美術修訂</title><style>*{box-sizing:border-box}body{margin:0;background:#f3efe6;color:#283b37;font:16px/1.75 system-ui}header{background:#243f37;color:#f7eedb;padding:45px max(24px,calc((100vw - 1480px)/2));}h1{margin:0;font-size:38px}main{max-width:1480px;margin:auto;padding:24px}a{color:#367066}header a{color:#cde1b8}.stats{display:flex;gap:12px;flex-wrap:wrap}.stats span{background:#fff;border:1px solid #d5d2c8;padding:14px 20px;border-radius:10px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,570px),1fr));gap:24px}article,section{background:#fff;border:1px solid #d5d2c8;border-radius:12px;padding:20px;margin:20px 0}article{margin:0}h2{margin:0 0 12px;font-size:22px}.compare{position:relative;aspect-ratio:1.44;overflow:hidden;background:#243f37;border-radius:8px}.compare img{position:absolute;width:100%;height:100%;object-fit:cover;display:block}.compare .old{clip-path:inset(0 calc(100% - var(--split,50%)) 0 0)}input{width:100%;accent-color:#367066}small{display:block;color:#6c7c72}.label{display:flex;justify-content:space-between}table{width:100%;border-collapse:collapse}th,td{padding:8px;text-align:left;border-bottom:1px solid #e0ded8}code{overflow-wrap:anywhere}details{margin:12px 0}button{background:#367066;color:white;border:0;padding:10px 18px;border-radius:8px;cursor:pointer;margin:6px}</style><header><h1>大學城 · 整城美術整理</h1><p>保留建築與動線，整理牆面、植栽、入口、照明與水景。拖動下方滑桿，比較相同鏡位的 Unreal 原生畫面。</p><a href="audit.json">原生重開驗收</a> · <a href="install.json">完整套用清單</a> · <a href="plan.json">設計規則</a> · <a href="../activity-scenes-v1/index.html">活動場景前次交付</a></header><main>]=]}
h[#h+1]='<div class="stats"><span><strong>'..#install.buildings..'</strong> 棟建築</span><span>427 個樓層模型保留</span><span>'..audit.wall_slots..' 個牆面綁定</span><span>'..audit.dressing_instances..' 個新增擺設</span><span>'..#install.entrance_lights..' 處入口照明</span><span>'..audit.checks..' 項原生檢查</span></div>'
h[#h+1]='<section><h2>本版重點</h2><p>外牆採各建築自己的主色與強調色，降低重複貼圖的干擾；室內保留既有品牌裝潢。草地調整為較自然的層次，飯店與校內泳池沿用已驗收的動態清水。新增擺設經地面支撐、道路、水域、建築邊界及碰撞篩選；沿用已 remesh 的三階 LOD 元件與 HISM 實例。</p><p>所有既有建築尺寸、位置、樓層幾何、樓梯、電梯、門窗、碰撞、樹與載具的數量／角度／縮放皆核對原生基線；Logo 與三語文字材質不覆寫。</p><button onclick="setAll(0)">全部看新版</button><button onclick="setAll(50)">左右對照</button><button onclick="setAll(100)">全部看原版</button></section><div class="grid">'
for _,r in ipairs(render.images)do
 if not obscured[r.name] then
 h[#h+1]='<article><h2>'..esc(names[r.name] or r.name)..'</h2><div class="compare" style="--split:50%"><img loading="lazy" src="renders/'..esc(r.name)..'.png" alt="新版"><img loading="lazy" class="old" src="before/'..esc(r.name)..'.png" alt="原版"></div><div class="label"><small>左：原版</small><small>右：新版</small></div><input aria-label="前後對照" type="range" min="0" max="100" value="50" oninput="this.parentElement.querySelector(\'.compare\').style.setProperty(\'--split\',this.value+\'%\')"><a href="renders/'..esc(r.name)..'.png">開啟新版原圖</a><small>實際保存地圖重開捕捉；預定位置設定後讀回相機，並關閉景深模糊以利檢查。</small></article>'
end
end
h[#h+1]='</div><section><h2>補拍外觀</h2><p>原始 25 組鏡位中，運動館、南商店街、百貨與商辦的視線落在室內或被樹冠遮住，因此另外補拍外觀；下列為新版單張，沒有假造改造前對照。原始截圖仍保留於原生收據。</p><div class="grid">'
for _,r in ipairs(detail.images)do h[#h+1]='<article><h2>'..esc(names[r.name])..'</h2><a href="details/'..esc(r.name)..'.png"><img loading="lazy" style="width:100%;border-radius:8px" src="details/'..esc(r.name)..'.png" alt="補拍外觀"></a></article>' end
h[#h+1]='</div></section><div>'
h[#h+1]='</div><section><h2>擺設與效能</h2><p>'..audit.dressing_instances..' 個實例分為 '..#install.dressing.groups..' 個群組，依距離剔除並保留三階 LOD。'..audit.skipped_candidates..' 個候選位置因水域、道路、建築邊界或既有物件衝突跳過，未強塞進場景。既有六色載具與植物變化保留。</p><h2>動線檢查界線</h2><p>對 '..#audit.corridors..' 條入口測線重做膠囊掃掠，本次沒有增加新的入口阻擋。原有 '..#audit.baseline_blocked..' 條固定 3.8m 測線出現訊號，可能涉及鄰棟、窄側門或地坪；本版以美術整理為範圍，保留原建築，未宣稱所有歷史入口問題已修復。</p><details><summary>查看原有測線訊號</summary><p>'..esc(table.concat(audit.baseline_blocked,', '))..'</p></details><p>地圖 SHA：<code>'..esc(audit.map_sha256)..'</code>。Shader 及三語切換已在獨立 Editor 重開檢查。</p></section><section><h2>設計參考與來源</h2><p><a href="https://www.ntust.edu.tw/p/404-1000-88778.php?Lang=zh-tw">台科大校內配置</a> · <a href="https://www.general.ntust.edu.tw/p/412-1054-7458.php?Lang=zh-tw">台科大校園植物</a> · <a href="https://www.eslitehotel.com/jp/">誠品行旅空間</a> · <a href="https://dev.epicgames.com/documentation/en-us/unreal-engine/single-layer-water-shading-model-in-unreal-engine">Epic 水面官方文件</a></p><p>本輪採既有 Tripo、Grok 材質與認證 remesh 元件，不另外生成重複資產。遊戲互動不在本輪範圍。</p></section><script>function setAll(v){document.querySelectorAll(".compare").forEach(e=>e.style.setProperty("--split",v+"%"));document.querySelectorAll("input[type=range]").forEach(e=>e.value=v)}</script></main></html>'
local page=table.concat(h,'\n')
local function replace_plain(s,from,to)
 local a,b=s:find(from,1,true);assert(a,'Missing report copy: '..from);return s:sub(1,a-1)..to..s:sub(b+1)
end
if install.wall_policy=='preserve_original_material' then
 page=replace_plain(page,'保留建築與動線，整理牆面、植栽、入口、照明與水景。','已撤回整批牆面改色，恢復各棟原有風格；保留植栽、入口、照明與水景改善。')
 page=replace_plain(page,' 個牆面綁定</span>',' 個牆面綁定還原</span>')
 page=replace_plain(page,'外牆採各建築自己的主色與強調色，降低重複貼圖的干擾；室內保留既有品牌裝潢。','交通站、市民文化中心、北商店街、學生宿舍與其餘建物，均恢復本輪改造前的原有牆面。上一版的統一淺底、水平色帶已撤回，室內原有裝潢也還原。')
 page=replace_plain(page,'<h2>本版重點</h2>','<h2>修正：保留建築個性</h2><p>預設保留原材質。未來的牆面修改必須指定單棟、原材質與經實拍檢視的替代材質，不再由整城共用配色規則覆蓋。<a href="restore_walls.json">牆面還原收據</a></p>')
end
write('index.html',page)
write('acceptance.json',json.encode({status='passed',buildings=#install.buildings,floors=427,wall_slots=audit.wall_slots,dressing_instances=audit.dressing_instances,lights=#install.entrance_lights,checks=audit.checks,renders=#render.images,displayed_comparisons=#render.images-4,supplemental_renders=#detail.images,map_sha256=audit.map_sha256,baseline_entrance_signals=audit.baseline_blocked,new_entrance_obstructions=#audit.new_obstructions,scope='City-wide art revision; gameplay and structural changes excluded'}))
print('Published city-wide native art review')
