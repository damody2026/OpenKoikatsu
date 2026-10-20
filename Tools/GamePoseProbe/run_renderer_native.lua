-- Native renderer fixtures from the original CharaStudio: installs OpenKoikatsu.GamePoseProbe.dll into the game's
-- BepInEx/plugins for the run only (moved back to Tools/GamePoseProbe/OpenKoikatsu.GamePoseProbe.<tag>.last-run.dll
-- afterwards), starts Studio once per card and waits for Saved/Tests/<tag>/Card<i>.json plus the requested extras.
--   Tools\lua\lua.exe Tools\GamePoseProbe\run_renderer_native.lua --card-names "弹丸论破 - 江之岛盾子2.png" [--tag NewCards306]
--     [--mesh-names a b] [--isolation-mesh-names ...] [--hair-rim-mesh-names ...] [--clothes-create-names ...|ALL]
--     [--texture-mips] [--light-audit] [--ramp-audit] [--capture-face] [--capture-body] [--capture-isolations]
--     [--capture-effects] [--renderer-controls] [--material-texture-controls] [--accessory-physics]
--     [--texture-coverage] [--face-create] [--body-create] [--body-morph] [--state-only] [--outfit 0..6]
--     [--studio-shadow-depth 0..1]
-- Build the probe first: Tools\lua\lua.exe Tools\GamePoseProbe\build.lua
local ok = require "ok"
local path, fs, util = ok.path, ok.fs, ok.util
local resolve_kk_card_path = require("resolve_card_path").resolve_kk_card_path

local function win(p) return (tostring(p):gsub("/", "\\")) end

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
local function flag(v) return v and "1" or nil end

-- Running processes whose executable is `exe` (ok-luakit proc-list: name filter, full image path).
local function running(exe)
  for _, p in ipairs(ok.kit.call({ "proc-list", path.name(exe) }) or {}) do
    if type(p.path) == "string" and p.path:gsub("/", "\\"):lower() == exe:gsub("/", "\\"):lower() then return true end
  end
  return false
end

local function exists(p) return path.exists(p) end

