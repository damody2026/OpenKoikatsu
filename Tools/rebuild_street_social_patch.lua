-- Rebuilds the StreetSocial runtime patch (Saved/StreetSocial/Build/<patch> from Launch/runtime.json) from Content only,
-- so Saved/ can be wiped:
--
--   1. RuntimeMaterialPatch-<tag>_P.pak: cooks the shared runtime materials already in Content
--      (Launch/materials.json root, e.g. Content/IndependentResources/RuntimeMaterials/StreetDisplay24) together with
--      their shader library. The materials are not regenerated, so Content stays untouched.
--   2. RuntimeAudioPatch-<tag>_P.pak: the engine audio settings packages (Tools/cook_runtime_audio_patch.lua).
--
-- Intermediates go under Saved/StreetSocial/PatchRebuild-<stamp>; the launcher runs this when the patch is missing.
--
--   Tools\lua\lua.exe Tools\rebuild_street_social_patch.lua [--engine DIR] [--output DIR]
-- The last stdout line is the patch folder.
local ok = require "ok"
local json, path, proc, util = ok.json, ok.path, ok.proc, ok.util
local lfs = require "lfs"
local shared_material = require "prepare_runtime_shared_material"

local function win(p) return (path.norm(p):gsub("/", "\\")) end
local function step(name) io.write("[", os.date("%H:%M:%S"), "] ", name, "\n"); io.flush() end

