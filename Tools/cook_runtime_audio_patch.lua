-- Cooks the engine-wide audio settings packages (Engine/Content/EngineSounds: Master and the default submixes) into
-- Saved/Crowd/RuntimeAudioPatch-<tag>_P.pak with an evidence .json beside it. Prints the pak path as the last line.
--
--   Tools\lua\lua.exe Tools\cook_runtime_audio_patch.lua --tag Street20261018120000 [--engine DIR]
local ok = require "ok"
local json, path, proc, hash = ok.json, ok.path, ok.proc, ok.hash
local lfs = require "lfs"

local function win(p) return (path.norm(p):gsub("/", "\\")) end

-- Files below dir, recursive, without following links (Get-ChildItem -File -Recurse).
local function files_below(dir, out)
  out = out or {}
  for _, n in ipairs(path.list(dir)) do
    local full = path.join(dir, n)
    local attr = lfs.symlinkattributes(full)
    if attr and attr.mode == "directory" then files_below(full, out) elseif attr and attr.mode == "file" then out[#out + 1] = full end
  end
  return out
end

-- Runs a command line verbatim (Start-Process style), writes its pid, waits up to `minutes`.
local function run_owned(line, pid_file, minutes, timeout_message)
  local child = proc.popen(line, { shell = false, hide = true, stdout = "NUL", stderr = "NUL" })
  path.write_text(pid_file, tostring(child:pid()) .. "\n")
  local code = child:wait(minutes * 60)
  if code == nil then child:kill(); error(timeout_message, 0) end
  return code
end

local function main()
  local p = ok.args.parser("Cook the engine audio settings patch")
  p:opt("--tag", { required = true })
  p:opt("--engine", { default = ok.engine_root() })
  local a = p:parse(arg)
  if not a.tag:match("^[%w_%-]+$") then error("--tag must match ^[A-Za-z0-9_-]+$", 0) end
  local engine = path.norm(a.engine)
  local base = path.join(ok.root, "Saved/Crowd/RuntimeAudioPatch-" .. a.tag)
  for _, suffix in ipairs({ ".host", ".cook", ".log", ".response.txt", "_P.pak", ".pak.log", ".json", ".pid" }) do
    if path.exists(base .. suffix) then error("Fresh audio Cook tag required", 0) end
  end
  -- These are engine-wide audio settings resources, never player/card assets.
  local source = path.join(engine, "Engine/Content/EngineSounds")
  local required = { "Master", "Submixes/MasterSubmixDefault", "Submixes/MasterReverbSubmixDefault", "Submixes/MasterEQSubmixDefault" }
  for _, package in ipairs(required) do
    if not path.exists(path.join(source, package .. ".uasset")) then error("Engine audio setting package missing: " .. package, 0) end
  end
  local host = base .. ".host"
  path.mkdir(path.join(host, "Config/Custom/CrowdCookSM6"))
  path.write_text(path.join(host, "GpuPoseAudit.uproject"), json.encode(json.object({ { "FileVersion", 3 }, { "EngineAssociation", "" },
    { "DisableEnginePluginsByDefault", true } }), { indent = 4 }) .. "\n", { bom = true })
  path.copy(path.join(ok.root, "Config/Custom/CrowdCookSM6/DefaultEngine.ini"), path.join(host, "Config/Custom/CrowdCookSM6/DefaultEngine.ini"))
  local args = { '"' .. win(host) .. '/GpuPoseAudit.uproject"', "-run=Cook", "-TargetPlatform=Windows", "-MAP=/Engine/Maps/Entry",
    "-NoDefaultMaps", "-NoGameAlwaysCook", "-SkipZenStore", "-CustomConfig=CrowdCookSM6", "-unattended", "-nosound", "-NoSplash",
    '-COOKDIR="' .. win(source) .. '"', '-OutputDir="' .. win(base) .. '.cook/[Platform]"', '-abslog="' .. win(base) .. '.log"' }
  local line = '"' .. win(path.join(engine, "Engine/Binaries/Win64/UnrealEditor-Cmd.exe")) .. '"' .. " " .. table.concat(args, " ")
  local code = run_owned(line, base .. ".pid", 10, "Owned audio Cook timeout")
  if code ~= 0 then error("Audio Cook failed: " .. win(base) .. ".log", 0) end
  local cook_audio = path.join(base .. ".cook/Windows", "Engine/Content/EngineSounds")
  for _, package in ipairs(required) do
    if not path.exists(path.join(cook_audio, package .. ".uasset")) then error("Cook omitted audio setting package: " .. package, 0) end
  end
  local lines = {}
  for _, f in ipairs(files_below(cook_audio)) do
    lines[#lines + 1] = '"' .. win(f) .. '" "../../../Engine/Content/EngineSounds/' .. f:sub(#cook_audio + 2) .. '"'
  end
  path.write_text(base .. ".response.txt", table.concat(lines, "\n") .. "\n", { bom = true })
  local pak = proc.run({ win(path.join(engine, "Engine/Binaries/Win64/UnrealPak.exe")), win(base) .. "_P.pak", "-Create=" .. win(base) .. ".response.txt" },
    { stdout_file = base .. ".pak.log", merge = true })
  if not pak.ok then error("Audio patch Pak failed", 0) end
  local pak_file = base .. "_P.pak"
  path.write_text(base .. ".json", json.encode(json.object({ { "schema", 1 }, { "passed", true }, { "common_only", true },
    { "per_card_assets", false }, { "required_packages", json.array(required) }, { "pak", win(pak_file) },
    { "pak_sha256", hash.sha256_file(pak_file):upper() }, { "pak_bytes", path.size(pak_file) } }), { indent = 4 }) .. "\n", { bom = true })
  io.write(win(pak_file), "\n")
end

ok.main(main)
