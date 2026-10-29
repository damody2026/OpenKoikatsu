-- Condense HS2Studio6 UI layout dumps into a layout spec for the OK Studio UI work (Docs/OK_STUDIO_UI_WORKFLOW_PLAN.md).
--
--   Tools\lua\lua.exe Tools\studio_layout_spec.lua [--dumps Saved/StudioUIRef/layout] [--out Docs/StudioUI/hs2studio6_layout_spec.json]
--
-- The dumps come from the `layout <name>` command of the local capture helper in ref/HS2Studio6 (plan section 11):
-- every active RectTransform with its rect in canvas reference units (top-left origin), kind, text, font size and fill.
-- Each step lists only what appeared since the step before it (the window or page that step opened); the shell is the
-- empty studio. Theme colours and font sizes are counted over all steps.
local ok = require "ok"
local json, path = ok.json, ok.path

-- id, base step, how the step was opened (menu path as shown in the studio)
local STEPS = {
  { "00_empty", nil, "啟動後的空場景" },
  { "01_add_menu", "00_empty", "add" },
  { "02_kk_loader", "01_add_menu", "add → KK角色（選一張卡預覽）" },
  { "03_character_loaded", "01_add_menu", "KK角色 → 读取，關閉載入器" },
  { "04_item_catalog", "03_character_loaded", "add → 项目" },
  { "05_light_add", "04_item_catalog", "add → 光照" },
  { "06_map", "05_light_add", "add → 地图" },
  { "07_background", "06_map", "add → 背景" },
  { "08_frame", "07_background", "add → 前景框" },
  { "10_anim_menu", "08_frame", "anim" },
  { "11_state", "10_anim_menu", "anim → 状态" },
  { "12_bones_fk", "11_state", "anim → 骨骼（FK 頁）" },
  { "13_bones_ik", "12_bones_fk", "骨骼 → IK" },
  { "14_bones_look", "13_bones_ik", "骨骼 → 视线" },
  { "15_bones_neck", "14_bones_look", "骨骼 → 颈部" },
  { "16_bones_face", "15_bones_neck", "骨骼 → 表情" },
  { "17_bones_hand", "16_bones_face", "骨骼 → 手势" },
  { "18_bones_pose", "17_bones_hand", "骨骼 → 姿势" },
  { "19_bones_weight", "18_bones_pose", "骨骼 → 权重" },
  { "20_animation", "19_bones_weight", "anim → 动画" },
  { "21_voice", "20_animation", "anim → 语音" },
  { "22_clothes", "21_voice", "anim → 服装" },
  { "23_joint_fix", "22_clothes", "anim → 关节校正" },
  { "24_custom_char", "23_joint_fix", "anim → 定制角色" },
  { "25_custom_acc", "24_custom_char", "anim → 定制饰品" },
  { "26_hairshader", "25_custom_acc", "anim → HairShader" },
  { "30_sound_menu", "26_hairshader", "sound" },
  { "31_bgm", "30_sound_menu", "sound → 背景音乐" },
  { "32_sound_external", "31_bgm", "sound → 外部读取" },
  { "40_system_menu", "32_sound_external", "system" },
  { "41_scene_card", "40_system_menu", "system → 载入" },
  { "42_backup", "41_scene_card", "system → 备份恢复" },
  { "43_resource_check", "42_backup", "system → 资源检查" },
  { "44_settings", "43_resource_check", "system → 设置（F2）" },
  { "45_scene_card_choice", "44_settings", "载入 → 點選場景卡" },
  { "50_quick_search", "45_scene_card_choice", "左側「找」" },
  { "51_pose_library", "50_quick_search", "左側「姿」" },
  { "52_timeline", "51_pose_library", "工作區 Timeline" },
  { "53_camera", "52_timeline", "右上 camera" },
  { "54_material_editor", "53_camera", "工作區 Mat. Editor" },
}

local CONTROL = { Button = true, Toggle = true, Slider = true, InputField = true, Dropdown = true, ScrollRect = true, Text = true }
-- Lists repeat one row per entry: keep the first few rows of a parent and count the rest.
local ROWS_KEPT = 4

local function r1(x) return math.floor(x * 10 + 0.5) / 10 end

