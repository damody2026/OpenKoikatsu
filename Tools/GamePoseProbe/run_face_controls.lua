-- Stage282 face controls in Unreal: --mode Tests runs the animation catalog / face blend shape automation tests
-- (UnrealEditor-Cmd, NullRHI), --mode Shots captures five installed-game cards with fixed mouth/eye/brow
-- patterns, --mode Gallery opens the gallery on March 7th and runs ok.VerifyAnimationCatalog.
--   Tools\lua\lua.exe Tools\GamePoseProbe\run_face_controls.lua --mode Tests|Shots|Gallery
-- Logs go to Saved/Logs/<stem>.log; the exit code is the engine's.
local ok = require "ok"
local fs = ok.fs

local function win(p) return (tostring(p):gsub("/", "\\")) end

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

local function main()
  local p = ok.args.parser("Stage282 face control tests, shots and gallery check")
  p:opt("--mode", { required = true, type = function(v)
    for _, c in ipairs({ "Tests", "Shots", "Gallery" }) do if c:lower() == v:lower() then return v end end
    error("--mode: invalid choice: " .. v .. " (choose from Tests, Shots, Gallery)", 0)
  end })
  local mode = p:parse(arg).mode
  local project = win(ok.root)
  local project_file = project .. "\\OpenKoikatsu.uproject"
  local engine_bin = ok.engine_root() .. "/Engine/Binaries/Win64"
  setenv("OK_GPU_SKIN", "1")
  setenv("OK_SKELMESH", "1")
  local arguments = { project_file, "-unattended", "-nosound", "-NoSplash", "-nop4" }
  local function add(list) for _, v in ipairs(list) do arguments[#arguments + 1] = v end end
  local executable, stem, console_log
  local m = mode:lower()
  if m == "tests" then
    executable = engine_bin .. "/UnrealEditor-Cmd.exe"
    stem = "FaceControlsUnrealAutomation282Finalv4"
    local tests = { "OpenKoikatsu.AnimationCatalog.HandShapes", "OpenKoikatsu.AnimationCatalog.ImportedData",
      "OpenKoikatsu.AnimationCatalog.Playback", "OpenKoikatsu.FaceBlendShape.NativeWeights",
      "OpenKoikatsu.FaceBlendShape.SharedDeformation", "OpenKoikatsu.FaceBlendShape.NativeFaceControls" }
    add({ "-NullRHI", '-ExecCmds="Automation RunTests ' .. table.concat(tests, "+") .. '"',
      '-TestExit="Automation Test Queue Empty"', "-stdout", "-FullStdOutLogOutput" })
    console_log = project .. "\\Saved\\Logs\\" .. stem .. "Console.log"
  elseif m == "shots" then
    executable = engine_bin .. "/UnrealEditor.exe"
    stem = "FaceControlsShots282Finalv4"
    local card_root = ok.game_root() .. "/UserData/chara/female/"
    local names = { "[Card] March_7th_keiyuu.png", "Alice_keiyuu.png",
      "Hayase_yuuka_keiyuu_V1_02 For KKS.png", "KK_091207.png", "KK_384283.png" }
    local cards = {}
    for i, n in ipairs(names) do cards[i] = card_root .. n end
    setenv("OK_SHOT_CARDS", table.concat(cards, ";"))
    setenv("OK_OUTFITS_LIMIT", "1")
    setenv("OK_SHOT_OUTPUT_DIR", project .. "\\Saved\\DebugShots\\FaceControls282")
    setenv("OK_SHOT_STUDIO_ROW", "r00000022")
    setenv("OK_SHOT_ANIM_SCENE", "4")
    setenv("OK_SHOT_ANIM_TIME", "0.4")
    setenv("OK_SHOT_MOUTH_PATTERN", "8")
    setenv("OK_SHOT_MOUTH_OPEN", "0.8")
    setenv("OK_SHOT_EYES", "1")
    setenv("OK_SHOT_EYES_OPEN", "0.61337")
    setenv("OK_SHOT_BROW", "2")
    setenv("OK_SHOT_BROW_OPEN", "0.8")
    setenv("OK_SHOT_FACE_AUDIT", "1")
    setenv("OK_SHOT_ATTACHMENT_AUDIT", "1")
    setenv("OK_MOUTH_DISPLAY_AUDIT", "1")
    add({ "-game", "-windowed", "-ForceRes", "-ResX=900", "-ResY=1080", '-ExecCmds="ok.ShotPreview"' })
  else
    executable = engine_bin .. "/UnrealEditor.exe"
    stem = "FaceControlsGallery282Finalv2"
    setenv("OK_AUTO_3D", "1")
    setenv("OK_AUTO_CARD", "[Card] March_7th_keiyuu")
    setenv("OK_VERIFY_FACE_CONTROLS", "1")
    add({ "-game", "-windowed", "-ForceRes", "-ResX=1440", "-ResY=1000", '-ExecCmds="ok.VerifyAnimationCatalog"' })
  end
  add({ "-abslog=" .. project .. "\\Saved\\Logs\\" .. stem .. ".log" })
  local opts = { shell = false, cwd = project, hide = true, stdout = "NUL", stderr = "NUL" }
  if console_log then
    opts.stdout = console_log
    opts.stderr = project .. "\\Saved\\Logs\\" .. stem .. "Stderr.log"
  end
  local owned = ok.proc.popen(command_line(executable, arguments), opts)
  print(("%s PID %d"):format(mode, owned:pid()))
  local code = owned:wait()
  print(("%s exit %d"):format(mode, code))
  os.exit(code)
end

ok.main(main)
