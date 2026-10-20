-- Captures ok.ShotPreview frames of installed-game cards in the Unreal game (T-pose and/or Studio walk at 0.4 s)
-- and gates the run on the log's load/shader errors and per-card/per-view GPU, material and shadow readiness.
--   Tools\lua\lua.exe Tools\GamePoseProbe\run_dump_cards.lua --card-names KK_388205.png "弹丸论破 - 江之岛盾子2.png"
--     [--poses Tpose Walk] [--tag NewCards305] [--four-views] [--disable-outline] [--disable-color-lut]
--     [--native-item-audit] [--shoes-type 0|1] [--studio-shadow-depth 0..1]
-- Frames go to Saved/DebugShots/<tag>/<pose> (an existing pose folder is moved to <tag>/old/<timestamp>),
-- logs and <tag>Captures.json to Saved/Logs.
local ok = require "ok"
local path, fs = ok.path, ok.fs
local resolve_kk_card_path = require("resolve_card_path").resolve_kk_card_path

local function win(p) return (tostring(p):gsub("/", "\\")) end
local function full(p) return win(path.abs(p)) end

-- [ValidateSet] ignores case and keeps the caller's spelling.
local function one_of(choices)
  return function(v)
    for _, c in ipairs(choices) do if c:lower() == v:lower() then return v end end
    error("invalid choice: " .. v .. " (choose from " .. table.concat(choices, ", ") .. ")", 0)
  end
end

-- PowerShell 5.1 [float].ToString('R'): G7 when it round-trips as a single, else G9.
local function single_r(x)
  local f = string.unpack("<f", string.pack("<f", x))
  local s = string.format("%.7g", f)
  if string.unpack("<f", string.pack("<f", tonumber(s))) ~= f then s = string.format("%.9g", f) end
  return (s:gsub("e", "E"))
end

-- Get-ChildItem Env: | Where-Object Name -like 'OK_*' | Remove-Item
local function clear_ok_env()
  local res = ok.proc.run({ "cmd.exe", "/D", "/C", "set", "OK_" }, { capture = true })
  for line in (res.stdout or ""):gmatch("[^\n]+") do
    local name = line:match("^([^=]+)=")
    if name and name:sub(1, 3):upper() == "OK_" then fs.setenv(name, nil) end
  end
end
-- $env:NAME = value; $null and '' remove the variable.
local function setenv(name, value)
  if value == nil or value == "" then
    if fs.getenv(name) then fs.setenv(name, nil) end
  else
    fs.setenv(name, value)
  end
end

