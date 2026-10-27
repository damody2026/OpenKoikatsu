-- The hundred-girl room (CrowdRoom100.cmd): the StreetSocial runtime characters (same build, materials, GPU poses,
-- shadows and render settings as the street's heroines) wandering an empty room (Source/OpenKoikatsu/OKCrowdRoom.cpp).
-- Runs the newest StreetSocial stage with -CrowdRoom=<manifest>; nothing else of the street starts.
--   Tools\lua\lua.exe Tools\run_crowd_room100.lua [--count N] [--cards <dir>] [--benchmark <seconds>] [--warmup <seconds>]
--       [--max-fps N] [--window WxH] [--executable <exe>] [--variants N] [--exec "CMD,CMD"] [--cases <json>] [--trace] [--aggregate]
-- --aggregate: once everyone is loaded, compose every girl's pose bank into one GPU bank (one skinning provider instead
-- of a hundred; needs twice the banks' memory while composing).
-- --cases: a JSON array [{"name": "...", "exec": ["console command", ...]}, ...]: each case is applied in the same running
-- game and measured for --benchmark seconds in turn (an A/B without loading the girls again). --trace: an Unreal
-- Insights trace (cpu, frame, named events) of the first measurement, trace.utrace beside the report.
-- --benchmark: measure that many seconds once every girl is drawn and warmed up, write report.json (frame mean / P95,
-- game / render / GPU ms, the same numbers as the street's /fps) and a screenshot, then quit. Measure with only this
-- Unreal game running (t.MaxFPS 0 unless --max-fps).
local ok = require "ok"
local path, json, proc, time = ok.path, ok.json, ok.proc, require("ok.time")

local function say(s) io.write(s, "\n"); io.flush() end
local function read_json(p) return json.decode(path.read_text(p)) end
local function win(p) return (path.norm(p):gsub("/", "\\")) end

local function parse(argv)
  local o = { count = 100, benchmark = 0, warmup = 10, max_fps = 0, window = "1920x1080", variants = 3 }
  local i = 1
  while i <= #argv do
    local a = argv[i]:lower():gsub("^%-%-?", "")
    local v = argv[i + 1]
    if a == "count" then o.count = tonumber(v); i = i + 1
    elseif a == "cards" then o.cards = v; i = i + 1
    elseif a == "benchmark" then o.benchmark = tonumber(v); i = i + 1
    elseif a == "warmup" then o.warmup = tonumber(v); i = i + 1
    elseif a == "max-fps" then o.max_fps = tonumber(v); i = i + 1
    elseif a == "window" then o.window = v; i = i + 1
    elseif a == "executable" then o.executable = v; i = i + 1
    elseif a == "variants" then o.variants = tonumber(v); i = i + 1
    elseif a == "exec" then o.exec = v; i = i + 1
    elseif a == "cases" then o.cases = v; i = i + 1
    elseif a == "trace" then o.trace = true
    elseif a == "aggregate" then o.aggregate = true
    else error("Unknown option " .. argv[i], 0) end
    i = i + 1
  end
  if not o.count or o.count < 1 or o.count > 100 then error("--count takes 1..100", 0) end
  return o
end

-- Newest StreetSocial stage (the street's own launcher picks the same one).
local function newest_stage(root)
  local dir, best, best_time = path.join(root, "Saved/StreetSocial/Build"), nil, -1
  if not path.is_dir(dir) then return nil end
  for _, n in ipairs(path.list(dir)) do
    if n:lower():match("%.stage$") and not n:lower():match("^crowdroom") then
      local exe = path.join(dir, n, "Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe")
      local t = path.mtime(exe)
      if t and t > best_time then best, best_time = exe, t end
    end
  end
  return best
end

-- Standing poses a girl takes while she stands about: the Studio standing set (立ち1-30), the waits (待機1-3),
-- looking out / at the sky / at a waterfall and the street's conversation waits; nothing that holds a prop.
local STAND_LIBRARY = {}
for k = 2, 30 do STAND_LIBRARY[#STAND_LIBRARY + 1] = ("library/lib_0_0_%d.okanim"):format(k) end
for _, id in ipairs({ "lib_0_4_0", "lib_0_4_1", "lib_0_4_2", "lib_0_4_13", "lib_17_4_0", "lib_17_4_2", "lib_17_4_5", "lib_17_4_11" }) do
  STAND_LIBRARY[#STAND_LIBRARY + 1] = "library/" .. id .. ".okanim"
end
local WAITS = { "conversation_wait_00", "conversation_wait_02", "conversation_wait_04", "conversation_wait_06", "conversation_wait_08",
  "conversation_wait_10", "conversation_wait_14", "conversation_wait_20", "conversation_wait_24", "conversation_wait_26", "conversation_reserved" }

local function main()
  local o = parse(arg)
  local root = ok.root
  local launch = read_json(path.join(root, "Content/IndependentResources/StreetSocialData/Launch/runtime.json"))
  local materials = read_json(path.join(root, launch.materials))
  local exe = o.executable or newest_stage(root)
  if not exe or not path.is_file(exe) then error("No StreetSocial stage under Saved/StreetSocial/Build (build one with Tools/rebuild_street_social_runtime.lua)", 0) end
  -- Cards: the crowd set first, then the street heroines. A card an earlier room could not draw (its materials are not
  -- among the shared runtime parents: "Source material coverage incomplete" in that run's log) is left out and the
  -- next one takes its place.
  local dirs = o.cards and { o.cards } or { path.join(root, "Content/CharacterCards/CrowdAPNG"), path.join(root, "Content/CharacterCards/StreetHeroines") }
  local unusable = {}
  local runs = path.join(root, "Saved/Crowd")
  if path.is_dir(runs) then
    for _, n in ipairs(path.list(runs)) do
      local log = path.join(runs, n, "game.log")
      if n:match("^Room100") and path.is_file(log) then
        for line in io.lines(log) do
          local card = line:match("LogTemp: Error: APNG (.-%.png): Source material coverage incomplete")
          if card then unusable[path.norm(card):lower()] = true end
        end
      end
    end
  end
  local cards, skipped = {}, 0
  for _, dir in ipairs(dirs) do
    local here = {}
    if path.is_dir(dir) then
      for _, n in ipairs(path.list(dir)) do
        if n:lower():match("%.png$") then here[#here + 1] = path.join(dir, n) end
      end
    end
    table.sort(here)
    for _, c in ipairs(here) do
      if unusable[path.norm(c):lower()] then skipped = skipped + 1
      elseif #cards < o.count then cards[#cards + 1] = c end
    end
  end
  if skipped > 0 then say(("Left out %d cards an earlier room could not draw"):format(skipped)) end
  if #cards < o.count then error(("only %d usable cards, %d asked for"):format(#cards, o.count), 0) end

  local anim = path.join(root, "Content/IndependentResources/RuntimeAnimations")
  -- Short clips only: every girl's pose bank holds each of her clips solved for her own body (GPU memory grows with the
  -- clip lengths times the girls). The plain stand is the Studio's 立ち1 (1.33 s), not the street's reading idle
  -- (idle_02) or conversation_neutral (several seconds each).
  local curves = { win(path.join(anim, "library/lib_0_0_1.okanim")), win(path.join(anim, "walk_01.okanim")) }
  local variants = {}
  for _, w in ipairs(WAITS) do
    local f = path.join(anim, w .. ".okanim")
    if path.is_file(f) then curves[#curves + 1] = win(f); variants[#variants + 1] = w .. ".okanim" end
  end
  for _, rel in ipairs(STAND_LIBRARY) do
    local f = path.join(anim, rel)
    if path.is_file(f) then curves[#curves + 1] = win(f); variants[#variants + 1] = path.name(f) end
  end

  local out = path.join(root, "Saved/Crowd/Room100-" .. os.date("%Y%m%dT%H%M%S"))
  path.mkdir(out, true)
  local characters, outfits = {}, {}
  for i, c in ipairs(cards) do
    outfits[i] = 0
    characters[i] = json.object({ { "path", win(c) }, { "walk_clip", "walk_01.okanim" } })
  end
  local room = json.object({ { "half_size", 2400 }, { "benchmark_seconds", o.benchmark }, { "warmup_seconds", o.warmup },
    { "report", win(path.join(out, "report.json")) }, { "exit_after_report", o.benchmark > 0 } })
  if o.cases then room.cases = read_json(o.cases) end
  if o.trace then room.trace_file = win(path.join(out, "trace.utrace")) end
  local manifest = json.object({
    { "cards", json.array((function() local t = {}; for i, c in ipairs(cards) do t[i] = win(c) end; return t end)()) },
    { "outfits", json.array(outfits) }, { "characters", json.array(characters) },
    { "curves", json.array(curves) }, { "idle_variants", json.array(variants) }, { "idle_variants_per_character", o.variants },
    { "material_root", materials.root }, { "material_library", "RuntimeMaterialBake" },
    { "ramp_catalogue", win(path.join(root, "Content/KKPreview/Ramps/ramps.json")) },
    { "display_lut", win(path.join(root, "Content/KKPreview/KKSDefaultSaturationLUT16.png")) },
    { "external_control", true }, { "enable_face_patterns", true }, { "report", win(path.join(out, "runtime.json")) },
    { "progressive_loading", true }, { "loading_cpu_preview", false }, { "parallel_loading_poses", true },
    { "auto_blink", true }, { "auto_action_streaming", false }, { "npc_action_preload_count", 0 },
    { "background_prepare_concurrency", 16 }, { "diagnostic_load_deadline_seconds", 300 },
    { "profile_seconds", 0 }, { "exit_after_report", false }, { "depth_prepass_enabled", true },
    { "room", room }, { "aggregate_gpu", o.aggregate == true },
  })
  if materials.hg_root then manifest.hg_material_root = materials.hg_root end
  local selection = path.join(out, "selection.json")
  path.write_text(selection, json.encode(manifest))

  ok.fs.setenv("OK_BUILTIN_RESOURCES", win(path.join(root, "Content/IndependentResources/v1")))
  local w, h = o.window:match("^(%d+)[xX](%d+)$")
  if not w then error("--window takes WxH", 0) end
  -- The street's render settings (OKStreetSocialScene BeginPlay: TAA at 100%, cascaded shadows, no CAS, no occlusion
  -- queries); the room path does not run that code, so they are passed here.
  -- Plus the room's own two (Docs/CROWD_ROOM100.md): no outline shells past 15 m (ok.Crowd.OutlineRange) and no depth
  -- prepass (r.EarlyZPass 0): a hundred girls are draw calls, these two took them from about 10000 to 5800 a frame.
  local execs = "r.AntiAliasingMethod 2,r.ScreenPercentage 100,r.Shadow.Virtual.Enable 0,r.VRS.ContrastAdaptiveShading 0,r.AllowOcclusionQueries 0,ok.Crowd.OutlineRange 1500,r.EarlyZPass 0,t.MaxFPS " .. o.max_fps
  if o.exec then execs = execs .. "," .. o.exec end
  local game = { exe, "/Game/Scenes/StreetSocial/StreetSocial", "-D3D12", "-SM6", "-windowed", "-ResX=" .. w, "-ResY=" .. h, "-WinX=40", "-WinY=40",
    "-CrowdRoom=" .. win(selection), "-abslog=" .. win(path.join(out, "game.log")), "-RuntimeGpuCacheMiB=4096", "-ExecCmds=" .. execs }
  local patch = path.join(root, launch.patch)
  if path.exists(patch) then game[#game + 1] = "-pakdir=" .. patch end
  if o.trace then game[#game + 1] = "-statnamedevents" end
  local pid = proc.spawn(game, { cwd = root })
  path.write_text(path.join(out, "game.pid"), tostring(pid))
  say("Crowd room: " .. #cards .. " girls, " .. #variants .. " standing poses (" .. o.variants .. " each), " .. exe)
  say(out)
  if o.benchmark > 0 then
    local report = path.join(out, "report.json")
    local deadline = time.time() + 1800
    while not path.is_file(report) do
      if time.time() > deadline then error("Crowd room benchmark: no report after 30 minutes", 0) end
      time.sleep(5)
    end
    time.sleep(1)
    say(path.read_text(report))
  end
  return 0
end

if ok.is_main() then ok.main(main) end
