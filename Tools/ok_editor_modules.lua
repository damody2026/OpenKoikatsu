-- Editor build check before launching the project in UnrealEditor (formerly ensure_editor_module_manifests.ps1).
--
--   local editor = require "ok_editor_modules"
--   editor.ensure(project_root, engine_root, {build = true})
--
-- The UBT receipt (Binaries/Win64/OpenKoikatsuEditor.target) must carry the engine's BuildId
-- (Engine/Binaries/Win64/UnrealEditor.modules). Both checkouts (C: master, E: duo) share one engine: a build in one of
-- them that touches the engine gives it a new BuildId, and the other checkout's modules then no longer load. With
-- build = true a stale or missing receipt is rebuilt here (OpenKoikatsuEditor, -NoEngineChanges) instead of failing.
--
-- Missing project module manifests (UnrealEditor.modules) are recovered from the receipt, never guessed from DLL names
-- or timestamps; existing manifests are only validated.
local ok = require "ok"
local json, path, proc = ok.json, ok.path, ok.proc

local M = {}

local function read_json(p) return json.decode((path.read_text(p):gsub("^\239\187\191", ""))) end

local function receipt_problem(receipt_path, engine_manifest)
  if not path.is_file(receipt_path) then return "missing " .. receipt_path end
  if not path.is_file(engine_manifest) then return "missing " .. engine_manifest end
  local receipt, engine = read_json(receipt_path), read_json(engine_manifest)
  local build_id = receipt.Version and receipt.Version.BuildId
  if receipt.TargetName ~= "OpenKoikatsuEditor" or receipt.Platform ~= "Win64" or receipt.Configuration ~= "Development"
    or not build_id or build_id == "" or build_id ~= engine.BuildId then
    return ("Editor build receipt does not match this engine (receipt %s, engine %s)"):format(tostring(build_id), tostring(engine.BuildId))
  end
  return nil, receipt, engine
end

