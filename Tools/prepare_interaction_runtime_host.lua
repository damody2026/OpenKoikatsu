-- Builds the Interaction runtime host (default Saved/Interaction/RuntimeHost): a copy of Source, Config and the game's
-- project plugins plus junctions to Content and Tools, with a game-only OpenKoikatsu.uproject. Prints the uproject path.
--
--   Tools\lua\lua.exe Tools\prepare_interaction_runtime_host.lua [--host-path Saved/Interaction/FreeTalk01Host]
local ok = require "ok"
local json, path, proc = ok.json, ok.path, ok.proc
local lfs = require "lfs"

local function win(p) return (path.norm(p):gsub("/", "\\")) end

local function main()
  local p = ok.args.parser("Prepare the Interaction runtime host")
  p:opt("--host-path", { default = "Saved/Interaction/RuntimeHost" })
  local a = p:parse(arg)
  local project_root = ok.root
  local destination = path.abs(path.join(project_root, a.host_path))
  if destination:sub(1, #(project_root .. "/Saved/")) ~= project_root .. "/Saved/" then
    error("Runtime host must be inside project Saved directory", 0)
  end
  path.mkdir(destination)
  for _, name in ipairs({ "Source", "Config" }) do path.copytree(path.join(project_root, name), path.join(destination, name)) end
  -- Source files deleted from the project go from the host copy too (copying only adds), else the build still compiles them.
  local host_source, project_source = path.join(destination, "Source"), path.join(project_root, "Source")
  local stale = {}
  local function scan(dir)
    for _, n in ipairs(path.list(dir)) do
      local full = path.join(dir, n)
      local attr = lfs.symlinkattributes(full)
      if attr and attr.mode == "directory" then scan(full)
      elseif attr and attr.mode == "file" then
        -- Any stale file goes, not only .h/.cpp/.inl/.cs: the PowerShell original's -Include filter was ignored together
        -- with -LiteralPath, so that is what the host has always had.
        if not path.exists(path.join(project_source, full:sub(#host_source + 2))) then stale[#stale + 1] = full end
      end
    end
  end
  scan(host_source)
  for _, f in ipairs(stale) do
    io.write("Removing stale host source ", win(f), "\n")
    path.remove(f)
  end
  for _, name in ipairs({ "Content", "Tools" }) do
    local link = path.join(destination, name)
    if not path.exists(link) then
      proc.run({ "mklink", "/J", win(link), win(path.join(project_root, name)) }, { quiet = true, check = true })
    end
  end
  local plugin_root = path.join(destination, "Plugins")
  path.mkdir(plugin_root)
  -- OKPhysXSecondary + KawaiiPhysics: one-to-one interaction physics (its DLLs are copied by its Build.cs); OKAnimation: OK clips.
  local host_plugins = { "KKCard", "KKSideloader", "KKUnityBundle", "KKRuntimePose", "KKImportedAssets", "KKCrowdGpuPacking",
    "OKHGShading", "OKPhysXSecondary", "KawaiiPhysics", "OKAnimation" }
  -- Plus any project plugin the game module depends on by name in its Build.cs (OKStudio once it did).
  local build_cs = path.read_text(path.join(project_root, "Source/OpenKoikatsu/OpenKoikatsu.Build.cs")) or ""
  local listed = {}
  for _, name in ipairs(host_plugins) do listed[name] = true end
  for _, name in ipairs(path.list(path.join(project_root, "Plugins"))) do
    if not listed[name] and path.is_dir(path.join(project_root, "Plugins/" .. name)) and build_cs:find('"' .. name .. '"', 1, true) then
      host_plugins[#host_plugins + 1] = name; listed[name] = true
    end
  end
  for _, name in ipairs(host_plugins) do
    local from, to = path.join(project_root, "Plugins/" .. name), path.join(plugin_root, name)
    path.mkdir(to)
    path.copy(path.join(from, name .. ".uplugin"), path.join(to, name .. ".uplugin"))
    for _, folder in ipairs({ "Source", "Shaders", "Resources", "Config" }) do
      if path.exists(path.join(from, folder)) then path.copytree(path.join(from, folder), path.join(to, folder)) end
    end
  end
  local project = json.decode(path.read_text(path.join(project_root, "OpenKoikatsu.uproject")))
  project.DisableEnginePluginsByDefault = true
  local plugins, present = json.array({}), {}
  for _, plugin in ipairs(project.Plugins or {}) do
    -- A project plugin not copied above is not in the host (a new one, e.g. OKStudio in progress): not required there either.
    local project_plugin = path.is_dir(path.join(project_root, "Plugins/" .. plugin.Name))
    if plugin.Name ~= "BpGeneratorUltimate" and plugin.Name ~= "ModelingToolsEditorMode" and (not project_plugin or listed[plugin.Name]) then
      plugins[#plugins + 1] = plugin; present[plugin.Name] = true
    end
  end
  for _, name in ipairs({ "ACLPlugin", "EnhancedInput" }) do
    if not present[name] then plugins[#plugins + 1] = json.object({ { "Name", name }, { "Enabled", true } }) end
  end
  project.Plugins = plugins
  local out = path.join(destination, "OpenKoikatsu.uproject")
  path.write_text(out, json.encode(project, { indent = 4, ensure_ascii = false }) .. "\n", { bom = true })
  io.write(win(out), "\n")
end

ok.main(main)
