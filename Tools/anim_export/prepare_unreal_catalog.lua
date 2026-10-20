-- Prepare lossless Unreal import data from the eight exported TSVs.
--
-- Only the five export tables have headers. The three mod scan lists are raw
-- two-column TSVs. Runtime source keys are installation-relative; original cells
-- are retained. Clip names are metadata, not unique playback identities.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local M = {}

local DESCRIPTION = "Prepare lossless Unreal import data from the eight exported TSVs.\n\nOnly the five export tables have headers. The three mod scan lists are raw\ntwo-column TSVs. Runtime source keys are installation-relative; original cells\nare retained. Clip names are metadata, not unique playback identities."

M.SCHEMAS = {
  { "animation_sources", util.split("origin source clips animator_controllers animator_overrides meshes") },
  { "animation_clips", util.split("origin source clip length_sec sample_rate frames streamed_curves dense_curves constant_curves bindings") },
  { "studio_catalog", util.split("kind origin asset id group category name group_name category_name bundle file override_bundle override_file clip options") },
  { "fbs_patterns", util.split("origin asset prefab controller target_index target_go mesh pattern close close_name open open_name") },
  { "fbs_channels", util.split("origin asset prefab controller target_index target_go mesh channel_index channel_name") },
  { "mod_anim_bundles", { "zipmod", "inner_path" } },
  { "mod_studio_info", { "zipmod", "inner_path" } },
  { "mod_head_prefabs", { "zipmod", "inner_path" } },
}

local function value_error(msg) error({ value_error = true, msg = msg }, 2) end
M.is_value_error = function(e) return type(e) == "table" and e.value_error == true end

function M.source_key(value, game_root)
  value = value:gsub("\\", "/")
  local root = path.abs(game_root):gsub("/+$", "") .. "/"
  if value:lower():sub(1, #root) == root:lower() then value = value:sub(#root + 1) end
  local bang = value:find("!", 1, true)
  if bang then
    local archive, inner = value:sub(1, bang - 1), value:sub(bang + 1)
    if not util.startswith(archive, "mods/") or not util.startswith(inner, "abdata/") then
      value_error("unsupported package source: " .. value)
    end
  elseif util.startswith(value, "abdata/") then
    value = value:sub(8)
  elseif not util.startswith(value, "mods/") then
    value_error("source is outside the game installation: " .. value)
  end
  for _, part in ipairs(util.split((value:gsub("!", "/")), "/")) do
    if part == "" or part == "." or part == ".." then value_error("invalid source: " .. value) end
  end
  if value:find(":", 1, true) then value_error("invalid source: " .. value) end
  return value
end

-- Python str.splitlines() (all line boundaries, UTF-8 text).
local function splitlines(s)
  local out, start, i, n = {}, 1, 1, #s
  while i <= n do
    local c = s:byte(i)
    local len = 0
    if c == 10 or c == 11 or c == 12 or c == 28 or c == 29 or c == 30 then len = 1
    elseif c == 13 then len = (s:byte(i + 1) == 10) and 2 or 1
    elseif c == 0xC2 and s:byte(i + 1) == 0x85 then len = 2
    elseif c == 0xE2 and s:byte(i + 1) == 0x80 and (s:byte(i + 2) == 0xA8 or s:byte(i + 2) == 0xA9) then len = 3 end
    if len > 0 then
      out[#out + 1] = s:sub(start, i - 1)
      i = i + len
      start = i
    else
      i = i + 1
    end
  end
  if start <= n then out[#out + 1] = s:sub(start) end
  return out
end

local function strip_bom(s) if s:sub(1, 3) == "\239\187\191" then return s:sub(4) end return s end

function M.prepare(input_dir, game_root, document)
  local tables = json.array({})
  local lookup = {}
  for _, schema in ipairs(M.SCHEMAS) do
    local name, columns = schema[1], schema[2]
    local file = path.join(input_dir, name .. ".tsv")
    local raw = path.read_bytes(file)
    local rows = {}
    if util.startswith(name, "mod_") then
      -- scan_mods writes literal paths rather than CSV quote escaping.
      for _, line in ipairs(splitlines(strip_bom(raw))) do
        if line ~= "" then rows[#rows + 1] = util.split(line, "\t") end
      end
    else
      local parsed = ok.csv.parse(raw, { delimiter = "\t" })
      local header = parsed[1]
      if header == nil or not util.equal(header, columns) then value_error(file .. ": unexpected columns") end
      for i = 2, #parsed do rows[#rows + 1] = parsed[i] end
    end
    local entries = json.array({})
    for ordinal, cells in ipairs(rows) do
      if #cells ~= #columns then
        value_error(("%s: row %d has %d cells, expected %d"):format(file, ordinal, #cells, #columns))
      end
      local row = {}
      for i, c in ipairs(columns) do row[c] = cells[i] end
      local source = row.source or row.asset or ""
      if util.startswith(name, "mod_") then source = row.zipmod .. "!" .. row.inner_path end
      entries[#entries + 1] = json.object({ { "values", json.array(cells) }, { "source_key", M.source_key(source, game_root) } })
    end
    local t = json.object({ { "name", name }, { "columns", json.array(columns) }, { "source_sha256", ok.hash.sha256_file(file) }, { "rows", entries } })
    tables[#tables + 1] = t
    lookup[name] = t
  end
  local actual = {}
  for _, row in ipairs(lookup.animation_clips.rows) do actual[row.source_key] = (actual[row.source_key] or 0) + 1 end
  for _, row in ipairs(lookup.animation_sources.rows) do
    local expected = math.tointeger(tonumber(util.strip(row.values[3])))
    if expected == nil then value_error("invalid literal for int() with base 10: '" .. row.values[3] .. "'") end
    local got = actual[row.source_key] or 0
    actual[row.source_key] = nil
    if got ~= expected then value_error("clip count does not match source: " .. row.source_key) end
  end
  if next(actual) ~= nil then value_error("clips refer to sources absent from animation_sources") end
  local raw_document = path.read_bytes(document)
  return json.object({ { "schema", 1 }, { "document", strip_bom(raw_document) },
    { "document_sha256", ok.hash.sha256_file(document) }, { "tables", tables } })
end

local function main()
  local root = ok.root
  local parser = ok.args.parser(DESCRIPTION)
  parser:opt("--input", { default = root .. "/Saved/anim_export" })
  parser:opt("--game", { default = ok.game_root(true) })
  parser:opt("--document", { default = root .. "/Docs/ANIMATION_AND_MOUTH.md" })
  parser:opt("--output", { default = root .. "/Saved/anim_export/unreal_catalog.json" })
  local args = parser:parse(arg)
  local okv, result = pcall(M.prepare, args.input, args.game, args.document)
  if not okv then
    if M.is_value_error(result) then error("ValueError: " .. result.msg, 0) end
    error(result, 0)
  end
  path.mkdir(path.parent(args.output))
  path.write_text(args.output, json.encode(result, { ensure_ascii = false, separators = { ",", ":" } }))
  local counts = json.object()
  for _, t in ipairs(result.tables) do counts[t.name] = #t.rows end
  print(json.encode(counts, { indent = 2 }))
end

if ok.is_main() then ok.main(main) end
return M
