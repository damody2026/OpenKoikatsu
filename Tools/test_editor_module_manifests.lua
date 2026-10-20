-- Fixture test of Tools/ok_editor_modules.lua (manifest recovery, idempotence, rejection of a stale receipt or a missing
-- DLL). Runs in Saved/Tests/ModuleManifestRecovery/<id>; never touches real build products.
--   Tools\lua\lua.exe Tools\test_editor_module_manifests.lua
local ok = require "ok"
local json, path, hash = ok.json, ok.path, ok.hash
local editor = require "ok_editor_modules"

local function main()
  local fixture = path.join(ok.root, "Saved/Tests/ModuleManifestRecovery", os.date("%Y%m%d-%H%M%S") .. ("-%08x"):format(math.random(0, 0x7fffffff)))
  local project, engine = path.join(fixture, "project"), path.join(fixture, "engine")
  local bin, engine_bin = path.join(project, "Binaries/Win64"), path.join(engine, "Engine/Binaries/Win64")
  path.mkdir(bin, true); path.mkdir(engine_bin, true)
  local manifest = path.join(bin, "UnrealEditor.modules")
  local dll = path.join(bin, "UnrealEditor-OpenKoikatsu.dll")
  local receipt_path = path.join(bin, "OpenKoikatsuEditor.target")
  path.write_text(dll, "fixture")
  path.write_text(path.join(engine_bin, "UnrealEditor.modules"), '{"BuildId":"fixture-compatible","Modules":{}}')
  local function receipt(build_id)
    path.write_text(receipt_path, json.encode(json.object({
      { "TargetName", "OpenKoikatsuEditor" }, { "Platform", "Win64" }, { "Configuration", "Development" },
      { "Version", json.object({ { "BuildId", build_id } }) },
      { "BuildProducts", json.array({
        json.object({ { "Path", "$(ProjectDir)/Binaries/Win64/UnrealEditor-OpenKoikatsu.dll" }, { "Type", "DynamicLibrary" } }),
        json.object({ { "Path", "$(ProjectDir)/Binaries/Win64/UnrealEditor.modules" }, { "Type", "RequiredResource" } }) }) } })))
  end
  local function rejected(fragment)
    local okv, err = pcall(editor.ensure, project, engine)
    return not okv and tostring(err):find(fragment, 1, true) ~= nil
  end

  receipt("fixture-compatible")
  editor.ensure(project, engine)
  local data = json.decode(path.read_text(manifest))
  assert(data.BuildId == "fixture-compatible" and data.Modules.OpenKoikatsu == "UnrealEditor-OpenKoikatsu.dll", "Recovery mismatch")
  local first = hash.sha256_file(manifest)
  editor.ensure(project, engine)
  assert(hash.sha256_file(manifest) == first, "Existing manifest changed")
  -- Rename only isolated fixtures; never remove actual project build products.
  path.rename(manifest, manifest .. ".verified")

  receipt("incompatible")
  assert(rejected("does not match") and not path.exists(manifest), "Incompatible receipt was not safely rejected")

  receipt("fixture-compatible")
  path.rename(dll, dll .. ".fixture")
  assert(rejected("DLL is missing") and not path.exists(manifest), "Missing DLL was not safely rejected")

  local result = json.encode(json.object({ { "recovery", true }, { "idempotent", true }, { "incompatible_receipt_rejected", true },
    { "missing_dll_rejected", true }, { "fixture", fixture } }), { indent = 2 })
  path.write_text(path.join(fixture, "result.json"), result)
  io.write(result, "\n")
end

ok.main(main)
