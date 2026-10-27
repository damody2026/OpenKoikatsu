-- The hundred-girl room (CrowdRoom100.cmd): the StreetSocial runtime characters (same build, materials, GPU poses,
-- shadows and render settings as the street's heroines) wandering an empty room (Source/OpenKoikatsu/OKCrowdRoom.cpp).
-- Runs the newest StreetSocial stage with -CrowdRoom=<manifest>; nothing else of the street starts.
--   Tools\lua\lua.exe Tools\run_crowd_room100.lua [--count N] [--cards <dir>] [--benchmark <seconds>] [--warmup <seconds>]
--       [--max-fps N] [--window WxH] [--executable <exe>] [--variants N] [--exec "CMD,CMD"] [--cases <json>] [--trace] [--aggregate]
--       [--gpu-rig] [--gpu-rig-verify N] [--editor] [--follow <card name part>] [--record <seconds>]
-- --follow: a demo: that girl (the first card whose file name contains it) walks the whole time and the camera follows
-- her from behind, without the on-screen numbers. --record: a video of that many seconds (demo.mp4 beside the report)
-- once everyone is drawn and warmed up; then the game quits. ffmpeg from Tools\ffmpeg grabs this game's own window by
-- its handle (looked up from the process id) with Windows.Graphics.Capture (gfxcapture), which sees the window's own
-- frames even behind other windows (not minimised); the video is then checked to move (no freeze of 1 s or more,
-- frames under demo-frames/), else an error. --check-video <mp4> [--record <seconds>]: only that check, on any video.
-- --aggregate: once everyone is loaded, compose every girl's pose bank into one GPU bank (one skinning provider instead
-- of a hundred; needs twice the banks' memory while composing).
-- --gpu-rig: pose every girl on the GPU from the shared clips and her body parameters (Shaders/GpuRig.usf) instead of
-- per-girl solved banks (implies --aggregate; physics is not simulated). --gpu-rig-verify N: keep the full banks too and
-- compare N frames of the rig's rows with them (runtime.json "gpu_rig"). --editor: run uncooked in the E engine's
-- UnrealEditor -game (new shaders without a cook); otherwise the newest stage.
-- --cases: a JSON array [{"name": "...", "exec": ["console command", ...]}, ...]: each case is applied in the same running
-- game and measured for --benchmark seconds in turn (an A/B without loading the girls again). --trace: an Unreal
-- Insights trace (cpu, frame, named events) of the first measurement, trace.utrace beside the report.
-- --benchmark: measure that many seconds once every girl is drawn and warmed up, write report.json (frame mean / P95,
-- game / render / GPU ms, the same numbers as the street's /fps) and a screenshot, then quit. Measure with only this
-- Unreal game running (t.MaxFPS 0 unless --max-fps).
-- Cards whose outfit 0 wears neither a top nor a bottom, or has a material the runtime cannot draw, are left out
-- (decided offline from the card, check_cards). --check-cards: only that check (every card), then the usable count;
-- no game. --no-card-check: open no card, use the verdicts already in Saved/Crowd/card-check.tsv (a card without one
-- counts as usable).
local ok = require "ok"
local path, json, proc, time = ok.path, ok.json, ok.proc, require("ok.time")

local function say(s) io.write(s, "\n"); io.flush() end
local function read_json(p) return json.decode(path.read_text(p)) end
local function win(p) return (path.norm(p):gsub("/", "\\")) end
local function ffmpeg_tool(root, name) return win(path.join(root, "Tools/ffmpeg/" .. name .. ".exe")) end

-- The main window handle of a process (nil while it has none). Recording goes by this handle, never by the window
-- title: every Unreal game here has the same title, and another one may be running.
local function main_window(pid)
  local r = proc.run({ "powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
    ("(Get-Process -Id %d -ErrorAction SilentlyContinue).MainWindowHandle"):format(pid) }, { capture = true, hide = true })
  local h = tonumber(((r.stdout or ""):match("(%d+)")))
  if h and h ~= 0 then return h end
end

-- A recording must show the game moving: about the asked length at ~60 fps, no still stretch of 1 s or more (ffmpeg
-- freezedetect at -60 dB) and a mean frame-to-frame luma change (signalstats YDIF) above 0.2. Five frames (1 s, 1/4,
-- 1/2, 3/4, last second) go to <dir>/demo-frames for a look; the numbers to <dir>/video-check.json. Errors if it fails.
local function check_video(root, video, seconds, dir)
  local probe = proc.run({ ffmpeg_tool(root, "ffprobe"), "-v", "error", "-select_streams", "v:0", "-count_packets",
    "-show_entries", "format=duration:stream=nb_read_packets,width,height", "-of", "default=nw=1", win(video) }, { capture = true })
  local info = probe.stdout or ""
  local duration, frames = tonumber(info:match("duration=([%d%.]+)")), tonumber(info:match("nb_read_packets=(%d+)"))
  if not duration or not frames then error("Video check: cannot read " .. win(video) .. "\n" .. info .. (probe.stderr or ""), 0) end
  local r = proc.run({ ffmpeg_tool(root, "ffmpeg"), "-hide_banner", "-nostats", "-i", win(video), "-map", "0:v:0", "-vf",
    "freezedetect=n=-60dB:d=1,signalstats,metadata=mode=print:key=lavfi.signalstats.YDIF", "-f", "null", "-" }, { capture = true, merge = true })
  local text = r.stdout or ""
  local freezes = {}
  for s in text:gmatch("freeze_start: ([%d%.]+)") do freezes[#freezes + 1] = { tonumber(s), duration } end
  local k = 0
  for e in text:gmatch("freeze_end: ([%d%.]+)") do k = k + 1; if freezes[k] then freezes[k][2] = tonumber(e) end end
  local sum, n = 0, 0
  -- (the first frame's YDIF is against nothing: left out)
  local first = true
  for d in text:gmatch("lavfi%.signalstats%.YDIF=([%d%.]+)") do
    if first then first = false else sum = sum + tonumber(d); n = n + 1 end
  end
  local ydif = n > 0 and sum / n or 0
  local shots = path.join(dir, "demo-frames")
  path.mkdir(shots, true)
  local picks = {}
  for _, t in ipairs({ 1, duration / 4, duration / 2, duration * 3 / 4, duration - 1 }) do
    t = math.max(0, math.min(t, duration - 0.05))
    local png = path.join(shots, ("t%05.2f.png"):format(t))
    proc.run({ ffmpeg_tool(root, "ffmpeg"), "-y", "-loglevel", "error", "-ss", ("%.2f"):format(t), "-i", win(video), "-frames:v", "1", win(png) }, { quiet = true })
    picks[#picks + 1] = win(png)
  end
  local problems = {}
  if not r.ok then problems[#problems + 1] = "ffmpeg could not decode it" end
  if seconds and duration < seconds - 1 then problems[#problems + 1] = ("%.1f s long, %d s asked for"):format(duration, seconds) end
  if frames < duration * 50 then problems[#problems + 1] = ("only %d frames in %.1f s"):format(frames, duration) end
  for _, f in ipairs(freezes) do problems[#problems + 1] = ("still from %.2f s to %.2f s"):format(f[1], f[2]) end
  if ydif < 0.2 then problems[#problems + 1] = ("mean frame change %.3f (still)"):format(ydif) end
  local freeze_list = {}
  for i, f in ipairs(freezes) do freeze_list[i] = json.array({ f[1], f[2] }) end
  path.write_text(path.join(dir, "video-check.json"), json.encode(json.object({ { "video", win(video) }, { "seconds", duration },
    { "frames", frames }, { "mean_ydif", ydif }, { "freezes", json.array(freeze_list) }, { "frames_png", json.array(picks) },
    { "ok", #problems == 0 }, { "problems", json.array(problems) } })))
  say(("Video check %s: %.1f s, %d frames, mean frame change %.2f, %d still stretches"):format(win(video), duration, frames, ydif, #freezes))
  if #problems > 0 then error("Video check failed (" .. win(video) .. "): " .. table.concat(problems, "; "), 0) end
end

local function parse(argv)
  local o = { count = 100, benchmark = 0, warmup = 10, max_fps = 0, window = "1920x1080", variants = 3 }
  local i = 1
  while i <= #argv do
    local a = argv[i]:lower():gsub("^%-%-?", "")
    local v = argv[i + 1]
    if a == "count" then o.count = tonumber(v); i = i + 1
    elseif a == "cards" then o.cards = v; i = i + 1
    elseif a == "check-cards" then o.check_cards = true
    elseif a == "no-card-check" then o.no_card_check = true
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
    elseif a == "gpu-rig" then o.gpu_rig = true; o.aggregate = true
    elseif a == "gpu-rig-verify" then o.gpu_rig_verify = tonumber(v); o.gpu_rig = true; o.aggregate = true; i = i + 1
    elseif a == "editor" then o.editor = true
    elseif a == "follow" then o.follow = v; i = i + 1
    elseif a == "record" then o.record = tonumber(v); i = i + 1
    elseif a == "check-video" then o.check_video = v; i = i + 1
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

-- Whether the room can use each card, decided offline from the card itself (not from earlier rooms' logs, and not from
-- its cover image, which is not outfit 0): `ok-extract --oklib crowd-check` opens outfit 0 as the runtime does and
-- tells whether she wears a top or a bottom and which drawn parts have a material the runtime cannot draw
-- (Tools/ok-extract/src/oklib/crowd_check.rs). Cards are checked in order until `want` are usable (a few seconds per
-- card); verdicts are kept in Saved/Crowd/card-check.tsv by file size and modification time, so later rooms open only
-- new or changed cards. Returns card -> { dressed, roles, unsupported ("-": none) } for the cards checked.
local CARD_CHECK_VERSION = 3
local function read_card_checks(store, into)
  if not path.is_file(store) then return into end
  for line in io.lines(store) do
    local v, c, st, dressed, roles, unsupported = line:match("^(%d+)\t([^\t]+)\t([^\t]+)\t([01])\t([^\t]*)\t(.*)$")
    if v and tonumber(v) == CARD_CHECK_VERSION then into[c] = { stamp = st, dressed = dressed == "1", roles = roles, unsupported = unsupported } end
  end
  return into
end
local function check_cards(root, cards, want, cached_only)
  local lib = path.join(root, "Content/IndependentResources/base_v1")
  local store = path.join(root, "Saved/Crowd/card-check.tsv")
  local tool = path.join(root, "Tools/ok-extract/target/release/ok-extract.exe")
  local stamp = function(c) return ("%d:%d"):format(path.size(c) or -1, math.floor(path.mtime(c) or -1)) end
  local known = read_card_checks(store, {})
  local out = {}
  local function current(c) local k = known[win(c)]; return k and k.stamp == stamp(c) and k or nil end
  while true do
    local usable, todo = 0, {}
    for _, c in ipairs(cards) do
      local k = out[c] or current(c)
      if k then out[c] = k; if k.dressed and k.unsupported == "-" then usable = usable + 1 end
      elseif usable + #todo < want + 4 then todo[#todo + 1] = c end
    end
    if usable >= want or #todo == 0 or cached_only then break end
    if not path.is_file(tool) then error("Card check needs " .. win(tool) .. " (cargo build --release --manifest-path Tools/ok-extract/Cargo.toml --bin ok-extract)", 0) end
    say(("Checking %d cards' outfit 0 (clothes, materials) ..."):format(#todo))
    -- The library as the game folder: an OK card opens from the library and its own pack only.
    local argv = { win(tool), win(lib), "--oklib", "crowd-check", win(lib) }
    for _, c in ipairs(todo) do argv[#argv + 1] = win(c) end
    local r = proc.run(argv, { capture = true })
    local fresh = {}
    for line in (r.stdout or ""):gmatch("[^\r\n]+") do
      local c, dressed, roles, unsupported = line:match("^OKCROWD\t([^\t]+)\t([01])\t([^\t]*)\t(.*)$")
      if c then fresh[c] = { stamp = stamp(c), dressed = dressed == "1", roles = roles, unsupported = unsupported } end
    end
    -- (another room may have written the store meanwhile: merge)
    known = read_card_checks(store, {})
    for c, k in pairs(fresh) do known[c] = k end
    local rows = {}
    for c, k in pairs(known) do rows[#rows + 1] = table.concat({ CARD_CHECK_VERSION, c, k.stamp, k.dressed and "1" or "0", k.roles, k.unsupported }, "\t") end
    table.sort(rows)
    path.mkdir(path.parent(store), true)
    path.write_text(store, table.concat(rows, "\n") .. "\n", { atomic = true })
    if not next(fresh) then error("Card check failed:\n" .. (r.stdout or "") .. (r.stderr or ""), 0) end
    -- A card that cannot be opened is left out this time (not remembered).
    for _, c in ipairs(todo) do out[c] = fresh[win(c)] or { dressed = false, roles = "", unsupported = "cannot be opened" } end
  end
  return out
end

local function main()
  local o = parse(arg)
  local root = ok.root
  if o.check_video then check_video(root, o.check_video, o.record, path.parent(path.abs(o.check_video))); return 0 end
  local launch = read_json(path.join(root, "Content/IndependentResources/StreetSocialData/Launch/runtime.json"))
  local materials = read_json(path.join(root, launch.materials))
  local exe = o.executable or (o.editor and path.join(ok.engine_root(), "Engine/Binaries/Win64/UnrealEditor.exe")) or newest_stage(root)
  if not o.check_cards and not (exe and path.is_file(exe)) then error("No StreetSocial stage under Saved/StreetSocial/Build (build one with Tools/rebuild_street_social_runtime.lua)", 0) end
  -- Cards: the crowd set first, then the street heroines, in file name order. A card the room cannot use is left out
  -- and the next one takes its place (check_cards).
  local dirs = o.cards and { o.cards } or { path.join(root, "Content/CharacterCards/CrowdAPNG"), path.join(root, "Content/CharacterCards/StreetHeroines") }
  local all = {}
  for _, dir in ipairs(dirs) do
    local here = {}
    if path.is_dir(dir) then
      for _, n in ipairs(path.list(dir)) do
        -- (".okremake-*.png": a card being rewritten in place)
        if n:lower():match("%.png$") and n:sub(1, 1) ~= "." then here[#here + 1] = path.join(dir, n) end
      end
    end
    table.sort(here)
    for _, c in ipairs(here) do all[#all + 1] = c end
  end
  local verdicts = check_cards(root, all, o.check_cards and math.huge or o.count, o.no_card_check)
  local cards, skipped, undressed, unchecked = {}, 0, 0, 0
  for _, c in ipairs(all) do
    -- (no verdict: past the cards needed, or --no-card-check without one)
    local v = verdicts[c]
    if not v then unchecked = unchecked + 1; if o.no_card_check and #cards < o.count then cards[#cards + 1] = c end
    elseif v.unsupported ~= "-" then skipped = skipped + 1; say(("Left out %s: material the runtime cannot draw (%s)"):format(path.name(c), v.unsupported))
    elseif not v.dressed then undressed = undressed + 1; say(("Left out %s: outfit 0 wears neither a top nor a bottom (%s)"):format(path.name(c), v.roles))
    elseif #cards < o.count then cards[#cards + 1] = c end
  end
  say(("Cards: %d usable of %d checked (%d with an unsupported material, %d undressed), %d not checked"):format(#all - unchecked - skipped - undressed,
    #all - unchecked, skipped, undressed, unchecked))
  if o.check_cards then return 0 end
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
  -- A recording runs as a measurement of its length (the room quits after it).
  -- The margin covers finding the window and starting ffmpeg after "benchmark starts" (the game quits 3 s after it).
  if o.record then o.benchmark = math.max(o.benchmark, o.record + 6) end
  local room = json.object({ { "half_size", 2400 }, { "benchmark_seconds", o.benchmark }, { "warmup_seconds", o.warmup },
    { "report", win(path.join(out, "report.json")) }, { "exit_after_report", o.benchmark > 0 } })
  if o.follow then
    local index
    for i, c in ipairs(cards) do if path.name(c):lower():find(o.follow:lower(), 1, true) then index = i - 1; break end end
    if not index then error("--follow: no card named like " .. o.follow .. " among the room's girls", 0) end
    room.follow = index
    say(("Following %s (girl %d)"):format(path.name(cards[index + 1]), index))
  end
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
    { "gpu_rig", o.gpu_rig == true }, { "gpu_rig_verify", o.gpu_rig_verify or 0 }, { "editor_common_materials", o.editor == true },
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
  local game = o.editor and { exe, win(path.join(root, "OpenKoikatsu.uproject")), "/Game/Scenes/StreetSocial/StreetSocial", "-game", "-D3D12", }
    or { exe, "/Game/Scenes/StreetSocial/StreetSocial", "-D3D12", }
  for _, a in ipairs({ "-SM6", "-windowed", "-ResX=" .. w, "-ResY=" .. h, "-WinX=40", "-WinY=40",
    "-CrowdRoom=" .. win(selection), "-abslog=" .. win(path.join(out, "game.log")), "-RuntimeGpuCacheMiB=4096", "-ExecCmds=" .. execs }) do game[#game + 1] = a end
  local patch = path.join(root, launch.patch)
  if not o.editor and path.exists(patch) then game[#game + 1] = "-pakdir=" .. patch end
  if o.trace then game[#game + 1] = "-statnamedevents" end
  if o.gpu_rig then game[#game + 1] = "-OKGpuRig" end
  local pid = proc.spawn(game, { cwd = root })
  path.write_text(path.join(out, "game.pid"), tostring(pid))
  if o.record then
    -- Once the measurement starts (everyone drawn, warmed up): this game's own window for o.record seconds. Found by
    -- the process id's window handle, not the title (all the Unreal games here share it), and grabbed through
    -- Windows.Graphics.Capture (ffmpeg gfxcapture): it takes the window's own frames even behind other windows. The
    -- old gdigrab GDI copy of the D3D12 window gave a still image (Docs/ERROR_LOG.md).
    local log, deadline = path.join(out, "game.log"), time.time() + 1800
    while not (path.is_file(log) and path.read_text(log):find("Crowd room: benchmark starts", 1, true)) do
      if time.time() > deadline then error("Crowd room record: the measurement never started", 0) end
      time.sleep(0.5)
    end
    local hwnd = main_window(pid)
    if not hwnd then error("Crowd room record: process " .. pid .. " has no window", 0) end
    local video = path.join(out, "demo.mp4")
    say(("Recording %d s of process %d's window 0x%X -> %s"):format(o.record, pid, hwnd, win(video)))
    local rec = proc.run({ ffmpeg_tool(root, "ffmpeg"), "-y", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
      "-i", ("gfxcapture=hwnd=%d:capture_cursor=0:max_framerate=60"):format(hwnd), "-t", tostring(o.record),
      "-vf", "hwdownload,format=bgra,fps=60,scale=trunc(iw/2)*2:trunc(ih/2)*2,format=yuv420p",
      "-c:v", "h264_nvenc", "-preset", "p5", "-cq", "19", win(video) }, { stdout_file = path.join(out, "ffmpeg.log"), merge = true })
    if not rec.ok then error("Crowd room record: ffmpeg failed (" .. win(path.join(out, "ffmpeg.log")) .. ")", 0) end
    say(win(video))
    check_video(root, video, o.record, out)
  end
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
