-- Check a four-view Unreal capture against the game-dump card inventory.
--
-- Usage: Tools/lua/lua.exe Tools/GamePoseProbe/audit_four_views.lua INVENTORY CAPTURE_DIR AUDIT_JSON
local ok = require "ok"
local G = require "gpp"
local json, path = ok.json, ok.path
local O, A, check = G.O, G.A, G.check

local M = {}
M.SUFFIXES = { "3d", "3d_side", "3d_back", "face" }
local SUFFIXES = M.SUFFIXES
local PNG_HEADER = "\137PNG\r\n\26\n"

local DOC = "Check a four-view Unreal capture against the game-dump card inventory.\n\n" ..
  "Usage: Tools/lua/lua.exe Tools/GamePoseProbe/audit_four_views.lua INVENTORY CAPTURE_DIR AUDIT_JSON\n"

-- png_size(path) -> width, height from the IHDR header.
function M.png_size(p)
  local f = assert(io.open(p, "rb"))
  local header = f:read(24) or ""
  f:close()
  if #header ~= 24 or header:sub(1, 8) ~= PNG_HEADER or header:sub(13, 16) ~= "IHDR" then
    error("ValueError: Invalid PNG header: " .. G.winpath(p), 2)
  end
  return string.unpack(">I4I4", header, 17)
end

-- Path(p).is_absolute() on Windows: drive + root, or UNC.
local function is_absolute(p) return p:match("^%a:[/\\]") ~= nil or p:match("^[/\\][/\\]") ~= nil end

local function main()
  local p = ok.args.parser(DOC)
  for _, name in ipairs({ "inventory", "capture", "output" }) do p:arg(name) end
  p:opt("--historical-audit", { help = "Accept a removed dump only if its identity and source card hash match an earlier passing audit" })
  local args = p:parse(arg)
  local inventory_path, capture_dir, audit_path = args.inventory, args.capture, args.output
  local historical = {}
  if args.historical_audit then
    local prior = G.read_json(args.historical_audit)
    check(prior.result == "pass")
    for _, r in ipairs(prior.cards) do historical[r.card] = r end
  end
  local inventory = G.read_json(inventory_path)
  if type(inventory) ~= "table" or not json.is_array(inventory) or #inventory == 0 then
    error("ValueError: Expected a nonempty card inventory")
  end
  local found, found_list, records = {}, {}, {}
  for _, item in ipairs(inventory) do
    local card = item.card
    local source_only = item.dump == nil
    local missing_dump = not source_only and not path.is_dir(item.dump)
    if not card:lower():match("%.png$") or (missing_dump and historical[card] == nil) then
      error("ValueError: Invalid dump/card entry: " .. json.encode(item, { ensure_ascii = false }))
    end
    local source_card
    if is_absolute(card) then
      source_card = card
    else
      source_card = require("card_paths").resolve_card(ok.game_root(true), card)
    end
    if source_only and not is_absolute(card) then
      error("ValueError: Source-only inventories require resolved absolute card paths")
    end
    if not path.is_file(source_card) then error("FileNotFoundError: " .. G.winpath(source_card)) end
    local card_hash = G.digest(source_card)
    if missing_dump then
      check(historical[card].dump == item.dump and historical[card].card_sha256 == card_hash,
        "('historical card/dump identity changed', '" .. card .. "')")
    end
    local stem = path.stem(card)
    local views = json.object()
    for _, suffix in ipairs(SUFFIXES) do
      local name = stem .. "_outfit0_" .. suffix .. ".png"
      local shot = path.join(capture_dir, name)
      if not path.is_file(shot) then error("FileNotFoundError: " .. G.winpath(shot)) end
      if found[name] then error("ValueError: Duplicate screenshot name: " .. name) end
      found[name] = true; found_list[#found_list + 1] = name
      local w, h = M.png_size(shot)
      if w ~= 900 or h ~= 1080 then error(("ValueError: Unexpected PNG size (%d, %d): %s"):format(w, h, G.winpath(shot))) end
      views[suffix] = O("path", G.winpath(path.abs(shot)), "size", A({ w, h }), "sha256", G.digest(shot))
    end
    local provenance = source_only and "source-card inventory; native evidence audited separately"
      or missing_dump and "historical passing audit; directory removed" or "current dump directory"
    records[#records + 1] = O("card", card, "card_sha256", card_hash, "dump_provenance", provenance,
      "dump", item.dump, "mesh_count", item.mesh_count, "photo_count", item.photo_count, "views", views)
  end
  local actual = G.glob_names(capture_dir, "*.png")
  if not G.set_eq(found_list, actual) then
    local extra = G.sorted(G.filter(actual, function(n) return not found[n] end))
    error("ValueError: Unexpected screenshots: " .. json.encode(extra, { ensure_ascii = false }))
  end
  local result = O("result", "pass", "card_count", #records, "screenshot_count", #found_list,
    "views_per_card", A({ table.unpack(SUFFIXES) }), "cards", A(records))
  path.mkdir(path.parent(audit_path), true)
  G.write_json(audit_path, result)
  print(("PASS cards=%d screenshots=%d size=900x1080"):format(#records, #found_list))
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
