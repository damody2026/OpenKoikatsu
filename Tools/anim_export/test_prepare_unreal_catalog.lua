-- Import validation controls; do not touch original game data or final assets.
local ok = require "ok"
local catalog = require "prepare_unreal_catalog"
local path = ok.path

local T = ok.test.suite("ImportControls")

local function raises_value_error(t, fn)
  local err = t:raises(fn)
  if not catalog.is_value_error(err) then t:fail("expected ValueError, got " .. tostring(err)) end
end

T:case("test_paths_and_package_identity", function(t)
  local game = "C:/portable/KoikatuSunshine"
  t:eq(catalog.source_key("abdata/action/animator/00.unity3d", game), "action/animator/00.unity3d")
  t:eq(catalog.source_key("C:\\portable\\KoikatuSunshine\\mods\\a.zipmod" .. "!abdata/x.unity3d", game), "mods/a.zipmod!abdata/x.unity3d")
  for _, invalid in ipairs({ "../x.unity3d", "C:/other/a.zipmod!abdata/x.unity3d", "mods/../a.zipmod!abdata/x.unity3d" }) do
    raises_value_error(t, function() catalog.source_key(invalid, game) end)
  end
end)

T:case("test_headerless_first_row_and_quoted_cells", function(t)
  local root = path.temp("_catalog")
  path.mkdir(root)
  local okv, err = pcall(function()
    for _, schema in ipairs(catalog.SCHEMAS) do
      local name, columns = schema[1], schema[2]
      local text = name:sub(1, 4) == "mod_" and "mods/a.zipmod\tabdata/x.unity3d\n" or (table.concat(columns, "\t") .. "\n")
      path.write_text(path.join(root, name .. ".tsv"), text)
    end
    local studio = nil
    for _, schema in ipairs(catalog.SCHEMAS) do if schema[1] == "studio_catalog" then studio = schema[2] end end
    path.write_bytes(path.join(root, "studio_catalog.tsv"), table.concat(studio, "\t") .. "\n"
      .. table.concat({ "anime", "base", "abdata/info.unity3d", "1", "0", "0", '"欄位\t含\n換行"', "", "", "", "", "", "", "", "" }, "\t") .. "\n")
    local doc = path.join(root, "doc.md")
    path.write_text(doc, "測試")
    local result = catalog.prepare(root, root, doc)
    local tables = {}
    for _, tb in ipairs(result.tables) do tables[tb.name] = tb end
    t:eq(#tables.mod_anim_bundles.rows, 1)
    t:eq(tables.studio_catalog.rows[1].values[7], "欄位\t含\n換行")
    path.write_text(path.join(root, "animation_sources.tsv"), "wrong\n")
    raises_value_error(t, function() catalog.prepare(root, root, doc) end)
  end)
  path.rmtree(root)
  if not okv then error(err, 0) end
end)

T:run()