-- Start-Process -ArgumentList joins the list with spaces without quoting (UE parses -ExecCmds="..." itself);
-- only the executable and arguments holding spaces but no quotes are quoted here.
local function command_line(exe, list)
  local parts = { '"' .. win(exe) .. '"' }
  for _, a in ipairs(list) do
    if a:find(" ", 1, true) and not a:find('"', 1, true) then a = '"' .. a .. '"' end
    parts[#parts + 1] = a
  end
  return table.concat(parts, " ")
end

local function count(text, pattern)
  local n = 0
  for _ in text:gmatch(pattern) do n = n + 1 end
  return n
end

local function stamp_ms()
  local t = ok.time.time()
  return os.date("%Y%m%d-%H%M%S", math.floor(t)) .. string.format("-%03d", math.floor((t % 1) * 1000))
end

local function main()
  local p = ok.args.parser("Capture installed-game cards with ok.ShotPreview")
  p:opt("--card-names", { nargs = "+", required = true })
  p:opt("--poses", { nargs = "+", type = one_of({ "Tpose", "Walk" }), default = { "Tpose", "Walk" } })
  p:opt("--tag", { default = "NewCards305" })
  p:flag("--four-views")
  p:flag("--disable-outline")
  p:flag("--disable-color-lut")
  p:flag("--native-item-audit")
  p:opt("--shoes-type", { type = "int" })
  p:opt("--studio-shadow-depth", { type = "float" })
  local a = p:parse(arg)
  if not a.tag:match("^[A-Za-z0-9_%-]+$") then p:fail("--tag: expected ^[A-Za-z0-9_-]+$") end
  if a.shoes_type ~= nil and (a.shoes_type < 0 or a.shoes_type > 1) then p:fail("--shoes-type: expected 0..1") end
  local shadow_depth = a.studio_shadow_depth and single_r(a.studio_shadow_depth)
  if shadow_depth and (tonumber(shadow_depth) < 0 or tonumber(shadow_depth) > 1) then p:fail("--studio-shadow-depth: expected 0..1") end

  local project = win(ok.root)
  local game = ok.game_root()
  local cards = {}
  for i, name in ipairs(a.card_names) do cards[i] = resolve_kk_card_path(game, name) end
  local capture_root = full(project .. "/Saved/DebugShots/" .. a.tag)
  local log_root = project .. "\\Saved\\Logs"
  clear_ok_env()
  setenv("OK_GPU_SKIN", "1"); setenv("OK_SKELMESH", "1")
  if a.shoes_type ~= nil then setenv("OK_SHOES_TYPE", tostring(a.shoes_type)) end
  if a.four_views then setenv("OK_SHOT_FOUR_VIEWS", "1") end
  if a.disable_outline then setenv("OK_OUTLINE", "0") end
  if a.disable_color_lut then setenv("OK_NO_LUT", "1") end
  setenv("OK_SHOT_CARDS", table.concat(cards, ";")); setenv("OK_OUTFITS_LIMIT", "1")
  setenv("OK_SHOT_MOUTH_PATTERN", "0"); setenv("OK_SHOT_MOUTH_OPEN", "0.8"); setenv("OK_SHOT_BLINK", "open")
  setenv("OK_SHOT_ATTACHMENT_AUDIT", "1"); setenv("OK_SHOT_PV_AUDIT", "1")
  local inside = function(p2) return p2:sub(1, #capture_root + 1):lower() == (capture_root .. "\\"):lower() end
  local results = ok.json.array({})
  for _, pose in ipairs(a.poses) do
    local output = full(capture_root .. "\\" .. pose)
    if not inside(output) then error("Output outside capture root", 0) end
    if path.exists(output) then
      local archive = full(capture_root .. "\\old\\" .. stamp_ms())
      if not inside(archive) then error("Archive outside capture root", 0) end
      path.mkdir(archive)
      path.rename(output, archive .. "\\" .. path.name(output))
    end
    setenv("OK_SHOT_OUTPUT_DIR", output)
    if pose:lower() == "tpose" then
      setenv("OK_SHOT_RESET_POSE", "1"); setenv("OK_SHOT_STUDIO_ROW", nil)
      setenv("OK_SHOT_ANIM_SCENE", nil); setenv("OK_SHOT_ANIM_TIME", nil)
    else
      setenv("OK_SHOT_RESET_POSE", nil); setenv("OK_SHOT_STUDIO_ROW", "r00000022")
      setenv("OK_SHOT_ANIM_SCENE", "4"); setenv("OK_SHOT_ANIM_TIME", "0.4")
    end
    local log = log_root .. "\\" .. a.tag .. "_" .. pose .. ".log"
    local stderr = log_root .. "\\" .. a.tag .. "_" .. pose .. "Stderr.log"
    local executable = ok.engine_root() .. "/Engine/Binaries/Win64/UnrealEditor.exe"
    local arguments = { project .. "\\OpenKoikatsu.uproject", "-game", "-windowed", "-ForceRes", "-ResX=900", "-ResY=1080",
      "-unattended", "-nosound", "-NoSplash", "-nop4", '-ExecCmds="ok.ShotPreview"', "-abslog=" .. log }
    if a.native_item_audit then arguments[#arguments + 1] = "-OKNativeItemAudit" end
    if shadow_depth then arguments[#arguments + 1] = "-OKStudioShadowDepth=" .. shadow_depth end
    local owned = ok.proc.popen(command_line(executable, arguments), { shell = false, cwd = project, hide = true, stdout = "NUL", stderr = stderr })
    local pid = owned:pid()
    -- Cold prefab/session caches can take over a minute per mod card.
    -- Keep a bounded timeout while accounting for the requested batch size.
    local timeout_minutes = math.max(10, 3 + 2 * #cards)
    local deadline = ok.time.time() + timeout_minutes * 60
    print(("%s %s PID=%d"):format(a.tag, pose, pid))
    local code = owned:wait(1)
    while not code do
      if ok.time.time() > deadline then
        owned:kill() -- our own process handle: no PID reuse, so no identity check is needed
        error("Capture timeout; see " .. log, 0)
      end
      code = owned:wait(1)
    end
    if code ~= 0 then error("Invalid capture exit: " .. code, 0) end
    local text = path.read_text(log)
    local lower = text:lower()
    for _, gate in ipairs({ "logtemp: error:", "requestexitwithstatus%([01], 1,", "ok.shotpreview [^\n]*failed",
      "ok.shotpreview fell back", "failed to compile material", "logshadercompilers: error:" }) do
      if lower:find(gate) then error("Load/display/shader gate failed; see " .. log, 0) end
    end
    local material_ready = count(text, "ok.ShotPreview material readiness pending_before=%d+ pending_after=0")
    local gpu_ready = count(text, "ok.ShotPreview GPU resource readiness completed=1")
    if material_ready ~= #cards or gpu_ready ~= #cards then
      error(("Incomplete material/GPU readiness: material=%d GPU=%d cards=%d; see %s"):format(material_ready, gpu_ready, #cards, log), 0)
    end
    local images = {}
    for _, f in ipairs(path.glob(output, "*.png")) do if path.is_file(f) then images[#images + 1] = win(f) end end
    local expected = #cards * (a.four_views and 4 or 3)
    if #images ~= expected then error(("Expected %d captures; got %d"):format(expected, #images), 0) end
    local ready_names, unique, views = {}, {}, 0
    for p_ in text:gmatch("ok.ShotPreview view GPU readiness completed=1 path=([^\n]*)") do
      views = views + 1
      local n = path.name((ok.util.strip(p_)):gsub("\\", "/")):lower()
      ready_names[n] = true
      if not unique[n] then unique[n] = true; unique[#unique + 1] = n end
    end
    local missing = 0
    for _, f in ipairs(images) do if not ready_names[path.name(f):lower()] then missing = missing + 1 end end
    if views ~= expected or #unique ~= expected or missing ~= 0 then error("Incomplete per-view GPU readiness; see " .. log, 0) end
    if count(text, "ok.ShotPreview shadow readiness captured=1 pose_clock_held=1 materials=[1-9]%d*") ~= expected then
      error("Incomplete frozen-pose shadow readiness; see " .. log, 0)
    end
    local digests = ok.hash.files(images, "sha256")
    local rows = ok.json.array({})
    for i, f in ipairs(images) do rows[i] = ok.json.object({ { "path", f }, { "sha256", tostring(digests[i]):lower() } }) end
    results[#results + 1] = ok.json.object({ { "pose", pose }, { "pid", pid }, { "exit", code }, { "log", log }, { "stderr", stderr }, { "images", rows } })
    print(("%s images=%d exit=%d"):format(pose, #images, code))
  end
  ok.write_json(log_root .. "\\" .. a.tag .. "Captures.json", results, { indent = 2 })
end

ok.main(main)
