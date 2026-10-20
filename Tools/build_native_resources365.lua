-- Copy the installed abdata tree into an independent native resource pack
-- (bundles/<abdata@path@file> plus catalog.json). Module use:
--   require("build_native_resources365").bundle_filename(logical)
local ok = require "ok"
local json, P = ok.json, ok.path
local O = json.object
local M = {}

local function fail(kind, msg) error(kind .. ": " .. msg, 0) end
local function win(p) return (P.norm(p):gsub("/", "\\")) end

function M.uuid_hex()
  local t = {}
  for i = 1, 16 do t[i] = string.format("%02x", math.random(0, 255)) end
  return table.concat(t)
end

-- Python 3.12 Windows stat()/fstat() expose different legacy ctime semantics.
-- Identity, byte length and nanosecond modification time agree on both APIs.
local function fingerprint(path)
  local s = P.file_id(path)
  return table.concat({ s.volume, s.index, s.size, s.mtime_ns }, ":"), s.size
end

local function utf16_units(s)
  local n = 0
  for _, c in utf8.codes(s) do n = n + (c >= 0x10000 and 2 or 1) end
  return n
end

function M.bundle_filename(logical)
  local parts = ok.util.split(logical, "/")
  local bad = parts[1] ~= "abdata" or #parts < 2
  for _, p in ipairs(parts) do
    if p == "" or p == "." or p == ".." or p:sub(-1) == " " or p:sub(-1) == "." then bad = true end
  end
  for _, c in utf8.codes(logical) do
    if c < 32 or ('@<>:"\\|?*'):find(utf8.char(c), 1, true) then bad = true end
  end
  if bad then fail("ValueError", "Cannot encode resource path as a Windows filename: " .. logical) end
  local name = table.concat(parts, "@")
  if utf16_units(name) > 255 then fail("ValueError", "Resource filename exceeds Windows limit: " .. logical) end
  return name
end

local function stream_copy(src, dst)
  local input = assert(io.open(src, "rb"))
  if P.exists(dst) then input:close(); fail("FileExistsError", "[Errno 17] File exists: '" .. win(dst) .. "'") end
  local output = assert(io.open(dst, "wb"))
  local count = 0
  while true do
    local block = input:read(4 * 1024 * 1024)
    if not block then break end
    output:write(block)
    count = count + #block
  end
  output:close(); input:close()
  return count
end

function M.build(source, output)
  local src = P.join(P.abs(source), "abdata")
  local out = P.abs(output)
  if not P.is_dir(src) or P.exists(out) then fail("RuntimeError", "Need source abdata and fresh resource pack directory") end
  local files = {}
  for _, p in ipairs(P.rglob(src, "*")) do if P.is_file(p) then files[#files + 1] = p end end
  files = ok.util.sorted(files, function(p) return win(p):lower() end)
  if #files == 0 then fail("RuntimeError", "Source abdata contains no resource files") end
  local seen, size = {}, 0
  for _, p in ipairs(files) do
    local name = M.bundle_filename("abdata/" .. P.rel(p, src)):lower()
    if seen[name] then fail("ValueError", "Resource paths collide as Windows filenames") end
    seen[name] = true
    size = size + P.size(p)
  end
  if P.disk_usage(P.parent(out)).free < size * 1.1 then fail("RuntimeError", "Insufficient free space for independent resource copy") end
  -- mkdir is exclusive too: a concurrent invocation cannot share this output.
  local lfs = require "lfs"
  local okv, err = lfs.mkdir(out)
  if not okv then fail("FileExistsError", tostring(err)) end
  assert(lfs.mkdir(P.join(out, "bundles")))

  local function copy(path)
    local before, before_size = fingerprint(path)
    local temporary = P.join(out, "bundles", ".partial-" .. M.uuid_hex())
    local count = stream_copy(path, temporary)
    if fingerprint(path) ~= before or count ~= before_size then fail("RuntimeError", "Source changed during copy: " .. win(path)) end
    local key = ok.hash.file(temporary, "sha256")
    local logical = "abdata/" .. P.rel(path, src)
    local filename = M.bundle_filename(logical)
    local target = P.join(out, "bundles", filename)
    -- Publish a complete independent copy atomically, without replacing
    -- an existing filename. This link is inside the pack;
    -- it never links to the installed game's source file.
    local linked, message, code = P.link(temporary, target)
    if not linked then
      if code ~= 183 and code ~= 80 then fail("OSError", tostring(message)) end
      if P.size(target) ~= count or ok.hash.file(target, "sha256") ~= key then
        fail("RuntimeError", "Published resource checksum mismatch: " .. win(target))
      end
    end
    P.remove(temporary)
    return O({ { "path", logical }, { "file", filename }, { "sha256", key }, { "bytes", count } })
  end

  local rows = json.array({})
  for i, path in ipairs(files) do rows[i] = copy(path) end
  -- Failed workers leave diagnostic partial files but never a usable catalog.
  local temporary_catalog = P.join(out, ".catalog.partial")
  if P.exists(temporary_catalog) then fail("FileExistsError", "[Errno 17] File exists: '" .. win(temporary_catalog) .. "'") end
  P.write_text(temporary_catalog, json.encode(O({ { "schema", 1 }, { "kind", "native-base-bundles" }, { "entries", rows } }), { indent = 2 }))
  P.replace(temporary_catalog, P.join(out, "catalog.json"))
  local total = 0
  for _, row in ipairs(rows) do total = total + row.bytes end
  return O({ { "files", #rows }, { "bytes", total }, { "output", win(out) } })
end

if ok.is_main() then
  local p = ok.args.parser()
  p:opt("--source", { required = true })
  p:opt("--output", { required = true })
  local args = p:parse(arg)
  print(json.encode(M.build(args.source, args.output)))
end

return M
