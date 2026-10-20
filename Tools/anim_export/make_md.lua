-- Render Docs/ANIMATION_AND_MOUTH.md from the TSV/CSV exports.
--
-- Reads Saved/anim_export/*.tsv and writes a Chinese reference document with the
-- animation source inventory, Studio catalogue summary, and the full
-- FaceBlendShape mouth/eye/eyebrow pattern tables.
--
-- Run:
--   Tools\lua\lua.exe Tools/anim_export/make_md.lua [export_dir] [md_path]
local ok = require "ok"
local path, util = ok.path, ok.util

local OUT = arg[1] or "C:\\portable\\OpenKoikatsu\\Saved\\anim_export"
local MD = arg[2] or "C:\\portable\\OpenKoikatsu\\Docs\\ANIMATION_AND_MOUTH.md"

local SEP = "\t"

local function read_tsv(name)
  local p = OUT .. "\\" .. name
  if not path.is_file(p) then return {} end
  local text = path.read_text(p, { keep_bom = true })
  if text == "" then return {} end
  if text:sub(-1) ~= "\n" then text = text .. "\n" end
  local rows = {}
  for line in text:gmatch("(.-)\n") do rows[#rows + 1] = util.split(line, SEP) end
  return util.slice(rows, 2)
end

-- Python str.strip() including the Unicode spaces that occur in names.
local USPACE = { "\u{3000}", "\u{a0}", "\u{2000}", "\u{2001}", "\u{2002}", "\u{2003}", "\u{2004}", "\u{2005}", "\u{2006}", "\u{2007}",
  "\u{2008}", "\u{2009}", "\u{200a}", "\u{2028}", "\u{2029}", "\u{202f}", "\u{205f}", "\u{85}", "\u{1680}" }
local function pystrip(s)
  local changed = true
  while changed do
    changed = false
    local t = s:gsub("^[ \t\n\r\v\f\28\29\30\31]+", ""):gsub("[ \t\n\r\v\f\28\29\30\31]+$", "")
    if t ~= s then s = t; changed = true end
    for _, u in ipairs(USPACE) do
      if s:sub(1, #u) == u then s = s:sub(#u + 1); changed = true end
      if #s >= #u and s:sub(-#u) == u then s = s:sub(1, -#u - 1); changed = true end
    end
  end
  return s
end

local function cell(s)
  return pystrip(util.replace(util.replace(s or "", "|", "\\|"), "\n", " "))
end

local function table_lines(header, rows)
  local dashes = {}
  for i = 1, #header do dashes[i] = "---" end
  local out = { "| " .. table.concat(header, " | ") .. " |", "|" .. table.concat(dashes, "|") .. "|" }
  for _, r in ipairs(rows) do
    local cells = {}
    for i, c in ipairs(r) do cells[i] = cell(c) end
    out[#out + 1] = "| " .. table.concat(cells, " | ") .. " |"
  end
  return out
end

-- int(s) or 0
local function num(s)
  if type(s) ~= "string" then return 0 end
  local t = pystrip(s)
  if not t:find("^[+-]?%d+$") and not t:find("^[+-]?%d+[_%d]*%d$") then return 0 end
  if t:find("__") then return 0 end
  return math.tointeger(tonumber((t:gsub("_", "")))) or 0
end

local function commas(n) return util.commas(n) end
local function add(list, more) for _, v in ipairs(more) do list[#list + 1] = v end end

-- Ordered counter: insertion-ordered keys (tables keyed by a string form).
local function counter()
  return { keys = {}, values = {}, raw = {} }
end
local function bump(c, key, by, raw)
  local k = type(key) == "table" and table.concat(key, "\0") or key
  if c.values[k] == nil then c.keys[#c.keys + 1] = k; c.values[k] = 0; c.raw[k] = raw or key end
  c.values[k] = c.values[k] + (by or 1)
end

local function main()
  local md = {}
  add(md, {
    "# Koikatsu Sunshine 動畫與嘴型資料總表",
    "",
    "> 由 `Tools/anim_export/export_anim_mouth.lua` 自動產生。",
    "> 資料來源：本機 Koikatsu Sunshine 安裝的 `abdata/` 與 `mods/*.zipmod`。",
    "> 完整明細見同目錄（`Saved/anim_export/`）的 TSV／CSV。",
    "",
    "本文件分兩部分：**動畫資料**（AnimationClip 清單、AnimatorController、" ..
    "Studio 動畫目錄）與**嘴型資料**（FaceBlendShape 的 Mouth／Eyes／Eyebrow " ..
    "控制器、PtnSet 開閉配對、blendshape 名稱、lip-sync 機制）。",
    "",
  })

  -- ---------------------------------------------------------------- sources
  local src = read_tsv("animation_sources.tsv")
  local clips = read_tsv("animation_clips.tsv")
  local base_src = util.filter(src, function(r) return #r > 0 and r[1] == "base" end)
  local mod_src = util.filter(src, function(r) return #r > 0 and r[1] == "mod" end)
  local total_clips = util.sum(src, function(r) return num(r[3]) end)
  local base_clips = util.sum(base_src, function(r) return num(r[3]) end)
  local mod_clips = util.sum(mod_src, function(r) return num(r[3]) end)
  local total_ctrl = util.sum(src, function(r) return num(r[4]) end)

  add(md, {
    "## 0. 摘要",
    "",
    ("- 掃描動畫 bundle：**%d** 個（base %d / mods %d）。"):format(#src, #base_src, #mod_src),
    ("- AnimationClip 總數：**%s**（base %s / mods %s）。"):format(commas(total_clips), commas(base_clips), commas(mod_clips)),
    ("- AnimatorController 總數：**%s**。"):format(commas(total_ctrl)),
  })

  -- duration stats
  local durs = {}
  for _, r in ipairs(clips) do
    if #r > 3 and r[4] ~= "" then
      local v = tonumber(pystrip(r[4]))
      if v == nil then error("ValueError: could not convert string to float: '" .. r[4] .. "'", 0) end
      durs[#durs + 1] = v + 0.0
    end
  end
  if #durs > 0 then
    local lo, hi, sum = durs[1], durs[1], 0.0
    for _, v in ipairs(durs) do if v < lo then lo = v end; if v > hi then hi = v end; sum = sum + v end
    md[#md + 1] = ("- clip 長度：最短 %.3fs，最長 %.3fs，"):format(lo, hi) .. ("平均 %.3fs（%s 筆可計算長度）。"):format(sum / #durs, commas(#durs))
  end
  local names = counter()
  for _, r in ipairs(clips) do if #r > 2 then bump(names, r[3]) end end
  local order = {}
  for i, k in ipairs(names.keys) do order[i] = { k = k, i = i } end
  table.sort(order, function(a, b)
    local ca, cb = names.values[a.k], names.values[b.k]
    if ca ~= cb then return ca > cb end
    return a.i < b.i
  end)
  local common = {}
  for i = 1, math.min(5, #order) do common[i] = order[i].k .. "(×" .. names.values[order[i].k] .. ")" end
  md[#md + 1] = ("- clip 名稱去重後 **%s** 個；"):format(commas(#names.keys)) .. "跨 bundle 撞名最多的：" .. table.concat(common, ", ") .. "。"
  add(md, {
    "",
    "> 本文件與 TSV 是**資源清單／中繼資料**（clip 名稱、長度、曲線數、Studio 目錄、嘴型配對），" ..
    "不含逐格烘焙的骨骼曲線；逐格取樣由 `Tools/ok-extract/src/anim.rs` 於執行期完成。",
    "",
  })

  -- ---------------------------------------------------------- animation tab
  add(md, {
    "## 1. 動畫資料",
    "",
    "### 1.1 動畫存放位置與格式",
    "",
    "角色的動作是 Unity **`AnimationClip`**，內含三種曲線來源：",
    "",
    "| 區塊 | 說明 |",
    "|---|---|",
    "| `m_StreamedClip` | 稀疏關鍵影格；每個 key 有 `coeff[4]`（三次多項式 / 斜率 / 值） |",
    "| `m_DenseClip` | 取樣後每格曲線值；`m_CurveCount × m_FrameCount` |",
    "| `m_ConstantClip` | 固定值曲線 |",
    "",
    "`m_ClipBindingConstant.genericBindings` 把曲線索引對到 Avatar 的 `m_TOS` " ..
    "骨名 hash。時間由 `m_MuscleClip.m_StartTime` / `m_StopTime` 決定；" ..
    "`m_DenseClip` 另有自己的 `m_SampleRate` / `m_BeginTime`（末端可能有 padding）。",
    "",
    "本專案可載入的動畫來源（`Tools/ok-extract/src/anim.rs` 的 `clip_sources` 與 " ..
    "`AssetBundleNames.cs`）：",
    "",
    "| 目錄 | 用途 |",
    "|---|---|",
    "| `abdata/custom/custompose.unity3d` | Maker 姿勢（`custom_pose` AnimatorController） |",
    "| `abdata/action/animator/00.unity3d` | 校園／日常動作 |",
    "| `abdata/studio/anime/*.unity3d` | Studio 角色動作（單檔數千 clip） |",
    "| `abdata/studio_k/anime/*.unity3d` | Studio KKS 追加動作 |",
    "| `abdata/h/anim/{female,male,parts}` | H 動作（男女分開） |",
    "| `abdata/esthetic/animator/00.unity3d` | 按摩動作 |",
    "| `abdata/adv/motion/controller/adv/00.unity3d` | ADV 劇情動作 |",
    "| `abdata/twoshot/*` | 雙人動作 |",
    "",
    "### 1.2 動畫來源清單",
    "",
  })
  local rows = {}
  for _, r in ipairs(util.sorted(src, function(x) return { x[1], x[2] } end)) do
    rows[#rows + 1] = { r[1], r[2], commas(num(r[3])), #r > 3 and r[4] or "", #r > 4 and r[5] or "" }
  end
  add(md, table_lines({ "origin", "source", "clips", "controllers", "override controllers" }, rows))
  add(md, { "" })

  -- per-origin summary
  add(md, { "### 1.3 每個來源的 clip 數分布", "" })
  local by_origin = counter()
  for _, r in ipairs(src) do bump(by_origin, r[1], num(r[3])) end
  local origins = util.sorted(by_origin.keys)
  rows = {}
  for _, k in ipairs(origins) do
    local n = 0
    for _, r in ipairs(src) do if r[1] == k then n = n + 1 end end
    rows[#rows + 1] = { k, commas(by_origin.values[k]), tostring(n) }
  end
  add(md, table_lines({ "origin", "clip 總數", "bundle 數" }, rows))
  add(md, { "", "各 bundle 的 clip 數（由多到少，前 40 筆）：", "" })
  local top = util.slice(util.sorted(src, function(r) return -num(r[3]) end), 1, 40)
  rows = {}
  for _, r in ipairs(top) do rows[#rows + 1] = { r[1], r[2], commas(num(r[3])) } end
  add(md, table_lines({ "origin", "source", "clips" }, rows))
  add(md, { "" })

  -- ---------------------------------------------------------------- catalog
  local cat = read_tsv("studio_catalog.tsv")
  local function kind(k) return util.filter(cat, function(r) return #r > 0 and r[1] == k end) end
  local groups, cats, anime, hanime, hand = kind("group"), kind("category"), kind("anime"), kind("hanime"), kind("handanime")
  -- columns: kind origin asset id group category name group_name category_name ...
  local gname = {}
  for _, r in ipairs(groups) do if gname[r[4]] == nil then gname[r[4]] = { r[5], r[2] } end end

  add(md, {
    "## 2. Studio 動畫目錄（`abdata/studio/info/*.unity3d` + mods CSV）",
    "",
    "Studio 的角色動作目錄由 `studio/info/*.unity3d` 的 `ExcelData` 提供，" ..
    "mod 則以 `abdata/studio/info/**/*.csv` 追加（Sideloader 轉成 ExcelData）。" ..
    "對應 `Studio/Info.cs` 的 `LoadAnimeLoadInfo` / `LoadHAnimeLoadInfo`。",
    "",
    ("目錄筆數：group %s、category %s、"):format(commas(#groups), commas(#cats)) ..
    ("Anime %s、HAnime %s、HandAnime %s。"):format(commas(#anime), commas(#hanime), commas(#hand)),
    "",
    "### 2.1 動作群組（group）",
    "",
  })
  local uniq, uniq_list = {}, {}
  for _, r in ipairs(groups) do
    local k = r[4] .. "\0" .. r[5] .. "\0" .. r[2]
    if not uniq[k] then uniq[k] = true; uniq_list[#uniq_list + 1] = { r[4], r[5], r[2] } end
  end
  add(md, table_lines({ "group", "名稱", "origin" }, util.sorted(uniq_list, function(r) return num(r[1]) end)))
  add(md, { "", "### 2.2 每個群組的動作數量", "",
    "| group | 群組名稱 | categories | Anime | HAnime |",
    "|---|---|---|---|---|" })
  local per, per_keys = {}, {}
  local function per_of(g)
    if not per[g] then per[g] = { set = {}, count = 0, a = 0, h = 0 }; per_keys[#per_keys + 1] = g end
    return per[g]
  end
  for _, r in ipairs(cats) do
    local p = per_of(r[5])
    if not p.set[r[4]] then p.set[r[4]] = true; p.count = p.count + 1 end
  end
  for _, r in ipairs(anime) do local p = per_of(r[5]); p.a = p.a + 1 end
  for _, r in ipairs(hanime) do local p = per_of(r[5]); p.h = p.h + 1 end
  for _, g in ipairs(util.sorted(per_keys, num)) do
    local label = (gname[g] or { "", "" })[1]
    md[#md + 1] = ("| %s | %s | %d | %d | %d |"):format(g, cell(label), per[g].count, per[g].a, per[g].h)
  end
  add(md, { "", "### 2.3 HAnime 欄位意義", "",
    "`studio_catalog.tsv` 的 HAnime 欄位對應 `Studio/Info.cs` 的 `HAnimeLoadInfo`：",
    "" })
  add(md, table_lines({ "欄位", "意義" },
    { { "group / category / id", "群組、中間分類、管理番號" },
      { "name", "顯示名" },
      { "bundle / file", "基底動作 bundle 與 asset（AnimatorController）" },
      { "override_bundle / override_file", "覆寫動作（實際 clip 所在的 override controller）" },
      { "clip", "Animator state 名（不是 AnimationClip 名）" },
      { "options", "BreastLayer、dynamic、isMotion、pv[4]、yure bundle/file" } }))
  add(md, { "",
    "### 2.4 手部動作（HandAnime）",
    "" })
  rows = {}
  for _, r in ipairs(util.sorted(hand, function(r) return num(r[4]) end)) do rows[#rows + 1] = { r[4], r[7], r[10], r[11], r[14] } end
  add(md, table_lines({ "id", "名稱", "bundle", "file", "clip" }, rows))
  add(md, { "",
    "完整 Anime／HAnime 條目見 `Saved/anim_export/studio_catalog.tsv`。",
    "" })

  -- -------------------------------------------------------------------- FBS
  local pats = read_tsv("fbs_patterns.tsv")
  local chans = read_tsv("fbs_channels.tsv")
  -- columns: origin asset prefab controller target_index target_go mesh pattern close close_name open open_name
  add(md, {
    "## 3. 嘴型與表情資料（FaceBlendShape）",
    "",
    "### 3.1 機制",
    "",
    "頭部 prefab 上的 `FaceBlendShape` MonoBehaviour 有四個控制器 " ..
    "`BlinkCtrl` / `EyebrowCtrl` / `EyesCtrl` / `MouthCtrl`（皆繼承 `FBSBase`）。" ..
    "每個控制器的 `FBSTarget` 指向一個 `SkinnedMeshRenderer`（臉、鼻線、牙齒、犬齒、舌頭等），" ..
    "其 `PtnSet` 是一串 `Close` / `Open` blendshape 索引配對。",
    "",
    "執行期 `FBSBase.CalculateBlendShape` 依「開合率」在 `Close`（權重 1-開）與 " ..
    "`Open`（權重 開）之間內插：",
    "",
    "```",
    "weight(Close) = 1 - open",
    "weight(Open)  = open",
    "```",
    "",
    "- `ChaControl.ChangeMouthPtn(ptn)` → `FBSCtrlMouth.ChangePtn` 選第幾個 PtnSet。",
    "- `ChaControl.ChangeMouthOpenMax(v)` / `ChangeMouthFixed` 設定開合上限與固定開度。",
    "- 嘴型 21 / 22 會啟用舌頭狀態與寬度縮放（`UseAdjustWidthScale`）。",
    "- SexFaces 等插件會在既有 `PtnSet` 後追加 morph（base 43 組 → 54 組），" ..
    "因此本專案以「名稱」而非固定索引查找（見 `Docs/ERROR_LOG.md` E-073 / E-076）。",
    "",
    "**Lip-sync**：`ChaControl.UpdateBlendShapeVoice()` 每幀算 `voiceValue`：" ..
    "有預先烘焙的 `WavInfoData`（`WavInfoControl.LipDataCreate`，RMS 序列、`soundinfo.bytes`）就用時間取樣，" ..
    "否則用 `AudioAssist.GetAudioWaveValue(AudioSource)` 取 `GetOutputData` 的 RMS，" ..
    "平滑後交給 `FBSCtrlMouth.CalcBlend(openValue)`；`ChangeMouthOpenMax` 是開合上限。",
    "",
    "### 3.2 FBS 控制器總覽",
    "",
  })
  -- summary per asset/controller/target
  local summary = counter()
  local summary_go = {}
  for _, r in ipairs(pats) do
    local key = { r[1], r[2], r[3], r[4], r[5], r[7] }
    bump(summary, key, 1, key)
    summary_go[table.concat(key, "\0")] = r[6]
  end
  local items = util.sorted(summary.keys, function(k) local t = summary.raw[k]; return { t[2], t[3], t[4], num(t[5]) } end)
  local srows = {}
  for _, k in ipairs(items) do
    local t = summary.raw[k]
    srows[#srows + 1] = { t[1], t[2], t[3], t[4], t[5], summary_go[k], t[6], tostring(summary.values[k]) }
  end
  add(md, table_lines({ "origin", "asset", "prefab", "controller", "target", "target_go", "mesh", "patterns" }, srows))
  add(md, { "" })

  -- full tables for the canonical base head, dedup identical prefabs
  local prefab_set = {}
  for _, r in ipairs(pats) do if r[1] == "base" then prefab_set[r[3]] = true end end
  local base_prefabs = util.sorted(util.keys(prefab_set))
  local base_pats = util.filter(pats, function(r) return r[1] == "base" and r[3] == "p_cf_head_00" end)
  local others = {}
  for _, p in ipairs(base_prefabs) do if p ~= "p_cf_head_00" then others[#others + 1] = "`" .. p .. "`" end end
  add(md, { "### 3.3 基礎頭部（`abdata/chara/bo_head_00.unity3d`，prefab `p_cf_head_00`）嘴型全表",
    "",
    "`bo_head_00` 內另有 " .. table.concat(others, ", ") .. " " ..
    "等 prefab，PtnSet 結構相同（見 `fbs_patterns.tsv`）。",
    "" })
  local seen, by_ctrl = {}, {}
  for _, r in ipairs(base_pats) do
    local key = table.concat({ r[4], r[5], r[7], r[8], r[9], r[11] }, "\0")
    if not seen[key] then
      seen[key] = true
      by_ctrl[r[4]] = by_ctrl[r[4]] or {}
      table.insert(by_ctrl[r[4]], r)
    end
  end
  for _, ctrl in ipairs({ "MouthCtrl", "EyesCtrl", "EyebrowCtrl", "BlinkCtrl" }) do
    local crow = by_ctrl[ctrl]
    if crow and #crow > 0 then
      add(md, { "#### " .. ctrl, "",
        "| target | mesh | ptn | close | close_name | open | open_name |",
        "|---|---|---|---|---|---|---|" })
      for _, r in ipairs(util.sorted(crow, function(r) return { num(r[5]), num(r[8]) } end)) do
        md[#md + 1] = ("| %s | %s | %s | %s | %s | "):format(r[5], cell(r[7]), r[8], r[9], cell(r[10])) .. ("%s | %s |"):format(r[11], cell(r[12]))
      end
      add(md, { "" })
    end
  end

  -- mod head summary
  local mh_set, mh_list = {}, {}
  for _, r in ipairs(pats) do
    if r[1] == "mod" then
      local k = r[2] .. "\0" .. r[3]
      if not mh_set[k] then mh_set[k] = true; mh_list[#mh_list + 1] = { r[2], r[3] } end
    end
  end
  local mod_heads = util.sorted(mh_list)
  if #mod_heads > 0 then
    add(md, { "### 3.4 mod 頭部 prefab 的嘴型", "",
      "mod 覆寫或新增的頭部 prefab（完整內容見 `fbs_patterns.tsv`）：", "" })
    local mh = {}
    for _, ap in ipairs(mod_heads) do
      local cset = {}
      for _, r in ipairs(pats) do if r[2] == ap[1] and r[3] == ap[2] then cset[r[4]] = true end end
      mh[#mh + 1] = { ap[1], ap[2], table.concat(util.sorted(util.keys(cset)), ", ") }
    end
    add(md, table_lines({ "asset", "prefab", "controllers" }, mh))
    add(md, { "" })
  end

  -- channels summary
  local chan_by_mesh = counter()
  for _, r in ipairs(chans) do bump(chan_by_mesh, { r[2], r[7] }, 1, { r[2], r[7] }) end
  add(md, { "### 3.5 blendshape channel 一覽",
    "",
    "| asset | mesh | channels |",
    "|---|---|---|" })
  for _, k in ipairs(util.sorted(chan_by_mesh.keys, function(k) return chan_by_mesh.raw[k] end)) do
    local t = chan_by_mesh.raw[k]
    md[#md + 1] = ("| %s | %s | %d |"):format(cell(t[1]), cell(t[2]), chan_by_mesh.values[k])
  end
  add(md, { "",
    "完整 channel 名稱見 `fbs_channels.tsv`。",
    "" })

  -- --------------------------------------------------------------- file list
  add(md, {
    "## 4. 輸出檔案",
    "",
    "| 檔案 | 內容 |",
    "|---|---|",
    "| `Saved/anim_export/animation_sources.tsv` | 每個動畫 bundle 的 clip / controller 數量 |",
    "| `Saved/anim_export/animation_clips.tsv` | **所有** AnimationClip（長度、取樣率、曲線數、binding 數） |",
    "| `Saved/anim_export/studio_catalog.tsv` | Studio Anime／HAnime／HandAnime／group／category 目錄 |",
    "| `Saved/anim_export/fbs_patterns.tsv` | 所有 FBS 控制器的 PtnSet（close/open 索引與名稱） |",
    "| `Saved/anim_export/fbs_channels.tsv` | 每個 FBS 目標 mesh 的 blendshape channel 名稱 |",
    "| `Saved/anim_export/mod_anim_bundles.tsv` | mods 內含 AnimationClip 的 bundle 清單 |",
    "| `Saved/anim_export/mod_studio_info.tsv` | mods 內 `studio/info` 目錄 CSV／bundle 清單 |",
    "| `Saved/anim_export/mod_head_prefabs.tsv` | mods 內的 `chara/bo_head_*.unity3d` |",
    "| `Tools/anim_export/scan_mods.lua` | 掃描 mods（動畫 bundle／studio/info／頭部 prefab） |",
    "| `Tools/anim_export/export_anim_mouth.lua` | 產生全部 TSV |",
    "| `Tools/anim_export/make_md.lua` | 產生本文件 |",
    "",
    "## 5. 重現指令",
    "",
    "```powershell",
    "Tools\\lua\\lua.exe Tools/anim_export/scan_mods.lua            # 掃描 mods（只需重跑一次）",
    "Tools\\lua\\lua.exe Tools/anim_export/export_anim_mouth.lua    # 產生全部 TSV",
    "Tools\\lua\\lua.exe Tools/anim_export/make_md.lua              # 產生本文件",
    "```",
    "",
  })

  path.mkdir(path.parent(MD))
  path.write_bytes(MD, table.concat(md, "\n"))
  print(("ok-extract: wrote %s (%d lines)"):format(MD, #md))
  return 0
end

ok.main(main)
