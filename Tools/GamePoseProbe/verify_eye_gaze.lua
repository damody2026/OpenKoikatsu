-- Stage284 actual selected-head metadata, native formula/UV and UE display gates.
local ok = require "ok"
local G = require "gpp"
local json, util = ok.json, ok.util
local O, F, I, check, findall = G.O, G.F, G.I, G.check, G.findall
local EC = require "verify_eye_clamp"
local ROOT, LOGS, CARDS, read, verify = EC.ROOT, EC.LOGS, EC.CARDS, EC.read, EC.verify
local source_settings = require("verify_face_time").source_settings
local FOLDER = require("verify_head_fbs").FOLDER

local M = {}

-- UnityPy env.objects equivalent: every object (no tree) plus lazily read trees.
-- A missing path loads as an empty environment, like UnityPy.load(str).
local function load_env(file)
  local all = ok.path.exists(file) and ok.kit.unity_objects(file, { tree = false }) or {}
  local env = { file = file, objects = all, by_id = {}, trees = {} }
  for _, o in ipairs(all) do env.by_id[o.path_id] = o end
  function env:trees_of(t)
    if not self.trees[t] then
      local m = {}
      if #self.objects > 0 then
        for _, o in ipairs(ok.kit.unity_objects(self.file, { type = t })) do m[o.path_id] = o.tree end
      end
      self.trees[t] = m
    end
    return self.trees[t]
  end
  function env:tree(o) return self:trees_of(o.type)[o.path_id] end
  return env
end