-- Files below dir, recursive, without following links (Get-ChildItem -File -Recurse); missing dir -> {}.
local function files_below(dir, out)
  out = out or {}
  if not path.is_dir(dir) then return out end
  for _, n in ipairs(path.list(dir)) do
    local full = path.join(dir, n)
    local attr = lfs.symlinkattributes(full)
    if attr and attr.mode == "directory" then files_below(full, out) elseif attr and attr.mode == "file" then out[#out + 1] = full end
  end
  return out
end

local function main()
  local p = ok.args.parser("Rebuild the StreetSocial runtime patch from Content")
  p:opt("--engine", { default = ok.engine_root() })
  p:opt("--output", { default = "" })
  local a = p:parse(arg)
  local engine = path.norm(a.engine)
  local root = ok.root
  local launch = json.decode(path.read_text(path.join(root, "Content/IndependentResources/StreetSocialData/Launch/runtime.json")))
  local materials = json.decode(path.read_text(path.join(root, launch.materials)))
  local material_root = util.str(materials.root)
  -- Every render style's parents go into the same patch and shader library (hg_root: the HG style, Plugins/OKHGShading).
  local material_roots = { material_root }
  if type(materials.hg_root) == "string" and materials.hg_root ~= "" then material_roots[#material_roots + 1] = materials.hg_root end
  local material_tag = material_root:match("([^/]*)$")
  local assets = {}
  for _, each in ipairs(material_roots) do
    if not util.startswith(each, "/Game/IndependentResources/RuntimeMaterials/") then error("Unexpected material root: " .. each, 0) end
    local each_assets = 0
    for _, f in ipairs(files_below(path.join(root, "Content/" .. each:sub(7)))) do
      if path.suffix(f):lower() == ".uasset" then assets[#assets + 1] = f; each_assets = each_assets + 1 end
    end
    if each_assets == 0 then error("No runtime materials in " .. each, 0) end
  end
  local patch = a.output ~= "" and path.abs(a.output) or path.join(root, launch.patch)
  local stamp = os.date("%Y%m%d%H%M%S")
  local work = path.join(root, "Saved/StreetSocial/PatchRebuild-" .. stamp)
  path.mkdir(work)

  -- --- 1. material bake host (same layout as Tools/prepare_runtime_shared_material.lua), editor build ---
  step("material cook host")
  local bake = shared_material.prepare_bake_host()
  local custom = path.join(bake.host, "Config/Custom/CrowdCookSM6")
  path.mkdir(custom)
  path.copy(path.join(root, "Config/Custom/CrowdCookSM6/DefaultEngine.ini"), path.join(custom, "DefaultEngine.ini"))
  if not shared_material.build_bake_host(engine, bake.project, path.join(work, "material-host.build.log")) then
    error("material cook host build failed: " .. win(path.join(work, "material-host.build.log")), 0)
  end

  -- --- 2. cook the Content materials and their shader library ---
  step("cook runtime materials " .. material_tag .. " (" .. #assets .. " assets)")
  local cooked = path.join(work, "material.cook")
  local cook_args = { '"' .. win(bake.project) .. '"', "-run=Cook", "-TargetPlatform=Windows", "-MAP=/Engine/Maps/Entry", "-NoDefaultMaps",
    "-NoGameAlwaysCook", "-SkipZenStore", "-CustomConfig=CrowdCookSM6", "-unattended", "-nosound", "-NoSplash", "-corelimit=9",
    '-OutputDir="' .. win(cooked) .. '"', '-abslog="' .. win(path.join(work, "material.cook.log")) .. '"' }
  for _, each in ipairs(material_roots) do cook_args[#cook_args + 1] = '-COOKDIR="' .. win(path.join(bake.host, "Content/" .. each:sub(7))) .. '"' end
  -- Like Start-Process: the arguments are joined verbatim (they carry their own quotes), hidden window, output discarded.
  local line = '"' .. win(path.join(engine, "Engine/Binaries/Win64/UnrealEditor-Cmd.exe")) .. '"' .. " " .. table.concat(cook_args, " ")
  local cook = proc.run(line, { shell = false, hide = true, quiet = true, timeout = 30 * 60 })
  if cook.error == "timeout" then error("material cook timeout", 0) end
  if not cook.ok then error("material cook failed: " .. win(path.join(work, "material.cook.log")), 0) end
  local cook_content = path.join(cooked, "RuntimeMaterialBake/Content")
  local files, uassets, archives = {}, 0, 0
  for _, each in ipairs(material_roots) do
    local dir = path.join(cook_content, each:sub(7))
    if not path.is_dir(dir) then error("Cooked material folder missing: " .. win(dir), 0) end
    for _, f in ipairs(files_below(dir)) do files[#files + 1] = f end
  end
  if path.is_dir(cook_content) then
    for _, f in ipairs(path.iterdir(cook_content)) do
      if path.is_file(f) and path.fnmatch(path.name(f), "ShaderArchive-RuntimeMaterialBake-*") then files[#files + 1] = f; archives = archives + 1 end
    end
  end
  for _, f in ipairs(files) do if path.suffix(f):lower() == ".uasset" then uassets = uassets + 1 end end
  if uassets < #assets then error("cook omitted runtime material packages", 0) end
  if archives == 0 then error("cook omitted the runtime material shader library", 0) end
  local response = path.join(work, "material.response.txt")
  local lines = {}
  for _, f in ipairs(files) do
    lines[#lines + 1] = '"' .. win(f) .. '" "../../../OpenKoikatsu/Content/' .. f:sub(#cook_content + 2) .. '"'
  end
  path.write_text(response, table.concat(lines, "\n") .. "\n", { bom = true })
  local material_pak = path.join(work, "RuntimeMaterialPatch-" .. material_tag .. "_P.pak")
  local pak = proc.run({ win(path.join(engine, "Engine/Binaries/Win64/UnrealPak.exe")), win(material_pak), "-Create=" .. win(response) },
    { stdout_file = path.join(work, "material.pak.log"), merge = true })
  if not pak.ok then error("material pak failed", 0) end

  -- --- 3. engine audio settings ---
  step("cook audio settings")
  local audio = proc.lua(path.join(root, "Tools/cook_runtime_audio_patch.lua"), { "--tag", "Street" .. stamp, "--engine", engine },
    { stdout_only = true })
  local audio_pak
  for l in (audio.stdout or ""):gmatch("[^\n]+") do audio_pak = l end
  if not audio.ok or not audio_pak or not path.exists(audio_pak) then error("audio patch failed", 0) end

  -- --- 4. the patch folder the launcher mounts ---
  step("patch -> " .. win(patch))
  path.mkdir(patch)
  for _, f in ipairs(path.glob(patch, "Runtime*Patch-*_P.pak")) do if path.is_file(f) then path.remove(f) end end
  path.copy(material_pak, path.join(patch, path.name(material_pak)))
  path.copy(audio_pak, path.join(patch, path.name(audio_pak)))
  step("done")
  io.write(win(patch), "\n")
end

ok.main(main)
