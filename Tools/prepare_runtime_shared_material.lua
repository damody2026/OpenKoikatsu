-- Authors the shared runtime materials under /Game/IndependentResources/RuntimeMaterials/<tag> with the GpuPoseAudit
-- commandlet (-PrepareRuntimeSharedMaterial) in the material bake host Saved/Crowd/RuntimeMaterialBakeHost, and checks
-- the gates of its Saved/Crowd/RuntimeSharedMaterial-<tag>.json evidence. Prints the evidence path as the last line.
-- --hg-shading authors the HG render style parents (Plugins/OKHGShading) instead of the Koikatsu ones.
--
--   Tools\lua\lua.exe Tools\prepare_runtime_shared_material.lua --tag StreetHG01 [--hg-shading] [--engine DIR]
-- require "prepare_runtime_shared_material" gives M.prepare_bake_host() (also used by rebuild_street_social_patch.lua).
local ok = require "ok"
local json, path, proc = ok.json, ok.path, ok.proc
local lfs = require "lfs"

local function win(p) return (path.norm(p):gsub("/", "\\")) end

local M = {}

-- Lays out the material bake host (GpuPoseAudit Source/Config, the material plugins, Tools/Content junctions,
-- RuntimeMaterialBake.uproject) and returns {host=, project=}. junction_error: message when a link is wrong.
function M.prepare_bake_host(junction_error)
  local root, tools = ok.root, path.join(ok.root, "Tools")
  local host = path.join(root, "Saved/Crowd/RuntimeMaterialBakeHost")
  path.mkdir(host)
  for _, part in ipairs({ "Source", "Config" }) do path.copytree(path.join(tools, "GpuPoseAudit/" .. part), path.join(host, part)) end
  -- The material plugins and every project plugin they depend on (.uplugin "Plugins", recursively; engine plugins
  -- come with the engine): a new dependency (KKRuntimePose -> OKPhysXSecondary -> KawaiiPhysics) needs no edit here.
  local plugins, seen = {}, {}
  local function want(name)
    if seen[name] then return end
    seen[name] = true
    local descriptor = path.join(root, "Plugins/" .. name .. "/" .. name .. ".uplugin")
    if not path.is_file(descriptor) then return end
    plugins[#plugins + 1] = name
    for _, dep in ipairs(json.decode((path.read_text(descriptor):gsub("^\239\187\191", ""))).Plugins or {}) do want(dep.Name) end
  end
  for _, plugin in ipairs({ "KKCrowdGpuPacking", "KKImportedAssets", "KKRuntimePose", "OKHGShading" }) do want(plugin) end
  for _, plugin in ipairs(plugins) do
    local destination = path.join(host, "Plugins/" .. plugin)
    path.mkdir(destination)
    path.copy(path.join(root, "Plugins/" .. plugin .. "/" .. plugin .. ".uplugin"), path.join(destination, plugin .. ".uplugin"))
    path.copytree(path.join(root, "Plugins/" .. plugin .. "/Source"), path.join(destination, "Source"))
    local shaders = path.join(root, "Plugins/" .. plugin .. "/Shaders")
    if path.exists(shaders) then path.copytree(shaders, path.join(destination, "Shaders")) end
    -- Prebuilt third-party libraries a plugin stages at runtime (OKPhysXSecondary's PhysX DLLs).
    local third_party = path.join(root, "Plugins/" .. plugin .. "/Binaries/ThirdParty")
    if path.exists(third_party) then path.copytree(third_party, path.join(destination, "Binaries/ThirdParty")) end
  end
  for _, part in ipairs({ "Tools", "Content" }) do
    local link, target = path.join(host, part), path.join(root, part)
    if not path.exists(link) then proc.run({ "mklink", "/J", win(link), win(target) }, { quiet = true, check = true }) end
    local attr = lfs.symlinkattributes(link)
    local linked = attr and attr.mode == "link" and attr.target and path.abs((attr.target:gsub("^\\\\%?\\", "")))
    if not linked or linked:lower() ~= path.abs(target):lower() then error(junction_error or ("Unexpected junction: " .. win(link)), 0) end
  end
  local project = json.decode(path.read_text(path.join(tools, "GpuPoseAudit/GpuPoseAudit.uproject")))
  project.Plugins[#project.Plugins + 1] = json.object({ { "Name", "KKRuntimePose" }, { "Enabled", true } })
  project.Plugins[#project.Plugins + 1] = json.object({ { "Name", "OKHGShading" }, { "Enabled", true } })
  local project_path = path.join(host, "RuntimeMaterialBake.uproject")
  path.write_text(project_path, json.encode(project, { indent = 4 }) .. "\n", { bom = true })
  return { host = host, project = project_path }
end

-- Editor build of the bake host (GpuPoseAuditEditor) with output to log; true on success.
function M.build_bake_host(engine, project_path, log)
  return proc.run({ win(path.join(engine, "Engine/Build/BatchFiles/Build.bat")), "GpuPoseAuditEditor", "Win64", "Development",
    "-Project=" .. win(project_path), "-NoEngineChanges", "-NoHotReloadFromIDE", "-UsePrecompiled", "-WaitMutex", "-MaxParallelActions=8" },
    { stdout_file = log, merge = true }).ok
end

local function main()
  local p = ok.args.parser("Prepare the shared runtime materials")
  p:opt("--tag", { required = true })
  p:opt("--engine", { default = ok.engine_root() })
  p:flag("--hg-shading")
  local a = p:parse(arg)
  if not a.tag:match("^[%w_%-]+$") then error("--tag must match ^[A-Za-z0-9_-]+$", 0) end
  local engine = path.norm(a.engine)
  local root = ok.root
  local base = path.join(root, "Saved/Crowd/RuntimeSharedMaterial-" .. a.tag)
  for _, suffix in ipairs({ ".json", ".log", ".build.log", ".pid" }) do
    if path.exists(base .. suffix) then error("Fresh material evidence tag required", 0) end
  end
  local bake = M.prepare_bake_host("Unexpected material tool junction")
  if not M.build_bake_host(engine, bake.project, base .. ".build.log") then
    error("Shared material tool build failed: " .. win(base) .. ".build.log", 0)
  end
  local args = { '"' .. win(bake.project) .. '"', "-run=GpuPoseAudit", "-PrepareRuntimeSharedMaterial", "-AllowCommandletRendering",
    "-unattended", "-nosound", "-NoSplash", '-AssetRoot="' .. win(root) .. '/Content"',
    "-MaterialRoot=/Game/IndependentResources/RuntimeMaterials/" .. a.tag, '-Output="' .. win(base) .. '.json"',
    '-abslog="' .. win(base) .. '.log"' }
  if a.hg_shading then args[#args + 1] = "-HGShading" end
  local line = '"' .. win(path.join(engine, "Engine/Binaries/Win64/UnrealEditor-Cmd.exe")) .. '"' .. " " .. table.concat(args, " ")
  local child = proc.popen(line, { shell = false, hide = true, stdout = "NUL", stderr = "NUL" })
  path.write_text(base .. ".pid", tostring(child:pid()) .. "\n")
  local code = child:wait(10 * 60)
  if code == nil then child:kill(); error("Owned shared material preparation timeout", 0) end
  if code ~= 0 then error("Shared material preparation failed: " .. win(base) .. ".log", 0) end
  local result = json.decode(path.read_text(base .. ".json"))
  local t = ok.util.truthy
  if not t(result.passed) or not t(result.source_mesh_shader_permutations_verified) or t(result.per_card_input_used)
    or t(result.runtime_compilation_required) or result.schema ~= 1 then
    error("Shared material generation or exact source mesh shader gates failed", 0)
  end
  io.write(win(base) .. ".json", "\n")
end

if ok.is_main() then ok.main(main) end
return M
