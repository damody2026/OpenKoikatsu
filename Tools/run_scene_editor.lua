-- OK Studio scene editor on its own (SceneEditor.cmd): UnrealEditor -game on an empty map with AOKStudioGameMode,
-- so only the studio's panel and viewer show (no card reader / gallery UI).
--
--   Tools\lua\lua.exe Tools\run_scene_editor.lua [SCENE | -Scene SCENE] [-WindowWidth N] [-WindowHeight N]
--     [-Engine ROOT] [-NoBuild] [-DryRun] [-- UNREAL_ARGS...]
-- SCENE: an .okscene, an OK scene card (PNG) or a KStudio scene PNG to open (none: a new scene). Option names are
-- case-insensitive and may also be written --window-width, ... ; -Name:value works too.
--
-- Like the card reader, the editor build must match the shared engine (Tools/ok_editor_modules.lua): when another
-- checkout rebuilt the engine, OpenKoikatsuEditor is rebuilt here first unless -NoBuild is given.
local ok = require "ok"
local json, path, proc = ok.json, ok.path, ok.proc
local editor = require "ok_editor_modules"

local SWITCHES = { nobuild = true, dryrun = true }
local VALUES = { scene = true, windowwidth = true, windowheight = true, engine = true }

local function parse(argv)
  local o, i = {}, 1
  o.passthrough = {}
  while i <= #argv do
    local raw = argv[i]
    if raw == "--" then table.move(argv, i + 1, #argv, 1, o.passthrough); break end
    local name, inline = raw:match("^%-%-?([%w%-]+):(.*)$")
    if not name then name = raw:match("^%-%-?([%w%-]+)$") end
    if not name then
      -- A bare argument is the scene (drag a file onto SceneEditor.cmd).
      if o.scene then error("unexpected argument: " .. raw, 0) end
      o.scene = raw
    else
      local key = name:gsub("%-", ""):lower()
      if SWITCHES[key] then o[key] = true
      elseif VALUES[key] then
        local v = inline
        if v == nil then i = i + 1; v = argv[i] end
        if v == nil then error("missing value for " .. raw, 0) end
        o[key] = v
      else error("unknown option: " .. raw, 0) end
    end
    i = i + 1
  end
  return o
end

local function size(o, key, default, lo, hi)
  local v = o[key] and tonumber(o[key]) or default
  if not v or v ~= math.floor(v) or v < lo or v > hi then error(("%s must be an integer %d..%d"):format(key, lo, hi), 0) end
  return v
end

local function main()
  local o = parse(arg)
  local root = ok.root
  local engine = o.engine and path.abs(o.engine) or ok.engine_root()
  local width = size(o, "windowwidth", 1600, 640, 7680)
  local height = size(o, "windowheight", 1000, 480, 4320)
  local scene = o.scene
  if scene then
    scene = path.abs(scene)
    if not path.is_file(scene) then error("Scene file does not exist: " .. scene, 0) end
  end

  editor.ensure(root, engine, { build = not o.nobuild and not o.dryrun })

  local project = path.join(root, "OpenKoikatsu.uproject")
  local exe = path.join(engine, "Engine/Binaries/Win64/UnrealEditor.exe")
  for _, f in ipairs({ project, exe }) do
    if not path.is_file(f) then error("Missing launch dependency: " .. f, 0) end
  end

  local log_root = path.join(root, "Saved/Logs/SceneEditor")
  local tag = os.date("%Y%m%d-%H%M%S") .. ("-%08x"):format(math.random(0, 0x7fffffff))
  local log = path.join(log_root, tag .. ".log")
  -- Explicit map and game mode: the project's default game mode is the card reader's.
  local args = { exe, project, "/Engine/Maps/Entry?game=/Script/OKStudio.OKStudioGameMode", "-game", "-windowed",
    "-ResX=" .. width, "-ResY=" .. height, "-NoSplash", "-nop4", "-abslog=" .. log }
  if scene then args[#args + 1] = "-OKScene=" .. scene end
  for _, a in ipairs(o.passthrough) do args[#args + 1] = a end
  local receipt = json.object({ { "project", project }, { "executable", exe }, { "arguments", json.array({ table.unpack(args, 2) }) },
    { "scene", scene or "" }, { "log", log } })
  if o.dryrun then io.write(json.encode(receipt, { indent = 2, ensure_ascii = false }), "\n"); return end

  path.mkdir(log_root, true)
  local okv, pid = pcall(proc.spawn, args, { cwd = root })
  if not okv then error(pid, 0) end
  receipt.pid = pid
  path.write_text(path.join(log_root, tag .. ".json"), json.encode(receipt, { indent = 2, ensure_ascii = false }))
  io.write("Scene editor started. PID=", tostring(pid), "\nLog: ", log, "\n")
end

ok.main(main)
