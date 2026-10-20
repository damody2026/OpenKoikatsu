-- Check changed cached texture ST against all available user material dumps.
local ok = require "ok"
local G = require "gpp"
local json, path, util = ok.json, ok.path, ok.util
local O, A, check = G.O, G.A, G.check
local material_key = require("verify_material_texture_transforms").material_key
local max_abs_error = require("verify_material_texture_transforms").max_abs_error

local M = {}
local DOC = "Check changed cached texture ST against all available user material dumps."

local function sha(p) return G.digest(p) end
local function read(p) return G.read_json(p) end
M.sha, M.read = sha, read
local null = json.null

-- str(Path(p).resolve())
local function resolved(p) return G.winpath(path.abs(p)) end
-- Python None comparisons: JSON null and a missing key are both None.
local function none(v) if v == null then return nil end return v end
local function eqv(a, b) return none(a) == none(b) end
-- Python truthiness of a JSON value.
local function truthy(v)
  if v == nil or v == null or v == false or v == 0 or v == "" then return false end
  if type(v) == "table" then return next(v) ~= nil end
  return true
end
-- sorted(Path list) on Windows: case-insensitive comparison of path parts.
local function path_key(p) return path.parts(p:lower()) end
local function tkey(...) return table.concat({ ... }, "\31") end

local function main()
  local p = ok.args.parser(DOC)
  for _, a in ipairs({ "inventory", "comparison", "output" }) do p:arg(a) end
  p:opt("--identities", { help = "Ordered source replay for duplicated mesh/material identities" })
  local args = p:parse(arg)
  local inventory = {}
  for _, row in ipairs(read(args.inventory)) do inventory[row.card] = row end
  local comparison = read(args.comparison)
  check(truthy(comparison.passed))
  local rows, missing, unchanged, uncovered = {}, {}, {}, {}
  local inputs = json.object()
  for _, q in ipairs({ args.inventory, args.comparison }) do inputs[resolved(q)] = sha(q) end
  local identities = json.object()
  if args.identities then
    local identity_report = read(args.identities)
    check(truthy(identity_report.passed) and resolved(identity_report.comparison):lower() == resolved(args.comparison):lower())
    if identity_report.comparison_sha256 ~= nil then check(sha(args.comparison) == identity_report.comparison_sha256) end
    local witnessed_cards = {}
    local sources = identity_report.source_reports or json.object()
    for _, sp in ipairs(json.keys(sources)) do
      local expected = sources[sp]
      check(sha(sp) == expected); inputs[resolved(sp)] = expected
      for _, c in ipairs(read(sp).cards) do witnessed_cards[#witnessed_cards + 1] = c end
    end
    if #witnessed_cards > 0 then check(util.equal(identity_report.cards, witnessed_cards)) end
    inputs[resolved(args.identities)] = sha(args.identities)
    for _, row in ipairs(identity_report.cards) do identities[row.card] = row end
    for _, card in ipairs(json.keys(identities)) do
      local row = identities[card]
      for _, name in ipairs({ "manifest", "source_card" }) do
        local q = row[name]; inputs[resolved(q)] = sha(q)
      end
    end
  end
  for _, card in ipairs(comparison.cards) do
    local inv = inventory[card.card]
    if inv == nil then error("KeyError: '" .. card.card .. "'") end
    local dump = inv.dump
    if not path.is_dir(dump) then
      missing[#missing + 1] = O("card", card.card, "changed_parts", #card.changes,
        "reason", "Current user dump absent; no native ST parity claimed for this card")
    elseif #card.changes == 0 then
      unchanged[#unchanged + 1] = card.card
    else
      local native = {}
      local identity = identities[card.card]
      for _, mp in ipairs(util.sorted(path.rglob(dump, "*.materials.json"), path_key)) do
        inputs[resolved(mp)] = sha(mp)
        local data = read(mp)
        local field, slot
        if identity then
          local name = path.name(mp)
          local metadata_path = path.with_name(mp, util.replace(name, ".materials.json", ".json"))
          inputs[resolved(metadata_path)] = sha(metadata_path)
          local source = read(metadata_path).sourcePart
          if not truthy(source) then source = {} end
          field, slot = source.field, source.slot
        end
        for _, material in ipairs(data.materials) do
          for _, prop in ipairs(material.properties) do
            local texture = prop.texture
            if truthy(texture) and texture.scale ~= nil and texture.offset ~= nil then
              local value = { texture.scale.x, texture.scale.y, texture.offset.x, texture.offset.y }
              -- SessionPart.name comes from the Unity Mesh asset, while
              -- renderer GameObject names may differ (e.g. child meshes).
              local key = tkey(data.mesh, material_key(material.name), prop.name:lower())
              native[key] = native[key] or {}
              table.insert(native[key], { value, G.winpath(mp), field, slot })
            end
          end
        end
      end
      for i, part in ipairs(card.changes) do
        local ordinal = i - 1
        if truthy(part.uncovered_shader_st_components) then
          uncovered[#uncovered + 1] = O("card", card.card, "mesh", part.name, "components", part.uncovered_shader_st_components)
        end
        for _, prop in ipairs(json.keys(part.st)) do
          local value = part.st[prop]
          if G.any(part.properties, function(kv) return util.startswith(kv[1]:lower(), prop:lower() .. "_st_") end) then
            local source_identity
            if identity then
              for _, row in ipairs(identity.entries) do
                if row.change_ordinal == ordinal then source_identity = row; break end
              end
              if source_identity == nil then error("StopIteration") end
              check(source_identity.mesh == part.name and source_identity.material == part.material)
            end
            local mesh = part.name
            if truthy(source_identity) and source_identity.source_mesh ~= nil then mesh = source_identity.source_mesh end
            local key = tkey(mesh, material_key(part.material), prop:lower())
            check(native[key] ~= nil, "('Missing native changed renderer/material/property', '" .. card.card .. "', " .. key .. ")")
            local candidates = native[key]
            if identity then
              candidates = G.filter(candidates, function(row)
                return eqv(row[3], source_identity.field) and eqv(row[4], source_identity.slot) end)
              check(#candidates > 0, "('Missing exact source field/slot', '" .. card.card .. "', " .. key .. ")")
            end
            -- Do not cherry-pick a matching snapshot when the same identity
            -- has conflicting states; every matching source must agree.
            local errors = G.map(candidates, function(row) return max_abs_error(value, row[1]) end)
            check(#value == 4 and G.all(candidates, function(row) return #row[1] == 4 end))
            check(G.max(errors) <= 1e-6, card.card .. " " .. key)
            rows[#rows + 1] = O("card", card.card, "mesh", part.name, "material", part.material,
              "property", prop, "cached", value, "native", A(candidates[1][1]), "max_error", G.max(errors),
              "source_identity", source_identity or null, "sources", A(G.map(candidates, function(row) return row[2] end)))
          end
        end
      end
    end
  end
  check(#rows > 0, "No changed native texture transforms covered")
  local report = O("passed", true,
    "scope", "Changed supported cached ST against available native user dumps; missing legacy dumps explicit; not complete shader or RGB parity",
    "compared_cards", #comparison.cards, "checked_slots", #rows, "rows", A(rows),
    "unchanged_available_cards", A(unchanged), "missing_current_dumps", A(missing),
    "uncovered_shader_st_components", A(uncovered), "source_files", inputs)
  G.write_json(args.output, report)
  print(("PASS user dump changed ST=%d, missing current dumps=%d, max=%s"):format(#rows, #missing,
    util.num(G.max(rows, function(r) return r.max_error end))))
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
