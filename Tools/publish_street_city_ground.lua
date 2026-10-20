-- Publish only after actual remeshed ground and Unreal map checks have passed.
local ok = require "ok"
local json, P, U = ok.json, ok.path, ok.util
local O = json.object

local function win(p) return (P.norm(p):gsub("/", "\\")) end
local function sha(p) return ok.hash.file(p, "sha256") end
local function load(p) return json.decode(P.read_text(p)) end

local ROOT = P.parent(ok.script_dir); local OUT = P.join(ROOT, "output/street-social-city-production/world-ground-v1")
local ground = load(P.join(OUT, "ground-acceptance.json")); local world = load(P.join(OUT, "unreal-acceptance.json"))
assert(ground.status == world.status and world.status == "passed" and ground.chunk_count == 16, "assertion failed")
local render = load(P.join(OUT, "unreal-render.json")); assert(render.status == "passed" and #render.images == 2, "assertion failed")
for _, image in ipairs(render.images) do assert(P.is_file(image.file) and P.size(image.file) == image.bytes, "assertion failed") end
for _, stage in ipairs({ "assemble", "render" }) do
  assert(load(P.join(OUT, "unreal-" .. stage .. "-task.json")).status == "succeeded", "assertion failed")
end
local map_file = P.join(ROOT, "StreetSocialCity/Content/UniversityCity/Maps/UniversityCity.umap")
assert(P.is_file(map_file) and P.size(map_file) > 100, "assertion failed")
local bindings = load(P.join(OUT, "unreal-material-bindings.json")); assert(bindings.status == "passed", "assertion failed")
do
  local a, b = {}, {}
  for _, r in ipairs(bindings.bindings) do a[r.mesh] = true end
  for _, r in ipairs(world.actors) do b[r.asset] = true end
  for k in pairs(a) do assert(b[k], "assertion failed") end
  for k in pairs(b) do assert(a[k], "assertion failed") end
end
for _, chunk in ipairs(ground.chunks) do
  for _, name in ipairs({ "ground", "water" }) do
    local item = chunk[name]
    if U.truthy(item) then assert(sha(item.path) == item.sha256, "assertion failed") end
  end
end
local names = { { "urban", "商店街鋪面" }, { "grass", "草地" }, { "campus", "校園石材" }, { "forest", "山林地面" }, { "riverbank", "河岸砂礫" }, { "sand", "沙灘" } }
local cards = {}
for _, kl in ipairs(names) do
  local kind, label = kl[1], kl[2]
  cards[#cards + 1] = ('<article><h3>%s</h3><div class="views"><img src="references/%s/front.png"><img src="references/%s/left.png"></div><p>GPT雙視圖 → Tripo Model → Rust remesh → 2K PBR烘焙</p><a href="assets/%s/remesh/remeshed-candidate.glb">元件 GLB</a> · <a href="assets/%s/remesh/remesh-report.json">remesh 紀錄</a></article>'):format(label, kind, kind, kind, kind)
end
local tiles = {}
for _, c in ipairs(ground.chunks) do
  local origin = c.world_origin_m; local tri = c.triangles
  tiles[#tiles + 1] = ('<tr><td>%s</td><td>%s, %s</td><td>275×275m</td><td>%s</td><td><a href="chunks/%s/remesh/remeshed-candidate.glb">地面GLB</a></td></tr>'):format(U.str(c.id), U.str(origin[1]), U.str(origin[2]), U.commas(tri), U.str(c.id))
end
local hero = ""
for _, nl in ipairs({ { "unreal-world-overview", "Unreal 大地圖" }, { "unreal-shop-street", "Unreal 商店街" } }) do
  local name, label = nl[1], nl[2]
  if P.exists(P.join(OUT, name .. ".png")) then hero = hero .. ('<h2>%s</h2><img class="hero" src="%s.png">'):format(label, name) end
end
local text = '<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><title>大學城地面與 Unreal 場景</title><style>body{background:#eff0ea;color:#233b36;font:16px system-ui;margin:0 auto;padding:32px;max-width:1450px}h1{font-size:32px}a{color:#126d68}.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:20px}article{background:white;border-radius:12px;padding:18px}.views{display:flex;gap:8px}.views img{width:48%;height:210px;object-fit:contain}.hero{width:100%;border-radius:12px}table{width:100%;border-collapse:collapse;background:white}td,th{padding:10px;border-bottom:1px solid #d7ddd6}.note{background:#fff;padding:20px;border-left:5px solid #328576}@media(max-width:850px){.grid{grid-template-columns:1fr}}</style><h1>澄川大學城 · 地面與 Unreal 場景</h1><p>1,100×1,100m · 16塊地面 · 96棟商店／288層 · 2K貼圖</p><div class="note">已通過：地面實際GLB ' .. U.commas(ground.checks) .. '項驗收、Unreal場景 ' .. U.commas(world.checks) .. '項驗收。共用邊界≤2mm，入口平台與一樓銜接；所有採用幾何先執行指定Rust remesh。Unreal目前為靜態環境與實體碰撞，門／電梯的遊戲控制器尚未接入。</div><p><a href="../../../StreetSocialCity/OpenCity.cmd">開啟 Unreal 場景</a> · <a href="../../../StreetSocialCity/StreetSocialCity.uproject">Unreal 專案</a> · <a href="ground-acceptance.json">地面驗收</a> · <a href="unreal-acceptance.json">Unreal驗收</a> · <a href="../shop-street-v1/index.html">商店街單棟模型</a></p>' .. hero .. '<h2>整個世界的分塊與地形規畫</h2><object data="ground-chunks.svg" type="image/svg+xml" style="width:100%;height:850px"></object><h2>六種 Tripo 地面來源</h2><div class="grid">' .. table.concat(cards) .. '</div><h2>16塊地面</h2><table><tr><th>區塊</th><th>東／北原點(m)</th><th>長寬</th><th>含水面三角面</th><th>模型</th></tr>' .. table.concat(tiles) .. '</table><h2>來源與處理方式</h2><p>GPT雙視圖由Tripo Model生成地面元件；大地圖依既有SVG位置與概念標高建立共用高度場，再取樣Tripo表面細節及材質，製作可銜接的分塊。車道、步道與入口依尺寸契約建立。宏觀高度是設計資料，並非實測地形。Rust原始結果、必要的來源拓樸恢復、固定邊界與材質烘焙均保留報告。</p><p>本批組裝店面與地面；學校、百貨等其他建物仍保留規畫位置。靜態水面不代表完整水體模擬；原遊戲StreetSocial.cmd尚未切換至此場景。</p><h2>Tripo 實際生成畫面</h2><img class="hero" src="tripo-ground-proof.jpg"></html>'
P.write_text(P.join(OUT, "index.html"), text)
local readme = [[
# 大學城地面與 Unreal 場景

已完成16塊275×275m地面，覆蓋1,100×1,100m；六種GPT雙視圖／Tripo Model來源已下載、正規化、Rust remesh並烘焙2K PBR。

Unreal專案：`C:/portable/OpenKoikatsu/StreetSocialCity/StreetSocialCity.uproject`。雙擊同目錄`OpenCity.cmd`，使用本專案引擎（`Tools/ok_engine.lua`，預設E:/PhysX/UE5.8）開啟`/Game/UniversityCity/Maps/UniversityCity`。

場景包含96棟／288層商店、16塊地面及獨立水面。地面]] .. U.commas(ground.checks) .. "項、Unreal" .. U.commas(world.checks) .. [[項實際驗收通過；詳見兩份acceptance.json。來源Blender座標為東／北／上公尺，Unreal為北／東／上公分；glTF匯入資產需90°基準旋轉，再扣除建物原方位角。

所有幾何先通過指定Rust引擎；相同來源版本可複用。地面使用共用高度樣本與XY極值固定，重新匯入檢查24條共用邊界。Rust之後必要的來源拓樸恢復、雙向位移上界證明、UV轉移均如實記錄，不宣稱是純Rust原始拓樸。

道路表層按每條路拆開，避免多個薄封閉件全域焊接後造成非流形與過度細分；土層位於鋪面下方。每塊地面含水面總面數≤100,000，貼圖≤2K。水面獨立且無碰撞，地面與建築使用精確三角形碰撞，沒有以整棟凸包堵住房門與樓梯。

這是可編輯的Unreal靜態環境。門／電梯原動畫保存在原始建物GLB，這批Unreal樓層為靜態姿態，尚未接入遊戲控制器。學校、百貨等建物未在此批建模；原StreetSocial.cmd未切換。來源地形高度為概念設計，非實測資料。未宣稱FPS或完整遊戲功能驗收。
]]
P.write_text(P.join(OUT, "README.md"), readme)
local files = { map_file, P.join(OUT, "ground-acceptance.json"), P.join(OUT, "unreal-acceptance.json"), P.join(OUT, "unreal-material-bindings.json"), P.join(OUT, "unreal-render.json"), P.join(OUT, "ground-plan.json"), P.join(OUT, "index.html") }
for _, row in ipairs(render.images) do files[#files + 1] = row.file end
local tri, maxtri = 0, nil
for _, c in ipairs(ground.chunks) do tri = tri + c.triangles; if maxtri == nil or c.triangles > maxtri then maxtri = c.triangles end end
local listed = json.array({})
for _, p in ipairs(files) do listed[#listed + 1] = O({ { "path", win(p) }, { "bytes", P.size(p) }, { "sha256", sha(p) } }) end
P.write_text(P.join(OUT, "delivery.json"), json.encode(O({ { "status", "passed" }, { "chunks", 16 }, { "shops", 96 }, { "floors", 288 }, { "ground_triangles", tri }, { "max_chunk_triangles", maxtri }, { "files", listed } }), { indent = 2 }))
print("World ground delivery published")