local function load_nodes(dir, id)
  local doc = ok.read_json(path.join(dir, id .. ".json"))
  local nodes, by_key, ref = {}, {}, nil
  for _, canvas in ipairs(doc.canvases) do
    ref = ref or canvas.reference
    for _, n in ipairs(canvas.nodes) do
      n.key = canvas.name .. "/" .. n.path
      nodes[#nodes + 1] = n
      by_key[n.key] = n
    end
  end
  return nodes, by_key, ref, doc.screen
end

local function parent_key(key) return key:match("^(.*)/[^/]+$") end

local function control_entry(n, root)
  local e = json.object({ { "kind", n.kind } })
  if n.text and n.text ~= "" then e.text = n.text end
  e.rect = json.array({ r1(n.rect[1] - root.rect[1]), r1(n.rect[2] - root.rect[2]), r1(n.rect[3]), r1(n.rect[4]) })
  if n.size then e.size = n.size end
  if n.fill then e.fill = n.fill end
  e.path = n.key:sub(#root.key + 2)
  return e
end

-- New nodes grouped under their topmost new ancestor; each group is a window, page or menu.
local function groups(nodes, base_keys)
  local new = {}
  for _, n in ipairs(nodes) do if not base_keys[n.key] then new[n.key] = n end end
  local roots, members = {}, {}
  for _, n in ipairs(nodes) do
    if new[n.key] then
      local k, top = n.key, n.key
      while true do
        local p = parent_key(k)
        if not p or not new[p] then break end
        top, k = p, p
      end
      if not members[top] then members[top] = {}; roots[#roots + 1] = new[top] end
      table.insert(members[top], n)
    end
  end
  local out = json.array({})
  for _, root in ipairs(roots) do
    local controls, per_parent, skipped = json.array({}), {}, 0
    for _, n in ipairs(members[root.key]) do
      if n ~= root and CONTROL[n.kind] then
        local p = parent_key(n.key)
        per_parent[p] = (per_parent[p] or 0) + 1
        if per_parent[p] <= ROWS_KEPT or n.kind ~= "Button" and n.kind ~= "Toggle" and n.kind ~= "Text" then
          controls[#controls + 1] = control_entry(n, root)
        else
          skipped = skipped + 1
        end
      end
    end
    local g = json.object({ { "root", root.key }, { "rect", json.array({ r1(root.rect[1]), r1(root.rect[2]), r1(root.rect[3]), r1(root.rect[4]) }) } })
    if root.fill then g.fill = root.fill end
    g.controls = controls
    if skipped > 0 then g.repeated_rows_omitted = skipped end
    -- Tiny groups (a highlight or a tooltip) carry little layout information; keep them but they sort last.
    g.weight = #members[root.key]
    out[#out + 1] = g
  end
  table.sort(out, function(a, b) return a.weight > b.weight end)
  return out
end

local function bump(t, k) if k then t[k] = (t[k] or 0) + 1 end end
local function top(t, n)
  local list = {}
  for k, v in pairs(t) do list[#list + 1] = { k, v } end
  table.sort(list, function(a, b) return a[2] > b[2] or a[2] == b[2] and tostring(a[1]) < tostring(b[1]) end)
  local out = json.array({})
  for i = 1, math.min(n, #list) do out[i] = json.object({ { "value", list[i][1] }, { "count", list[i][2] } }) end
  return out
end

-- --compare STEP: OK Studio's dump (OKStudio.UiLayout STEP, Saved/StudioUIRef/ok_layout) against HS2Studio6's dump of the
-- same step. OK shell parts (their tag) map to HS2Studio6 objects; right-anchored parts compare their distance from the
-- right edge and the transform bar its distance from the bottom, since the two canvases differ in size.
local SHELL = {
  RootToolbar = { "StudioReplicaCanvas/RootToolbar" },
  PoseLibraryButton = { "StudioReplicaCanvas/PoseLibraryButton" },
  QuickSearchButton = { "StudioReplicaCanvas/QuickSearchButton" },
  ["TopTools/TopTool_0"] = { "StudioReplicaCanvas/TopTool_Groups", "right" },
  ["TopTools/TopTool_2"] = { "StudioReplicaCanvas/TopTool_0", "right" },
  ["TopTools/TopTool_3"] = { "StudioReplicaCanvas/TopTool_1", "right" },
  ["TopTools/TopTool_4"] = { "StudioReplicaCanvas/TopTool_2", "right" },
  ["TopTools/TopTool_5"] = { "StudioReplicaCanvas/TopTool_3", "right" },
  ["TopTools/TopTool_6"] = { "StudioReplicaCanvas/TopTool_4", "right" },
  ["TopTools/TopTool_7"] = { "StudioReplicaCanvas/TopTool_5", "right" },
  CharacterGroupPanel = { "StudioReplicaCanvas/CharacterGroupPanel", "right" },
  TreePanel = { "StudioReplicaCanvas/TreePanel", "right" },
  TransformBar = { "StudioReplicaCanvas/TransformBar", "bottom" },
}
local STEP_WINDOWS = {
  ["01_add_menu"] = { SubMenu = "SubCanvas/AddPanel" },
  ["10_anim_menu"] = { SubMenu = "SubCanvas/AnimationMenu" },
  ["40_system_menu"] = { SubMenu = "SubCanvas/SystemMenu" },
  ["04_item_catalog"] = { DetailWindow = "SubCanvas/StudioItemCatalog" },
  ["05_light_add"] = { DetailWindow = "SubCanvas/StudioLightCatalog" },
  ["11_state"] = { DetailWindow = "StudioReplicaCanvas/CharacterStatePanel" },
  ["12_bones_fk"] = { DetailWindow = "StudioReplicaCanvas/CharacterKinematicPanel" },
  ["20_animation"] = { DetailWindow = "SubCanvas/CharacterAnimePanel" },
  ["53_camera"] = { DetailWindow = "StudioReplicaCanvas/ManipulatePanel" },
  ["52_timeline"] = { TimelinePanel = "SubCanvas/TimelinePanel" },
}

local function compare(step, tolerance)
  local function nodes_of(file)
    local doc = ok.read_json(file)
    local by, size = {}, nil
    for _, c in ipairs(doc.canvases) do
      for _, n in ipairs(c.nodes) do
        by[(c.name == "OKStudio" and "" or c.name .. "/") .. n.path] = n
        if n.path == "" and n.rect[3] > 100 then size = size or { n.rect[3], n.rect[4] } end
      end
      if c.name == "OKStudio" then size = { doc.screen[1] / c.scale, doc.screen[2] / c.scale } end
    end
    return by, size
  end
  local okf = path.join(ok.root, "Saved/StudioUIRef/ok_layout", step .. ".json")
  local hsf = path.join(ok.root, "Saved/StudioUIRef/layout", step .. ".json")
  if not path.is_file(okf) or not path.is_file(hsf) then error("missing dump: " .. (path.is_file(okf) and hsf or okf), 0) end
  local okn, oks = nodes_of(okf)
  local hsn, hss = nodes_of(hsf)
  hss = hss or { 1768.9, 814.1 }
  local pairs_ = {}
  for tag, v in pairs(SHELL) do pairs_[#pairs_ + 1] = { tag, v[1], v[2] } end
  for tag, hs in pairs(STEP_WINDOWS[step] or {}) do pairs_[#pairs_ + 1] = { tag, hs } end
  table.sort(pairs_, function(a, b) return a[1] < b[1] end)
  local lines, worst, failed = {}, 0, 0
  local function norm(r, size, anchor)
    if anchor == "right" then return { size[1] - r[1], r[2], r[3], r[4] } end
    if anchor == "bottom" then return { r[1], size[2] - r[2], r[3], r[4] } end
    return r
  end
  for _, p in ipairs(pairs_) do
    local a, b = okn[p[1]], hsn[p[2]]
    if a and b then
      local ra, rb = norm(a.rect, oks, p[3]), norm(b.rect, hss, p[3])
      local d = 0
      for k = 1, 4 do d = math.max(d, math.abs(ra[k] - rb[k])) end
      worst = math.max(worst, d)
      if d > tolerance then failed = failed + 1 end
      lines[#lines + 1] = string.format("%-4s %-22s ok[%7.1f %6.1f %6.1f %6.1f]  hs[%7.1f %6.1f %6.1f %6.1f]  max %.1f%s",
        d <= tolerance and "PASS" or "FAIL", p[1], ra[1], ra[2], ra[3], ra[4], rb[1], rb[2], rb[3], rb[4], d, p[3] and (" (" .. p[3] .. ")") or "")
    elseif a or b then
      lines[#lines + 1] = string.format("SKIP %-22s %s", p[1], a and "HS2Studio6 has no such window open" or "OK Studio does not show it")
    end
  end
  lines[#lines + 1] = string.format("%s: %d over ±%g, worst %.1f", step, failed, tolerance, worst)
  local report = table.concat(lines, "\n") .. "\n"
  local out = path.join(ok.root, "Saved/StudioUIRef/compare", "layout_" .. step .. ".txt")
  path.mkdir(path.parent(out), true)
  path.write_text(out, report)
  io.write(report)
  return failed
end

-- --bounds STEP: every widget of OK Studio's dump STEP lies inside the window (nothing cut off at a small window size).
-- Report in Saved/StudioUIRef/compare/bounds_STEP.txt.
local function bounds(step)
  local file = path.join(ok.root, "Saved/StudioUIRef/ok_layout", step .. ".json")
  if not path.is_file(file) then error("missing dump: " .. file, 0) end
  local doc = ok.read_json(file)
  local lines, out = {}, 0
  for _, c in ipairs(doc.canvases) do
    local w, h = doc.screen[1] / c.scale, doc.screen[2] / c.scale
    for _, n in ipairs(c.nodes) do
      local r = n.rect
      if r[1] < -0.5 or r[2] < -0.5 or r[1] + r[3] > w + 0.5 or r[2] + r[4] > h + 0.5 then
        out = out + 1
        lines[#lines + 1] = string.format("OUT  %-60s [%7.1f %6.1f %6.1f %6.1f] window %.1f x %.1f", n.path, r[1], r[2], r[3], r[4], w, h)
      end
    end
    lines[#lines + 1] = string.format("%s: %d of %d widgets outside the %d x %d window", step, out, #c.nodes, doc.screen[1], doc.screen[2])
  end
  local report = table.concat(lines, "\n") .. "\n"
  local dest = path.join(ok.root, "Saved/StudioUIRef/compare", "bounds_" .. step .. ".txt")
  path.mkdir(path.parent(dest), true)
  path.write_text(dest, report)
  io.write(report)
  return out
end

local function main()
  local o = { dumps = "Saved/StudioUIRef/layout", out = "Docs/StudioUI/hs2studio6_layout_spec.json" }
  local i = 1
  while i <= #arg do
    local k = arg[i]:match("^%-%-?(%w+)$")
    if k == "compare" and arg[i + 1] then
      local failed = 0
      for step in arg[i + 1]:gmatch("[^,]+") do failed = failed + compare(step, tonumber(arg[i + 2]) or 4) end
      return
    end
    if k == "bounds" and arg[i + 1] then
      for step in arg[i + 1]:gmatch("[^,]+") do bounds(step) end
      return
    end
    if not k or o[k] == nil or not arg[i + 1] then error("usage: studio_layout_spec.lua [--dumps DIR] [--out FILE] | --compare STEP[,STEP...] [TOLERANCE] | --bounds STEP[,STEP...]", 0) end
    o[k] = arg[i + 1]; i = i + 2
  end
  local dir = path.abs(path.join(ok.root, o.dumps))
  local fills, texts, sizes, kinds = {}, {}, {}, {}
  local steps, cache, reference, screen = json.array({}), {}, nil, nil
  for _, s in ipairs(STEPS) do
    local id, base, opened = s[1], s[2], s[3]
    if path.is_file(path.join(dir, id .. ".json")) then
      local nodes, by_key, ref, scr = load_nodes(dir, id)
      cache[id] = by_key; reference = reference or ref; screen = screen or scr
      for _, n in ipairs(nodes) do
        bump(fills, n.fill); bump(sizes, n.size); bump(kinds, n.kind)
        if n.text then bump(texts, n.color) end
      end
      local base_keys = base and cache[base] or {}
      steps[#steps + 1] = json.object({ { "id", id }, { "opened_by", opened }, { "base", base or json.null },
        { "screenshot", "Saved/StudioUIRef/hs2/L_" .. id .. ".png" }, { "groups", groups(nodes, base_keys) } })
    else
      io.write("missing dump: ", id, "\n")
    end
  end
  local spec = json.object({
    { "source", "ref/HS2Studio6 zzzstudio.unity (Play), dumped 2026-10-29 with the local capture helper" },
    { "units", "canvas reference units, origin top-left; control rects are relative to their group root" },
    { "reference", reference }, { "screen", screen },
    { "theme", json.object({ { "fills", top(fills, 16) }, { "text_colors", top(texts, 8) }, { "font_sizes", top(sizes, 10) }, { "kinds", top(kinds, 12) } }) },
    { "steps", steps },
  })
  local out = path.abs(path.join(ok.root, o.out))
  path.mkdir(path.parent(out), true)
  ok.write_json(out, spec, { indent = 1, ensure_ascii = false })
  io.write("steps ", #steps, " -> ", out, "\n")
end

ok.main(main)
