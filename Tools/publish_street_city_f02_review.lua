-- Publish final status only after independent exported-geometry acceptance.
local ok = require "ok"
local json, P, U = ok.json, ok.path, ok.util
local O = json.object

local function html_escape(s)
  return (s:gsub("&", "&amp;"):gsub("<", "&lt;"):gsub(">", "&gt;"):gsub('"', "&quot;"):gsub("'", "&#x27;"))
end

local ROOT = P.parent(ok.script_dir)
local BASE = P.join(ROOT, "output/street-social-city-production/S01-modular-components-v1")
local OUT = P.join(BASE, "assembly-f02")
local function read(p) return json.decode(P.read_text(p)) end
local function write(p, value) P.write_text(p, json.encode(value, { ensure_ascii = false, indent = 2 })) end
local function concat(a, b) local t = json.array({}); for _, v in ipairs(a) do t[#t + 1] = v end; for _, v in ipairs(b) do t[#t + 1] = v end; return t end

local accept = read(P.join(OUT, "acceptance.json")); local assembly = read(P.join(OUT, "assembly-report.json"))
local curved = read(P.join(BASE, "generated-components/acceptance.json")); local localc = read(P.join(BASE, "local-components/acceptance.json"))
assert(accept.status == "passed" and curved.status == "passed" and not U.truthy(assembly.missing), "assertion failed")
assert(ok.hash.file(accept.source, "sha256") == accept.sha256, "assertion failed")
local records = concat(localc.records, curved.records); assert(#records == 16, "assertion failed")
local catalog = read(P.join(BASE, "components.json")); local byid = {}
for _, r in ipairs(records) do byid[r.id] = r end
for _, c in ipairs(catalog.components) do
  local r = byid[c.id]; c.current_status = "passed_export_reimport"; c.local_remesh_verified = r
  c.current_route = (c.id == "chair" or c.id == "toilet" or c.id == "sink") and "Tripo Model, mandatory Rust pass and documented surface repair" or "local rectangular construction and mandatory Rust remesh"
  c.runtime_triangle_cap = r.cap; c.measured_triangles = r.triangles
end
catalog.scope = "S01 F02 completed geometry and reusable component references"
catalog.mesh_verified = "16 component versions; assembled F02 exported geometry accepted"
catalog.current_review = "assembly-f02/index.html"
catalog.assembly_acceptance = "assembly-f02/acceptance.json"
write(P.join(BASE, "components.json"), catalog)
assembly.status = "passed_exported_geometry"; assembly.approved = true
assembly.acceptance = P.join(OUT, "acceptance.json"):gsub("/", "\\")
assembly.limitations = concat(accept.limitations, curved.limitations); write(P.join(OUT, "assembly-report.json"), assembly)
for _, key in ipairs(json.keys(assembly.sources)) do
  local source = assembly.sources[key]
  local path = source.remesh_report; local r = read(path)
  r.adopted = true; r.adoption_scope = "S01 F02 exported geometry accepted; game integration pending"; write(path, r)
end
write(P.join(BASE, "component-acceptance.json"), O({ { "status", "passed" }, { "verified", 16 }, { "records", records }, { "assembly_acceptance", "assembly-f02/acceptance.json" }, { "limitations", curved.limitations } }))
local style = "body{max-width:1280px;margin:32px auto;padding:20px;background:#f4f1eb;color:#26332f;font:17px/1.7 system-ui}a{color:#176458}img{width:100%;height:auto;border-radius:10px}table{border-collapse:collapse;width:100%;background:white}td,th{padding:10px;border-bottom:1px solid #ddd;text-align:left}.status{background:#dcece1;padding:18px;border-radius:10px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:20px}article{padding:15px;background:white;border-radius:10px}article img{height:250px;object-fit:contain}small{color:#58645e}"
local function name_of(id) for _, c in ipairs(catalog.components) do if c.id == id then return c.name end end; error("StopIteration") end
local rows = {}
for _, r in ipairs(records) do
  local path = P.rel(r.path, BASE); local name = name_of(r.id)
  local note = U.truthy(r.reference_topology_recovery) and "來源拓樸修復" or "指定 Rust remesh"
  local dims = {}; for i, d in ipairs(r.size_m) do dims[i] = ("%.3f"):format(d) end
  rows[#rows + 1] = ("<tr><td>%s (%s)</td><td>%s m</td><td>%s / %s</td><td>%s</td><td><a href=\"../%s\" download>GLB</a></td></tr>"):format(
    html_escape(name), U.str(r.id), table.concat(dims, " × "), U.commas(r.triangles), U.commas(r.cap), note, path)
end
local page = '<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>S01 二樓完成模型與驗收</title><style>' .. style .. '</style><h1>S01 二樓｜完成模型與驗收</h1><p class="status">16種基本元件完成。實際匯出重新匯入後，' .. U.commas(accept.checks) .. '項幾何檢查通過，整層含重複家具共 ' .. U.commas(accept.triangles) .. ' 三角面 / 100,000上限。</p><p><a href="S01-F02-assembled-2k.blend" download>下載 Blender</a> · <a href="S01-F02-assembled-2k.glb" download>下載 GLB</a> · <a href="acceptance.json">逐項驗收</a> · <a href="assembly-report.json">元件、位置與來源</a></p><p>外框12×18m，二樓完成面Z=3.8m，樓高3.8m、牆厚0.2m。主走廊至少2m、門淨寬1.2m，實際樓梯每階高度、2.2m淨高及樓層抵達面均通過抽樣。貼圖最大2048×2048。</p><img src="S01-F02-assembled-review.png" alt="二樓完成模型剖視渲染"><p><small>圖中天花板及部分外牆僅在渲染隱藏；下載模型保留完整牆體與樓板開孔。</small></p><h2>三個 Tripo 元件完成外觀</h2><img src="../generated-components/components-review.png" alt="重新匯入的椅子馬桶洗手台"><p>使用GPT參考圖，Tripo Model雙圖Multi-view與2K貼圖；椅子複用既有生成結果，馬桶及洗手台新增生成。原始下載、Rust原始OBJ、被拒候選及修復紀錄保留。</p><p>衛浴的純Rust輸出漏掉細部表面，因此最終版本在指定remesh後，另以來源連接拓樸重建，採用受限於1mm的remesh位置場；缺失特徵由來源恢復。這兩件並非純Rust輸出拓樸。椅子及馬桶保留來源開口接縫，不能宣稱為封閉實體。</p><h2>已完成的幾何驗收範圍</h2><p>四桌、十六椅、六書架、四百二十本書、衛浴、門窗、電梯車廂與上下樓梯。門片與電梯門獨立，預留樞紐。尚未接入遊戲門、電梯控制及角色導航；驗收為實際匯出幾何，不代表遊戲內測試。F01只建立相容的樓梯銜接檢查情境，並非完整一樓模型。</p><h2>16種元件尺寸與實測面數</h2><table><tr><th>元件</th><th>長寬高</th><th>三角面 / 執行上限</th><th>處理</th><th>檔案</th></tr>' .. table.concat(rows) .. '</table><h2>Tripo生成證據</h2><img src="../generated-components/toilet/tripo-result.png" alt="Tripo馬桶生成與匯出畫面"><img src="../generated-components/sink/tripo-result.png" alt="Tripo洗手台生成與匯出畫面"><p><a href="../component-acceptance.json">完整元件重新匯入檢查</a> · <a href="../components.json">尺寸與參考圖清單</a> · <a href="../index.html">圖片圖冊</a></p></html>'
P.write_text(P.join(OUT, "index.html"), page)
local cards = {}
for _, c in ipairs(catalog.components) do
  local r = byid[c.id]; local link = P.rel(r.path, BASE)
  local dims = {}; for i, d in ipairs(c.size) do dims[i] = U.str(d) end
  cards[#cards + 1] = ('<article><a href="%s"><img src="%s" alt="%s"></a><h2>%s</h2><p>%s m</p><p>實測 %s 三角面；執行上限 %s</p><a href="%s" download>完成模型GLB</a> · <a href="%s" download>GPT參考圖</a></article>'):format(
    U.str(c.file), U.str(c.file), html_escape(c.name), html_escape(c.name), table.concat(dims, " × "), U.commas(r.triangles), U.commas(r.cap), link, U.str(c.file))
end
P.write_text(P.join(BASE, "index.html"), '<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>S01 二樓16種元件</title><style>' .. style .. '</style><h1>S01 二樓｜16種元件與完整組裝</h1><p class="status">已完成16種元件及二樓組裝；' .. U.commas(accept.triangles) .. '三角面，' .. U.commas(accept.checks) .. '項匯出幾何驗收通過。<a href="assembly-f02/index.html">查看完成樓層、限制與下載</a></p><p>下方為GPT參考圖，模型尺寸由程式與實際GLB重新匯入檢查，不由圖片推定。長方體結構以程式製作；曲面家具使用Tripo Model。全部資產版本均跑指定Rust remesh，衛浴包含另行記錄的來源拓樸修復。</p><main class="grid">' .. table.concat(cards) .. '</main><p><a href="components.json">完整清單</a> · <a href="component-acceptance.json">16種元件驗收</a></p></html>')
-- Preserve old verification evidence but remove stale progress assertions from UI.
P.write_text(P.join(BASE, "local-components/index.html"), '<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><title>基本板件驗收</title><style>' .. style .. '</style><h1>13種本機基本板件</h1><p>此頁為早期板件驗收資料。椅子、衛浴及完整二樓已完成，請開啟<a href="../assembly-f02/index.html">最新16種元件與二樓驗收頁</a>。</p><img src="component-review.png"><p><a href="acceptance.json">早期板件檢查快照</a></p></html>')
print(json.encode(O({ { "status", "published" }, { "triangles", accept.triangles }, { "checks", accept.checks }, { "components", #records } })))
