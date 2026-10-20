-- Generates the face parents the StreetSocial scene loads beside the shared runtime parents, into every material root
-- named by Content/IndependentResources/StreetSocialData/Launch/materials.json (root, hg_root), so
-- Tools/rebuild_street_social_patch.lua cooks them with the other runtime parents:
--   M_OKTearLit (PrepareTearMaterial.cpp)      the lit tears (cf_O_namida_S/M/L)
--   <Parent>_Fg (PrepareForegroundMaterial.cpp) eyes / eyebrows drawn through the front hair (the card's foreground
--                                               setting, as Koikatsu's stencil does)
--
--   Tools\lua\lua.exe Tools\prepare_face_materials.lua
--
-- Uses the material bake host (Saved/Crowd/RuntimeMaterialBakeHost, laid out as Tools/prepare_runtime_shared_material.lua
-- does) and this checkout's engine (ok.engine_root()). Existing assets are kept.
local ok = require "ok"
local json, path, proc = ok.json, ok.path, ok.proc

local root = path.abs(path.join(path.dirname(path.abs(arg[0])), ".."))
local function say(s) io.write(s, "\n"); io.flush() end

local engine = ok.engine_root()
-- Every render style's root (root, hg_root): the scene loads its face parents from the root it draws with.
local materials = json.decode(path.read_text(path.join(root, "Content/IndependentResources/StreetSocialData/Launch/materials.json")))
local roots, todo = {}, {}
for _, r in ipairs({ materials.root, materials.hg_root }) do
  if r then
    table.insert(roots, r)
    local material = r .. "/M_OKTearLit"
    local asset = path.join(root, "Content", material:sub(7) .. ".uasset")
    if path.exists(asset) then say("exists " .. asset) else table.insert(todo, { material = material, asset = asset }) end
  end
end

-- The bake host: this checkout's tool and plugin sources (Content and Tools are junctions to the checkout).
local host = path.join(root, "Saved/Crowd/RuntimeMaterialBakeHost")
path.mkdir(host, true)
for _, part in ipairs({ "Source", "Config" }) do path.copytree(path.join(root, "Tools/GpuPoseAudit", part), path.join(host, part)) end
-- The tool's plugins and, through their .uplugin, every project plugin they need (KKRuntimePose -> OKPhysXSecondary
-- -> KawaiiPhysics); engine plugins are the engine's.
local plugins, seen = {}, {}
local function want(name)
  if seen[name] or not path.exists(path.join(root, "Plugins", name, name .. ".uplugin")) then return end
  seen[name] = true; table.insert(plugins, name)
  local desc = json.decode(path.read_text(path.join(root, "Plugins", name, name .. ".uplugin")))
  for _, dep in ipairs(desc.Plugins or {}) do want(dep.Name) end
end
for _, name in ipairs({ "KKCrowdGpuPacking", "KKImportedAssets", "KKRuntimePose", "OKHGShading" }) do want(name) end
for _, plugin in ipairs(plugins) do
  local dst = path.join(host, "Plugins", plugin)
  path.mkdir(dst, true)
  path.copy(path.join(root, "Plugins", plugin, plugin .. ".uplugin"), path.join(dst, plugin .. ".uplugin"))
  for _, part in ipairs({ "Source", "Shaders", "Resources" }) do
    local src = path.join(root, "Plugins", plugin, part)
    if path.exists(src) then path.copytree(src, path.join(dst, part)) end
  end
end
for _, part in ipairs({ "Tools", "Content" }) do
  local link = path.join(host, part)
  if not path.exists(link) then proc.run(('mklink /J "%s" "%s"'):format(link:gsub("/", "\\"), path.join(root, part):gsub("/", "\\")), { check = true, quiet = true }) end
end
local project = path.join(host, "RuntimeMaterialBake.uproject")
if not path.exists(project) then
  local p = json.decode(path.read_text(path.join(root, "Tools/GpuPoseAudit/GpuPoseAudit.uproject")))
  p.Plugins = p.Plugins or {}
  table.insert(p.Plugins, { Name = "KKRuntimePose", Enabled = true })
  table.insert(p.Plugins, { Name = "OKHGShading", Enabled = true })
  path.write_text(project, json.encode(p, { indent = 2 }))
end

local stamp = os.date("%Y%m%d%H%M%S")
local log = path.join(root, "Saved/Crowd/FaceMaterials-" .. stamp)
say("build GpuPoseAuditEditor (" .. engine .. ")")
-- -NoEngineChanges -UsePrecompiled: the engine is used as prebuilt, never rewritten (this host's plugin set differs
-- from the game's; without them UBT relinked the shared engine binaries, NetCore among them, for every other build).
local built = proc.run({ path.join(engine, "Engine/Build/BatchFiles/Build.bat"), "GpuPoseAuditEditor", "Win64", "Development",
  "-Project=" .. project, "-NoEngineChanges", "-UsePrecompiled", "-NoHotReloadFromIDE", "-WaitMutex", "-MaxParallelActions=8" }, { stdout_file = log .. ".build.log", merge = true })
if not built.ok then error("Face material tool build failed: " .. log .. ".build.log", 0) end

local function commandlet(args, out, what)
  local cmd = { path.join(engine, "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), project, "-run=GpuPoseAudit",
    "-AllowCommandletRendering", "-unattended", "-nosound", "-NoSplash", "-AssetRoot=" .. path.join(root, "Content"), "-Output=" .. out .. ".json" }
  for _, a in ipairs(args) do table.insert(cmd, a) end
  local run = proc.run(cmd, { stdout_file = out .. ".log", merge = true, timeout = 900 })
  if not run.ok or not path.exists(out .. ".json") then error(what .. " failed: " .. out .. ".log", 0) end
end
for k, each in ipairs(todo) do
  say("generate " .. each.material)
  commandlet({ "-PrepareTearMaterial", "-MaterialPath=" .. each.material }, log .. "-tear" .. k, "Tear material generation")
  say("ok " .. each.asset)
end
-- The foreground variants of every translucent shared parent; the commandlet keeps the ones already there.
for k, r in ipairs(roots) do
  say("foreground parents " .. r)
  local out = log .. "-fg" .. k
  commandlet({ "-PrepareForegroundMaterials", "-MaterialRoot=" .. r }, out, "Foreground material generation")
  local report = json.decode(path.read_text(out .. ".json"))
  say(("made %d, kept %d"):format(#(report.made or {}), #(report.skipped or {})))
end
say("next: Tools\\lua\\lua.exe Tools\\rebuild_street_social_patch.lua")
