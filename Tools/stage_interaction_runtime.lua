-- Stages an Interaction host's Development game build under Saved/StreetSocial/Build/<name>.stage: Paks and the engine
-- folders are junctions into --base-dir, IndependentResources and KKPreview junctions into Content, the receipt's plugin
-- descriptors (with Resources/Shaders) and project plugin runtime files are copied, and the plugin manifest is written;
-- then stage_street_social.lua links the rest of the base and copies the executable. Prints the staged executable path.
--
--   Tools\lua\lua.exe Tools\stage_interaction_runtime.lua [--host-path Saved/Interaction/FreeTalk01Host]
--     [--destination Saved/StreetSocial/Build/X.stage] [--base-dir Saved/StreetSocial/Base]
local ok = require "ok"
local json, path, proc, util = ok.json, ok.path, ok.proc, ok.util
local stage_street_social = require "stage_street_social"

local function win(p) return (path.norm(p):gsub("/", "\\")) end
local function junction(link, target)
  proc.run({ "mklink", "/J", win(link), win(target) }, { quiet = true, check = true })
end

local function main()
  local p = ok.args.parser("Stage an Interaction host build")
  p:opt("--host-path", { default = "Saved/Interaction/RuntimeHost" })
  p:opt("--destination", { default = "Saved/StreetSocial/Build/SharedInteraction28.stage" })
  p:opt("--base-dir", { default = "Saved/StreetSocial/Base" })
  local a = p:parse(arg)
  local project_root = ok.root
  local host_root = path.abs(path.join(project_root, a.host_path)):gsub("/+$", "")
  local stage = path.abs(path.join(project_root, a.destination)):gsub("/+$", "")
  local prefix = project_root .. "/Saved/StreetSocial/Build/"
  if stage:sub(1, #prefix) ~= prefix then error("Stage target must be inside Saved/StreetSocial/Build", 0) end
  local engine_root = ok.engine_root() .. "/Engine"
  local engine_stage = path.join(stage, "Windows/Engine")
  local project_content = path.join(stage, "Windows/OpenKoikatsu/Content")
  if not path.exists(project_content) then
    path.mkdir(project_content)
    junction(path.join(project_content, "Paks"), path.join(project_root, a.base_dir .. "/Windows/OpenKoikatsu/Content/Paks"))
    junction(path.join(project_content, "IndependentResources"), path.join(project_root, "Content/IndependentResources"))
  end
  path.mkdir(engine_stage)
  local base_engine = path.join(project_root, a.base_dir .. "/Windows/Engine")
  if not path.is_dir(base_engine) then error("Base engine folder missing: " .. win(base_engine), 0) end
  for _, folder in ipairs(path.list(base_engine)) do
    local full = path.join(base_engine, folder)
    if path.is_dir(full) then
      local target = path.join(engine_stage, folder)
      if not path.exists(target) then junction(target, full) end
    end
  end
  local lut_target = path.join(project_content, "KKPreview")
  if not path.exists(lut_target) then junction(lut_target, path.join(project_root, "Content/KKPreview")) end
  local receipt = json.decode(path.read_text(path.join(host_root, "Binaries/Win64/OpenKoikatsu.target")))
  local plugin_entries = json.array({})
  for _, dependency in ipairs(receipt.RuntimeDependencies or {}) do
    local dep = dependency.Path
    if not util.endswith(dep, ".uplugin") then
      -- Project plugins' own runtime files (OKPhysXSecondary: PhysX and MuJoCo DLLs, their licenses) go beside the plugin.
      if util.startswith(dep, "$(ProjectDir)/Plugins/") then
        local source = util.replace(dep, "$(ProjectDir)", host_root)
        local target = path.join(stage, util.replace(dep, "$(ProjectDir)", "Windows/OpenKoikatsu"))
        if not path.exists(source) then error("Runtime dependency missing: " .. win(source), 0) end
        path.mkdir(path.parent(target))
        path.copy(source, target)
      end
    else
      local source = util.replace(util.replace(dep, "$(ProjectDir)", host_root), "$(EngineDir)", engine_root)
      local relative = util.replace(util.replace(dep, "$(ProjectDir)", "Windows/OpenKoikatsu"), "$(EngineDir)", "Windows/Engine")
      local target = path.join(stage, relative)
      path.mkdir(path.parent(target))
      path.copy(source, target)
      -- UE accepts trailing commas in descriptors (engine SequencerScripting.uplugin has one); strict JSON parsers do not.
      local descriptor = path.read_text(source):gsub(",(%s*[%]}])", "%1")
      plugin_entries[#plugin_entries + 1] = json.object({ { "File", "../../../" .. relative:sub(#"Windows/" + 1) },
        { "Descriptor", json.decode(descriptor) } })
      for _, folder in ipairs({ "Resources", "Shaders" }) do
        local from = path.join(path.parent(source), folder)
        if path.exists(from) then path.copytree(from, path.join(path.parent(target), folder)) end
      end
    end
  end
  local plugin_manifest = path.join(stage, "Windows/OpenKoikatsu/Plugins/SharedInteraction.upluginmanifest")
  path.mkdir(path.parent(plugin_manifest))
  path.write_text(plugin_manifest, json.encode(json.object({ { "Contents", plugin_entries } }), { indent = 4, ensure_ascii = false }) .. "\n",
    { bom = true })
  io.write(stage_street_social.stage(path.join(host_root, "Binaries/Win64/OpenKoikatsu.exe"), a.destination, a.base_dir), "\n")
end

ok.main(main)