-- Newest project source (Source/, Plugins/*/Source/, *.uplugin, *.Build.cs ...) newer than the receipt: after a merge or
-- pull the receipt still matches the engine, but the DLLs are older than the code and would run the previous build.
local SOURCE_EXT = { cpp = true, h = true, hpp = true, inl = true, cs = true, uplugin = true, usf = true, ush = true }
local function newest_source(dir, newest)
  if not path.is_dir(dir) then return newest end
  path.walk(dir, function(d, _, files)
    for _, n in ipairs(files) do
      local ext = n:match("%.([%w]+)$")
      if ext and SOURCE_EXT[ext:lower()] then
        local t = path.mtime(path.join(d, n))
        if t and t > newest then newest = t end
      end
    end
  end)
  return newest
end

local function sources_newer(project_root, receipt_path)
  local built = path.mtime(receipt_path)
  if not built then return nil end
  local newest = newest_source(path.join(project_root, "Source"), 0)
  local plugins = path.join(project_root, "Plugins")
  for _, plugin in ipairs(path.is_dir(plugins) and path.list(plugins) or {}) do
    newest = newest_source(path.join(project_root, "Plugins", plugin, "Source"), newest)
  end
  if newest > built then return ("Project sources are newer than the editor build (%s > %s)"):format(os.date("%m-%d %H:%M", math.floor(newest)), os.date("%m-%d %H:%M", math.floor(built))) end
end

-- Builds OpenKoikatsuEditor against the engine without touching the engine's own binaries.
function M.build(project_root, engine_root)
  local bat = path.join(engine_root, "Engine/Build/BatchFiles/Build.bat")
  if not path.is_file(bat) then error("Engine build script not found: " .. bat, 0) end
  local project = path.join(project_root, "OpenKoikatsu.uproject")
  io.write("Building OpenKoikatsuEditor (", engine_root, ") ...\n"); io.flush()
  local r = proc.run({ (bat:gsub("/", "\\")), "OpenKoikatsuEditor", "Win64", "Development", "-Project=" .. (project:gsub("/", "\\")),
    "-NoEngineChanges", "-WaitMutex" }, { cwd = project_root })
  if not r.ok then error(("OpenKoikatsuEditor build failed (exit %d); see the output above."):format(r.code), 0) end
end

-- Validates the receipt and module manifests; recovers missing project manifests. opts.build: rebuild when stale.
function M.ensure(project_root, engine_root, opts)
  opts = opts or {}
  project_root = path.abs(project_root):gsub("/+$", "")
  engine_root = path.abs(engine_root):gsub("/+$", "")
  local receipt_path = path.join(project_root, "Binaries/Win64/OpenKoikatsuEditor.target")
  local engine_manifest = path.join(engine_root, "Engine/Binaries/Win64/UnrealEditor.modules")
  local problem, receipt, engine = receipt_problem(receipt_path, engine_manifest)
  if not problem and opts.build then problem = sources_newer(project_root, receipt_path) end
  if problem and opts.build then
    io.write(problem, "\n"); io.flush()
    M.build(project_root, engine_root)
    problem, receipt, engine = receipt_problem(receipt_path, engine_manifest)
  end
  if problem then error(problem .. ". Rebuild OpenKoikatsuEditor with -NoEngineChanges; manifests were not changed.", 0) end

  local groups, manifests = {}, {}
  local prefix = "$(ProjectDir)/"
  for _, product in ipairs(receipt.BuildProducts or {}) do
    local rel = product.Path
    if rel:sub(1, #prefix) == prefix then
      local full = path.abs(path.join(project_root, rel:sub(#prefix + 1)))
      if full:lower():sub(1, #project_root + 1) ~= project_root:lower() .. "/" then error("Build receipt contains a path outside the project.", 0) end
      local name = path.name(full)
      if name == "UnrealEditor.modules" then manifests[#manifests + 1] = full end
      -- Third-party DLLs staged next to the modules (PhysX.dll, mujoco.dll, ...) are dependencies, not modules.
      if product.Type == "DynamicLibrary" and name:lower():sub(1, 13) == "unrealeditor-" then
        local module = name:match("^UnrealEditor%-([A-Za-z_][A-Za-z_0-9]*)%.dll$")
        if not module then error("Cannot recover manifests from hot-reload or unexpected DLL: " .. name .. ". Rebuild the editor target.", 0) end
        if not path.is_file(full) then error("Build receipt DLL is missing: " .. full .. ". Rebuild the editor target; manifests were not changed.", 0) end
        local dir = path.parent(full)
        groups[dir] = groups[dir] or {}
        table.insert(groups[dir], { module, name })
      end
    end
  end
  local project_manifest = path.join(project_root, "Binaries/Win64/UnrealEditor.modules")
  local listed = false
  for _, m in ipairs(manifests) do if m:lower() == project_manifest:lower() then listed = true end end
  if not listed then error("Build receipt has no project module manifest.", 0) end

  -- Validate everything before writing anything.
  local pending = {}
  for _, m in ipairs(manifests) do
    local dir = path.parent(m)
    if not groups[dir] then error("Build receipt has no DLLs for " .. m, 0) end
    if path.is_file(m) then
      local existing = read_json(m)
      if existing.BuildId ~= engine.BuildId then error("Incompatible module manifest: " .. m .. ". Rebuild the editor target.", 0) end
      for _, key in ipairs(json.keys(existing.Modules or {})) do
        local dll = existing.Modules[key]
        if path.name(dll) ~= dll or not path.is_file(path.join(dir, dll)) then error("Missing or invalid module DLL in " .. m .. " : " .. tostring(dll), 0) end
      end
    else
      pending[#pending + 1] = m
    end
  end
  for _, m in ipairs(pending) do
    local modules = {}
    for _, entry in ipairs(groups[path.parent(m)]) do modules[#modules + 1] = entry end
    local data = json.object({ { "BuildId", engine.BuildId }, { "Modules", json.object(modules) } })
    -- Never overwrite a manifest UBT wrote in the meantime.
    if path.exists(m) then error("Module manifest appeared while recovering it: " .. m, 0) end
    path.write_text(m, json.encode(data, { indent = 2 }))
    io.write("Recovered module manifest from matching UBT receipt: ", m, "\n")
  end
  return receipt
end

return M
