-- Rebuilds a runnable StreetSocial stage from Content + Source only, so everything under Saved/ stays disposable.
--
--   1. Interaction host (Saved/Interaction/FreeTalk01Host) and its Development game executable
--   2. Cook of the StreetSocial map, the always-cook directories and the engine's renderer packages (main project)
--   3. UAT staging with -pak into --base-dir (default Saved/StreetSocial/Base): Windows/Engine and OpenKoikatsu/Content/Paks.
--      Running stages link to their base, so a different engine build goes to a new --base-dir (old stages keep working).
--   4. A stage under Saved/StreetSocial/Build/<Tag>.stage (stage_interaction_runtime.lua links Paks/Engine from Base)
--
-- The StreetSocial/material/audio patch (Launch/runtime.json patch, e.g. Saved/StreetSocial/Build/StreetDisplay25HG.patch)
-- is mounted on top; the base pak carries the map and the older StreetDisplay23 materials only, so the current material
-- roots (Launch/materials.json) come from the patch, which the launcher rebuilds when it is missing.
--
--   Tools\lua\lua.exe Tools\rebuild_street_social_runtime.lua --tag HeroineMind13
--     [--engine DIR] [--skip-exe] [--skip-cook] [--skip-base] [--base-dir Saved/StreetSocial/Base]
-- Logs go to Saved/StreetSocial/Rebuild-<tag>/; the last stdout line is the staged executable.
local ok = require "ok"
local path, proc = ok.path, ok.proc

local function win(p) return (path.norm(p):gsub("/", "\\")) end
local function step(name) io.write("[", os.date("%H:%M:%S"), "] ", name, "\n"); io.flush() end

-- True when some line of an ini file matches the (case-insensitive) Lua pattern.
local function has_line(file, pattern)
  for _, line in ipairs(path.read_lines(file)) do
    if line:lower():match(pattern) then return true end
  end
  return false
end