local function main()
  local p = ok.args.parser("Native CharaStudio renderer fixtures through the GamePoseProbe plugin")
  p:opt("--card-names", { nargs = "+", required = true })
  p:opt("--tag", { default = "NewCards306" })
  p:opt("--mesh-names", { nargs = "*", default = {} })
  p:opt("--isolation-mesh-names", { nargs = "*", default = {} })
  p:opt("--hair-rim-mesh-names", { nargs = "*", default = {} })
  p:opt("--clothes-create-names", { nargs = "*", default = {} })
  for _, f in ipairs({ "--texture-mips", "--light-audit", "--ramp-audit", "--capture-face", "--capture-body",
    "--capture-isolations", "--capture-effects", "--renderer-controls", "--material-texture-controls",
    "--accessory-physics", "--texture-coverage", "--face-create", "--body-create", "--body-morph", "--state-only" }) do
    p:flag(f)
  end
  p:opt("--outfit", { type = "int", default = 0 })
  p:opt("--studio-shadow-depth", { type = "float" })
  local a = p:parse(arg)
  if not a.tag:match("^[A-Za-z0-9_%-]+$") then p:fail("--tag: expected ^[A-Za-z0-9_-]+$") end
  for _, n in ipairs(a.clothes_create_names) do
    if not n:match("^[A-Za-z0-9_]+$") then p:fail("--clothes-create-names: expected ^[A-Za-z0-9_]+$: " .. n) end
  end
  if a.outfit < 0 or a.outfit > 6 then p:fail("--outfit: expected 0..6") end
  local shadow_depth = a.studio_shadow_depth and single_r(a.studio_shadow_depth)
  if shadow_depth and (tonumber(shadow_depth) < 0 or tonumber(shadow_depth) > 1) then p:fail("--studio-shadow-depth: expected 0..1") end
  local mesh_names, isolation, rim, clothes = a.mesh_names, a.isolation_mesh_names, a.hair_rim_mesh_names, a.clothes_create_names

  if (a.capture_isolations or a.capture_effects or #rim > 0) and not a.capture_face then
    error("Face isolation, effects, and rim comparisons require --capture-face", 0)
  end
  local project = win(ok.root)
  local here = win(ok.script_dir)
  local game = ok.game_root()
  local exe = win(path.abs(game .. "/CharaStudio.exe"))
  local installed = win(game .. "/BepInEx/plugins/OpenKoikatsu.GamePoseProbe.dll")
  if exists(installed) then error("Probe already installed; account for its owner", 0) end
  if running(exe) then error("Existing Studio process must retain its original plugins", 0) end
  local output = project .. "\\Saved\\Tests\\" .. a.tag
  path.mkdir(output)
  local cards = {}
  for i, name in ipairs(a.card_names) do cards[i] = resolve_kk_card_path(game, name) end
  clear_ok_env()
  setenv("OK_PROBE_OUTFIT", tostring(a.outfit)); setenv("OK_PROBE_RENDERERS", "1"); setenv("OK_PROBE_EXPRESSION", "0")
  setenv("OK_PROBE_STATE_ONLY", flag(a.state_only))
  setenv("OK_PROBE_ACCESSORY_PHYSICS", flag(a.accessory_physics))
  setenv("OK_PROBE_BODY_CREATE", flag(a.body_create))
  setenv("OK_PROBE_BODY_MORPH", flag(a.body_morph))
  setenv("OK_PROBE_FACE_CREATE", flag(a.face_create))
  setenv("OK_PROBE_TEXTURE_COVERAGE", flag(a.texture_coverage))
  setenv("OK_PROBE_RENDERER_MESHES", table.concat(mesh_names, ";"))
  setenv("OK_PROBE_ISOLATION_MESHES", table.concat(isolation, ";"))
  setenv("OK_PROBE_HAIR_RIM_MESHES", table.concat(rim, ";"))
  setenv("OK_PROBE_CLOTHES_CREATE_NAMES", table.concat(clothes, ";"))
  setenv("OK_PROBE_TEXTURE_MIPS", flag(a.texture_mips))
  setenv("OK_PROBE_LIGHTS", flag(a.light_audit or a.ramp_audit))
  setenv("OK_PROBE_RAMP", flag(a.ramp_audit))
  setenv("OK_PROBE_RENDERER_FULL_BODY", flag(a.capture_body))
  setenv("OK_PROBE_RENDERER_FACE", flag(a.capture_face or a.capture_body))
  setenv("OK_PROBE_RENDERER_ISOLATIONS", flag(a.capture_isolations))
  setenv("OK_PROBE_RENDERER_EFFECTS", flag(a.capture_effects))
  setenv("OK_PROBE_MATERIAL_TEXTURE_CONTROLS", flag(a.material_texture_controls))
  setenv("OK_PROBE_RENDERER_CONTROLS", flag(a.renderer_controls))
  setenv("OK_STUDIO_SHADOW_DEPTH", shadow_depth)

  local results, owned = ok.json.array({}), nil
  path.copy(here .. "\\OpenKoikatsu.GamePoseProbe.dll", installed)
  local okv, err = xpcall(function()
    for i0 = 0, #cards - 1 do
      local card = cards[i0 + 1]
      local fixture = output .. "\\Card" .. i0 .. ".json"
      setenv("OK_PROBE_CARD", card)
      setenv("OK_GAME_POSE_OUTPUT", fixture)
      if exists(fixture) then error("Use a new tag to retain prior fixtures", 0) end
      owned = ok.proc.popen('"' .. exe .. '"', { shell = false, cwd = game, hide = true, stdout = "NUL", stderr = "NUL" })
      print(("Native renderer card=%s PID=%d"):format(a.card_names[i0 + 1], owned:pid()))
      local deadline = ok.time.time() + 5 * 60
      while not exists(fixture) do
        if owned:poll() then error("Native exited before completion", 0) end
        if ok.time.time() > deadline then error("Native renderer fixture timeout", 0) end
        ok.time.sleep(1)
      end
      local completion = ok.read_json(fixture)
      if util.truthy(completion.error) then error(tostring(completion.error), 0) end
      local rows = ok.read_json(fixture .. ".renderers.json")
      if ok.json.is_object(rows) then rows = { rows } end -- @(...) of a single object
      if #rows < 10 then error("Incomplete native renderer fixture", 0) end
      local function has(suffix) return exists(fixture .. suffix) end
      local function any(pattern) return #path.glob(output, "Card" .. i0 .. ".json" .. pattern) > 0 end
      if (a.capture_face or a.capture_body) and not has(".face.png") then error("Native face capture absent", 0) end
      if a.capture_body and not has(".body.png") then error("Native body capture absent", 0) end
      if a.renderer_controls and not any(".renderer*.controls.tsv") then error("Native renderer material controls absent", 0) end
      if a.face_create and (not has(".face-before-probe.png") or not has(".face-overlays.tsv")) then error("Native face/create provenance fixture absent", 0) end
      if a.accessory_physics and not has(".accessory-physics.tsv") then error("Native accessory physics control absent", 0) end
      if a.material_texture_controls and not any(".renderer*.material-texture-control.json") then error("Native MaterialAPI controls absent", 0) end
      if a.capture_isolations and not has(".face-isolations.tsv") then error("Native renderer isolation capture absent", 0) end
      if a.capture_effects and not has(".face-effects.tsv") then error("Native effect capture absent", 0) end
      if #rim > 0 and not has(".face-rim.tsv") then error("Native hair rim capture absent", 0) end
      if (a.light_audit or a.ramp_audit) and not has(".lights.tsv") then error("Native scene light fixture absent", 0) end
      if (a.light_audit or a.ramp_audit) and not has(".shader-globals.tsv") then error("Native shader globals fixture absent", 0) end
      if a.ramp_audit and not has(".ramp.png") then error("Native ramp texture fixture absent", 0) end
      if a.texture_mips then
        local sampling = path.glob(output, "Card" .. i0 .. ".json.renderer*.sampling.tsv")
        if #mesh_names > 0 and #sampling == 0 then error("Requested MIP sampling fixture absent", 0) end
        local control = fixture .. ".mip-controls.tsv"
        if not exists(control) then error("Native MIP controls absent", 0) end
        local rt_count = 0
        local tsvs = {}
        for _, s in ipairs(sampling) do tsvs[#tsvs + 1] = s end
        tsvs[#tsvs + 1] = control
        for _, tsv in ipairs(tsvs) do
          for _, line in ipairs(util.splitlines(path.read_text(tsv))) do
            local cols = util.split(line, "\t")
            if #cols >= 11 and cols[1]:lower() == "sampling" and cols[4]:lower() == "rendertexture" then
              rt_count = rt_count + 1
              local levels = math.tointeger(util.round(tonumber(cols[5]) or error("Cannot convert value to int: " .. cols[5], 0)))
              for level = 0, levels - 1 do
                local mip = fixture .. cols[2] .. ".mip" .. level .. ".rgba8"
                if not path.is_file(mip) or path.size(mip) < 4 then error("Missing native mip: " .. mip, 0) end
              end
            end
          end
        end
        if rt_count < 4 then error("Incomplete native MIP controls", 0) end
        for _, size in ipairs({ "5x3", "1x7", "7x1", "4x4", "8x4" }) do
          local direct = fixture .. ".mip-control." .. size .. ".direct.mip0.rgba8"
          if not exists(direct) then error("Direct GPU mip control absent: " .. direct, 0) end
        end
      end
      for _, name in ipairs(clothes) do
        if name:lower() == "all" then
          if not any(".clothes-create.*.tsv") then error("Missing native clothes-create inputs", 0) end
        else
          local prefix = fixture .. ".clothes-create." .. name
          for _, suffix in ipairs({ ".tsv", "._MainTex.rgba32f", "._ColorMask.rgba32f" }) do
            if not exists(prefix .. suffix) then error("Missing native clothes input: " .. prefix .. suffix, 0) end
          end
        end
      end
      local code = owned:wait(15)
      if not code then
        owned:kill() -- our own process handle: the identity cannot change
        code = owned:wait()
        print("Owned Studio delayed quit stopped")
      end
      results[#results + 1] = ok.json.object({ { "card", card }, { "outfit", a.outfit }, { "pid", owned:pid() }, { "exit", code },
        { "fixture", fixture }, { "renderers", #rows } })
      print(("Renderer snapshot complete rows=%d exit=%d"):format(#rows, code))
      owned = nil
    end
    ok.write_json(output .. "\\Sources.json", results, { indent = 2 })
  end, debug.traceback)
  -- finally
  if owned and not owned:poll() then owned:kill(); owned:wait() end
  if exists(installed) then
    -- Never leave the probe in the game: a reused tag gets a timestamped name instead of failing the move.
    local last = here .. "\\OpenKoikatsu.GamePoseProbe." .. a.tag .. ".last-run.dll"
    if exists(last) then last = here .. "\\OpenKoikatsu.GamePoseProbe." .. a.tag .. ".last-run." .. os.date("%Y%m%d%H%M%S") .. ".dll" end
    path.rename(installed, last)
  end
  if not okv then error(err, 0) end
end

ok.main(main)
