-- Composes a fresh StreetSocial rendering patch folder (--output, must not exist): every .pak of the --baseline patch
-- folder except its RuntimeMaterialPatch-* paks, plus the given cooked common material pak (RuntimeMaterialPatch-*_P.pak).
--
--   Tools\lua\lua.exe Tools\prepare_street_social_render_patch.lua --baseline DIR --material-pak PAK --output DIR
local ok = require "ok"
local path = ok.path

local function main()
  local p = ok.args.parser("Compose a StreetSocial rendering patch with one common material pak")
  p:opt("--baseline", { required = true })
  p:opt("--material-pak", { required = true })
  p:opt("--output", { required = true })
  local a = p:parse(arg)
  if path.exists(a.output) then error("Fresh rendering patch directory required", 0) end
  if not path.exists(a.baseline) then error("Baseline not found: " .. a.baseline, 0) end
  if not path.exists(a.material_pak) then error("Material pak not found: " .. a.material_pak, 0) end
  local material_name = path.name(path.abs(a.material_pak))
  if not path.is_dir(a.baseline) or path.is_dir(a.material_pak) or not path.fnmatch(material_name, "RuntimeMaterialPatch-*_P.pak") then
    error("Expected baseline directory and cooked common material pak", 0)
  end
  -- Every common material cook owns ShaderArchive-RuntimeMaterialBake. Keeping
  -- two versions in the mounted directory makes the first archive hide shaders
  -- needed by the other version, even when their material package roots differ.
  local output = path.abs(a.output)
  path.mkdir(output)
  for _, f in ipairs(path.glob(path.abs(a.baseline), "*.pak")) do
    if path.is_file(f) and not path.fnmatch(path.name(f), "RuntimeMaterialPatch-*") then path.copy(f, path.join(output, path.name(f))) end
  end
  path.copy(path.abs(a.material_pak), path.join(output, material_name))
  if #path.glob(output, "RuntimeMaterialPatch-*.pak") ~= 1 then error("One common shader archive required", 0) end
end

ok.main(main)