local function main()
  local p = ok.args.parser("Rebuild a runnable StreetSocial stage from Content + Source")
  p:opt("--tag", { required = true })
  p:opt("--engine", { default = ok.engine_root() })
  p:flag("--skip-exe")
  p:flag("--skip-cook")
  p:flag("--skip-base")
  p:opt("--base-dir", { default = "Saved/StreetSocial/Base" })
  local a = p:parse(arg)
  if not a.tag:match("^[%w_%-]+$") then error("--tag must match ^[A-Za-z0-9_-]+$", 0) end
  local engine = path.norm(a.engine)
  local root = ok.root
  local host_path = "Saved/Interaction/FreeTalk01Host"
  local host_dir = path.join(root, host_path)
  local logs = path.join(root, "Saved/StreetSocial/Rebuild-" .. a.tag)
  path.mkdir(logs)

  if not a.skip_exe then
    step("host + game executable")
    local prep = proc.lua(path.join(root, "Tools/prepare_interaction_runtime_host.lua"), { "--host-path", host_path },
      { stdout_file = path.join(logs, "prepare.log"), merge = true })
    if not prep.ok then error("host preparation failed: " .. win(path.join(logs, "prepare.log")), 0) end
    local build = proc.run({ win(path.join(engine, "Engine/Build/BatchFiles/Build.bat")), "OpenKoikatsu", "Win64", "Development",
      "-Project=" .. win(path.join(host_dir, "OpenKoikatsu.uproject")), "-WaitMutex" },
      { stdout_file = path.join(logs, "build.log"), merge = true })
    if not build.ok then error("game build failed: " .. win(path.join(logs, "build.log")), 0) end
  end
  if not a.skip_cook then
    step("cook (main project -> host Saved/Cooked)")
    local cooked = path.join(host_dir, "Saved/Cooked/[Platform]")
    local cook_args = { '"' .. win(root) .. '/OpenKoikatsu.uproject"', "-run=Cook", "-TargetPlatform=Windows",
      "-MAP=/Game/Scenes/StreetSocial/StreetSocial", "-SkipZenStore", "-CustomConfig=CrowdCookSM6",
      "-DisablePlugins=BpGeneratorUltimate", "-unattended", "-nosound", "-NoSplash", "-corelimit=24",
      "-PACKAGE=/Engine/EngineMaterials/FastBlueNoise_scalar_128x128x64+/Engine/EngineMaterials/FastBlueNoise_vec2_128x128x64+/Engine/EngineMaterials/FastBlueNoise_scalar_128x128x8+/Engine/EngineMaterials/Hair/Hair_DualScattering+/Engine/EngineMaterials/Hair/Hair_DirectionalAlbedo+/Engine/EngineMaterials/Hair/Hair_Coverage",
      '-OutputDir="' .. win(cooked) .. '"', '-abslog="' .. win(path.join(logs, "cook.log")) .. '"' }
    -- Like Start-Process: the arguments are joined verbatim (they carry their own quotes), hidden window, output discarded.
    local line = '"' .. win(path.join(engine, "Engine/Binaries/Win64/UnrealEditor-Cmd.exe")) .. '"' .. " " .. table.concat(cook_args, " ")
    local cook = proc.run(line, { shell = false, hide = true, quiet = true })
    if not cook.ok then error("cook failed: " .. win(path.join(logs, "cook.log")), 0) end
    if not path.exists(path.join(host_dir, "Saved/Cooked/Windows/OpenKoikatsu/Content/Scenes/StreetSocial/StreetSocial.umap")) then
      error("cook did not produce the StreetSocial map", 0)
    end
  end
  if not a.skip_base then
    step("stage with pak -> " .. a.base_dir)
    local base = path.abs(path.join(root, a.base_dir))
    local inside = root .. "/Saved/StreetSocial/"
    if base:sub(1, #inside) ~= inside then error("BaseDir must be inside Saved/StreetSocial", 0) end
    -- The host carries several editor targets; UAT needs to be told which one (staging only reads its receipt).
    local host_ini = path.join(host_dir, "Config/DefaultEngine.ini")
    if not has_line(host_ini, "^defaulteditortarget=") then
      path.append_text(host_ini, "\r\n[/Script/BuildSettings.BuildSettings]\r\nDefaultEditorTarget=OpenKoikatsuEditor\r\n", { newline = "" })
    end
    -- Plain .pak (no IoStore/Zen): the StreetSocial patch is a legacy pak, and IoStore builds never mount legacy paks.
    local host_game = path.join(host_dir, "Config/DefaultGame.ini")
    if not has_line(host_game, "^buseiostore=false") then
      path.append_text(host_game, "\r\n[/Script/UnrealEd.ProjectPackagingSettings]\r\nbUseIoStore=False\r\nbUseZenStore=False\r\nbUsePakFile=True\r\nIncludeDebugFiles=False\r\n",
        { newline = "" })
    end
    local old_paks = path.join(base, "Windows/OpenKoikatsu/Content/Paks")
    if path.is_dir(old_paks) then
      for _, f in ipairs(path.iterdir(old_paks)) do if path.is_file(f) then path.remove(f) end end
    end
    local uat = proc.run({ win(path.join(engine, "Engine/Build/BatchFiles/RunUAT.bat")), "BuildCookRun",
      "-project=" .. win(path.join(host_dir, "OpenKoikatsu.uproject")), "-nop4", "-nocompile", "-nocompileeditor", "-skipbuild",
      "-skipcook", "-stage", "-pak", "-platform=Win64", "-clientconfig=Development", "-stagingdirectory=" .. win(base) },
      { stdout_file = path.join(logs, "stage-base.log"), merge = true })
    if not uat.ok then error("base staging failed: " .. win(path.join(logs, "stage-base.log")), 0) end
    if #path.glob(path.join(base, "Windows/OpenKoikatsu/Content/Paks"), "*.pak") == 0 then error("base staging produced no pak", 0) end
  end
  step("stage " .. a.tag)
  local staged = proc.lua(path.join(root, "Tools/stage_interaction_runtime.lua"), { "--host-path", host_path,
    "--destination", "Saved/StreetSocial/Build/" .. a.tag .. ".stage", "--base-dir", a.base_dir },
    { stdout_file = path.join(logs, "stage.log"), merge = true })
  if not staged.ok then error("stage failed: " .. win(path.join(logs, "stage.log")), 0) end
  step("done")
  io.write(win(path.join(root, "Saved/StreetSocial/Build/" .. a.tag .. ".stage/Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe")), "\n")
end

ok.main(main)