function M.sources()
  local heads = source_settings()
  for _, card in ipairs(json.keys(heads)) do
    local head = heads[card]
    local stem = G.ascii_stem(card .. ".png", 48)
    local env = load_env(FOLDER .. "/" .. stem .. "_selected_head.unity3d")
    local gos = {}
    for _, o in ipairs(env.objects) do if o.type == "GameObject" then gos[o.path_id] = o.name end end
    local renderers = {}
    for _, o in ipairs(env.objects) do
      if o.type == "SkinnedMeshRenderer" then renderers[env:tree(o).m_GameObject.m_PathID] = o end
    end
    local controllers = json.array({})
    for _, obj in ipairs(env.objects) do
      if obj.type == "MonoBehaviour" then
        local data = env:tree(obj)
        if data.EyesCtrl ~= nil and gos[data.m_GameObject.m_PathID] == head.prefab then
          for _, key in ipairs({ "EyeLookUpCorrect", "EyeLookDownCorrect", "EyeLookSideCorrect" }) do
            head.settings[#head.settings + 1] = data[key]
          end
          head.settings[#head.settings + 1] = 1.0
        end
        if data.InsideWait ~= nil and data.texStates ~= nil then
          local renderer_id = data._renderer.m_PathID
          local renderer = renderer_id ~= 0 and env.by_id[renderer_id] or renderers[data.m_GameObject.m_PathID]
          if renderer == nil then error("KeyError: " .. tostring(renderer_id)) end
          local renderer_data = env:tree(renderer)
          local mesh = env.by_id[renderer_data.m_Mesh.m_PathID]
          local settings = json.array({})
          for _, k in ipairs({ "eyeLR", "InsideWait", "OutsideWait", "UpWait", "DownWait",
            "InsideLimit", "OutsideLimit", "UpLimit", "DownLimit", "power" }) do settings[#settings + 1] = data[k] end
          controllers[#controllers + 1] = O("renderer", gos[renderer_data.m_GameObject.m_PathID], "mesh", mesh.name,
            "settings", settings, "fallback", renderer_id == 0)
        end
      end
    end
    check(#head.settings == 12 and #controllers == 2, { card, head })
    head.eye_materials = controllers
  end
  local rig = ok.game_root(true) .. "/abdata/studio/base/00.unity3d"
  local env = load_env(rig)
  local gos = {}
  for _, o in ipairs(env.objects) do if o.type == "GameObject" then gos[o.path_id] = o.name end end
  local nodes = env:trees_of("Transform")
  local by_go = {}
  for _, o in ipairs(env.objects) do
    if o.type == "Transform" then by_go[nodes[o.path_id].m_GameObject.m_PathID] = o.path_id end
  end
  local found = {}
  for _, obj in ipairs(env.objects) do
    if obj.type == "MonoBehaviour" then
      local data = env:tree(obj)
      if data.eyeLookScript ~= nil then
        local id = by_go[data.m_GameObject.m_PathID]
        if id == nil then error("KeyError: " .. tostring(data.m_GameObject.m_PathID)) end
        local p = {}
        while id ~= 0 do
          local node = nodes[id]
          p[#p + 1] = gos[node.m_GameObject.m_PathID]
          id = node.m_Father.m_PathID
        end
        if G.contains(p, "p_cf_head_bone") then found[#found + 1] = p end
      end
    end
  end
  check(#found == 1, found)
  return heads, O("path", G.winpath(rig), "sha256", G.digest(rig), "controller_path", found[1])
end

function M.contact()
  local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
  for row, card in ipairs(CARDS) do
    local y = (row - 1) * 384
    sheet:text(8, y + 6, card .. " : gaze / full body / face / side")
    for col, view in ipairs({ "3d", "face", "face_side" }) do
      sheet:paste(ROOT .. "/Saved/DebugShots/EyeGaze284_open/" .. card .. "_outfit0_" .. view .. ".png", (col - 1) * 300, y + 24, 300, 360)
    end
  end
  local p = ROOT .. "/Saved/Tests/EyeGaze284/ContactSheet.png"
  sheet:save(p)
  print(G.winpath(p))
end

local function rows_of(text)
  local rows = {}
  for _, line in ipairs(util.splitlines(text)) do rows[#rows + 1] = util.split(line, "\t") end
  return rows
end

local function main()
  local heads, rig = M.sources()
  local fixtures = json.object()
  for _, pair in ipairs({ { "GameMarchGaze284", CARDS[1] }, { "GameAliceGaze284", CARDS[2] } }) do
    local stem, card = pair[1], pair[2]
    local p = ROOT .. "/Saved/Tests/EyeGaze284/" .. stem .. ".json"
    check(json.decode(read(p))["error"] == "")
    local tbl = p .. ".eye-gaze.tsv"
    local rows = rows_of(read(tbl))
    local settings = {}
    local count = 0
    for _, r in ipairs(rows) do if r[1] == "setting" then if settings[I(r[2])] == nil then count = count + 1 end; settings[I(r[2])] = F(r[3]) end end
    check(count == 12)
    for i, value in ipairs(heads[card].settings) do
      if value ~= json.null then
        local s = settings[i - 1]
        if s == nil then error("KeyError: " .. (i - 1)) end
        check(math.abs(s - value) < 1e-6, { card, i - 1, value, s })
      end
    end
    for _, material in ipairs(heads[card].eye_materials) do
      local actual, n = {}, 0
      for _, r in ipairs(rows) do
        if r[1] == "material-setting" and r[2] == material.renderer then
          if actual[I(r[3])] == nil then n = n + 1 end
          actual[I(r[3])] = F(r[4])
        end
      end
      check(n == 14)
      for i, value in ipairs(material.settings) do
        if actual[i - 1] == nil then error("KeyError: " .. (i - 1)) end
        check(math.abs(actual[i - 1] - value) < 1e-6)
      end
    end
    local cases = G.filter(rows, function(r) return r[1] == "case" end)
    local formulas = G.filter(rows, function(r) return r[1] == "formula" end)
    check(#cases == 200 and #formulas == 200)
    check(G.set_eq(G.map(cases, function(r) return F(r[6]) end), { 0, .5, 1 }) and G.set_eq(G.map(cases, function(r) return F(r[7]) end), { -.1, .61337 }))
    check(G.set_eq(G.map(formulas, function(r) return F(r[17]) end), { -.2, 0, .8, 1.8 }))
    local center = G.filter(cases, function(r) return F(r[3]) == 0 and F(r[5]) == 0 end)
    if settings[8] == nil then error("KeyError: 8") end
    check(G.all(center, function(r) return math.abs(F(r[10]) - math.min(1 - settings[8], F(r[9]))) < 1e-6 end))
    check(G.mtime(tbl) <= G.mtime(LOGS .. "/EyeGazeUnrealAutomation284Console.log"))
    fixtures[stem] = O("cases", #cases, "formula_cases", #formulas,
      "channels", G.sum(rows, function(r) return r[1] == "weight" and 1 or 0 end),
      "raw_uvs", G.sum(rows, function(r) return r[1] == "raw-uv" and 1 or 0 end),
      "sha256", G.digest(tbl))
  end
  check(G.has(read(LOGS .. "/EyeGazeFormulaRust284.log"), "1 passed; 0 failed"))
  check(G.has(read(LOGS .. "/EyeGazeLegacyUvRust284.log"), "1 passed; 0 failed"))
  local result = verify({ prefix = "EyeGaze", stage = 284, pipeline = 134, build_log = "EyeGazeUnrealBuild284Final.log",
    extra_tests = { "NativeFaceControls", "NativeFaceTime", "NativeEyeGaze" }, unit_log = "EyeGazeCacheRust284.log",
    geometry_log = "EyeGazeGeometryRust284Final.log", shots_log = "EyeGazeShots284_open.log",
    shots_folder = "Saved/DebugShots/EyeGaze284_open", save_audit = false })
  local automation = read(LOGS .. "/EyeGazeUnrealAutomation284Console.log")
  local match = G.search([[KK native eye gaze cases=(\d+) settings=(\d+) pairs=(\d+) channel_comparisons=(\d+) uv_comparisons=(\d+)]], automation)
  check(match and I(match[1]) == 400 and I(match[2]) == 80 and I(match[4]) == G.sum(util.values(fixtures), function(f) return f.channels end))
  local shots = read(LOGS .. "/EyeGazeShots284_open.log")
  local audits = findall([[OK gaze display audit passed=(\d+) iris_parts=(\d+) changed_uvs=(\d+) vertices=(\d+) restore_position_cm=([\d.]+) restore_uv=([\d.]+) fixed_override=(\d+) invalid_rejected=(\d+) clock_unchanged=(\d+)]], shots)
  check(#audits == 5 and G.all(audits, function(r) return r[1] == "1" and r[2] == "2" and I(r[3]) > 0 and F(r[5]) == 0 and F(r[6]) == 0
    and r[7] == "1" and r[8] == "1" and r[9] == "1" end))
  check(G.count(shots, "OK gaze shot rates=(0.600000,-0.400000,-0.400000) source_controller=1") == 5)
  local rates = findall([[OK blink shot state=open open_rate=([\d.]+) eyes=([\d.]+) brow=([\d.]+)]], shots)
  check(#rates == 5)
  for i = 1, math.min(#CARDS, #rates) do
    local blink, eyes, brow = rates[i][1], rates[i][2], rates[i][3]
    local s = heads[CARDS[i]].settings
    local delta = -s[10] * (.4 ^ .5) - s[11] * (.6 ^ .5)
    delta = math.max(-math.max(s[10], s[11]), math.min(s[9], delta))
    local expected = math.min(1 - s[9], .92) + delta * .92
    check(math.abs(F(eyes) - expected) < 1e-6 and F(blink) == 1 and F(brow) == 1)
  end
  local gallery = read(LOGS .. "/EyeGazeGallery284.log")
  check(G.has(gallery, "KK gaze gallery verified rates=(0.6,-0.4,-0.4)"))
  check(G.has(gallery, "clock_unchanged=1") and G.has(gallery, "RequestExitWithStatus(0, 0,") and not G.has(gallery, "LogTemp: Error:"))
  check(G.size_is(ROOT .. "/Saved/DebugShots/EyeGaze284UI/gallery_imported_studio.png", 1440, 1000))
  local paths = {}
  for _, name in ipairs({ "lib.rs", "unity.rs", "eye_gaze.rs", "ffi.rs", "session.rs", "sqlcache.rs" }) do paths[#paths + 1] = ROOT .. "/Tools/ok-extract/src/" .. name end
  for _, p in ipairs({ "Tools/ok-extract/include/ok_extract.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKFaceAnimation.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKPreviewActor.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKFaceBlendShape.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKRustExtract.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKRustExtract.cpp",
    "Source/OpenKoikatsu/OKGalleryWidget.cpp", "Source/OpenKoikatsu/OKGalleryWidget.h" }) do paths[#paths + 1] = ROOT .. "/" .. p end
  local build_time = G.mtime(LOGS .. "/EyeGazeUnrealBuild284Final.log")
  check(G.all(paths, function(p) return G.mtime(p) <= build_time end))
  check(not G.exists(ok.game_root(true) .. "/BepInEx/plugins/OpenKoikatsu.GamePoseProbe.dll"))
  local hashes = json.object()
  for _, p in ipairs(paths) do hashes[G.rel(p)] = G.digest(p) end
  result.selected_head_sources = heads
  result.head_rig_source = rig
  result.native_gaze = fixtures
  result.native_unreal_counts = G.map(match:groups(), I)
  result.display_roundtrips = G.rows(audits)
  result.gallery_verified = true
  result.source_sha256 = hashes
  result.limitations = "Normalized source gaze rates, correctOpenMax and MainTex UV transforms are verified. World-target EyeLookCalc and eye bone rotations, full iris/highlight shader pixels, GPU-head display, tear-level controls and full Animator graphs remain separate. Chin work is deferred."
  G.write_json(LOGS .. "/EyeGaze284Audit.json", result)
  print(json.encode(result, { ensure_ascii = false }))
end

if ok.is_main() then
  ok.main(function()
    if ok.args.has(arg, "--sources") then
      local heads, rig = M.sources()
      print(json.encode({ heads, rig }, { ensure_ascii = false }))
    elseif ok.args.has(arg, "--contact") then M.contact()
    else main() end
  end)
end
return M
