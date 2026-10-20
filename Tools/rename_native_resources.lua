-- Migrate native bundles to readable @ filenames without copying payloads.
-- Module use: require("rename_native_resources").migrate(pack, backup)
local ok = require "ok"
local json, P = ok.json, ok.path
local O = json.object
local build = require "build_native_resources365"
local bundle_filename = build.bundle_filename
local M = {}

local function fail(kind, msg) error(kind .. ": " .. msg, 0) end
local function win(p) return (P.norm(p):gsub("/", "\\")) end
local function same(a, b) return P.abs(a):lower() == P.abs(b):lower() end

-- os.link; tests replace M.link to inject failures.
function M.link(source, target)
  local linked, message = P.link(source, target)
  if not linked then fail("OSError", tostring(message)) end
end

local function digest(path) return ok.hash.file(path, "sha256") end

function M.migrate(pack, backup)
  if not P.exists(pack) then fail("FileNotFoundError", "[WinError 2] The system cannot find the file specified: '" .. win(pack) .. "'") end
  pack = P.abs(pack)
  backup = P.abs(backup)
  if same(backup, pack) or P.is_relative_to(backup, pack) then fail("ValueError", "Keep catalog backup outside the resource pack") end
  local catalog_path = P.join(pack, "catalog.json")
  local original = P.read_bytes(catalog_path)
  local catalog = json.decode(original:sub(1, 3) == "\239\187\191" and original:sub(4) or original)
  if catalog.schema ~= 1 or catalog.kind ~= "native-base-bundles" then fail("ValueError", "Unsupported native catalog") end
  local plans, names = {}, {}
  for _, row in ipairs(catalog.entries) do
    local name = bundle_filename(row.path)
    if names[name:lower()] then fail("ValueError", "Filename collision: " .. name) end
    names[name:lower()] = true
    local old = row.file
    if old == nil then old = row.sha256 end
    if old ~= name and old ~= row.sha256 then fail("ValueError", "Unexpected source filename: " .. tostring(old)) end
    local sha = row.sha256
    if #sha ~= 64 or sha:find("[^0-9a-f]") then fail("ValueError", "Invalid resource SHA-256") end
    local source = P.join(pack, "bundles", old)
    local target = P.join(pack, "bundles", name)
    if P.size(source) ~= row.bytes or digest(source) ~= sha then fail("ValueError", "Resource checksum mismatch: " .. win(source)) end
    if not same(source, target) and P.exists(target) then fail("FileExistsError", win(target)) end
    plans[#plans + 1] = { source, target }
    row.file = name
  end
  -- Validate the complete pack before changing any filenames.
  P.mkdir(P.parent(backup))
  if P.exists(backup) then fail("FileExistsError", "[Errno 17] File exists: '" .. win(backup) .. "'") end
  P.write_bytes(backup, original)
  local created = {}
  local temporary = P.join(pack, ".catalog-" .. build.uuid_hex() .. ".partial")
  local okv, err = pcall(function()
    for _, plan in ipairs(plans) do
      if not same(plan[1], plan[2]) then
        M.link(plan[1], plan[2])
        created[#created + 1] = plan[2]
      end
    end
    if P.exists(temporary) then fail("FileExistsError", "[Errno 17] File exists: '" .. win(temporary) .. "'") end
    P.write_text(temporary, json.encode(catalog, { indent = 2 }), { newline = "\n" })
    if P.read_bytes(catalog_path) ~= original then fail("RuntimeError", "Catalog changed during migration") end
    P.replace(temporary, catalog_path)
  end)
  if not okv then
    -- Old catalog and old names remain usable until atomic publication.
    for _, target in ipairs(created) do P.remove(target) end
    P.remove(temporary, true)
    error(err, 0)
  end
  -- After publication, remove only old names; payload inode stays unchanged.
  local removed = {}
  for _, plan in ipairs(plans) do
    local key = P.abs(plan[1]):lower()
    if not same(plan[1], plan[2]) and not removed[key] then removed[key] = true; P.remove(plan[1]) end
  end
  local payload = 0
  for _, row in ipairs(catalog.entries) do
    local file = P.join(pack, "bundles", row.file)
    if P.size(file) ~= row.bytes then fail("RuntimeError", "Migrated resource size mismatch: " .. win(file)) end
    payload = payload + row.bytes
  end
  return O({ { "files", #plans }, { "payload_bytes", payload },
    { "catalog_bytes", P.size(catalog_path) }, { "backup", win(backup) },
    { "verified", "full SHA-256 before migration; unchanged payload hardlinks after" } })
end

if ok.is_main() then
  local p = ok.args.parser("Migrate native bundles to readable @ filenames without copying payloads.")
  p:opt("--pack", { required = true })
  p:opt("--backup", { required = true })
  local args = p:parse(arg)
  print(json.encode(M.migrate(args.pack, args.backup)))
end

return M
