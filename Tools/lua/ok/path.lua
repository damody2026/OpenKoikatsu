-- Path helpers on plain strings (forward slashes), pathlib-like.
-- Text I/O mirrors Python on Windows: read_text uses universal newlines and
-- drops a UTF-8 BOM; write_text writes "\n" as "\r\n" unless newline="" or "\n".
local fs = require "okfs"
local lfs = require "lfs"
local M = {}

local function norm(p) return (tostring(p):gsub("\\", "/")) end
M.norm = norm

function M.join(...)
  local out
  for _, part in ipairs({ ... }) do
    part = norm(part)
    if part ~= "" then
      if out == nil or part:match("^/") or part:match("^%a:") then out = part
      elseif out:sub(-1) == "/" then out = out .. part
      else out = out .. "/" .. part end
    end
  end
  return out or ""
end

function M.parent(p)
  p = norm(p):gsub("/+$", "")
  local d = p:match("^(.*)/[^/]*$")
  if d == nil then return "." end
  if d == "" then return "/" end
  if d:match("^%a:$") then return d .. "/" end
  return d
end
M.dirname = M.parent
function M.name(p) return (norm(p):gsub("/+$", ""):match("([^/]*)$")) end
M.basename = M.name
function M.suffix(p) local n = M.name(p); return n:match("^.+(%.[^.]*)$") or "" end
function M.stem(p) local n, s = M.name(p), M.suffix(p); return s == "" and n or n:sub(1, #n - #s) end
function M.with_suffix(p, s) p = norm(p); local cur = M.suffix(p); return p:sub(1, #p - #cur) .. s end
function M.with_name(p, n) return M.join(M.parent(p), n) end
function M.with_stem(p, st) return M.with_name(p, st .. M.suffix(p)) end
function M.parts(p) local t = {}; for s in norm(p):gmatch("[^/]+") do t[#t + 1] = s end; return t end

-- Absolute, normalized path with forward slashes (no symlink resolution).
function M.abs(p) return (fs.fullpath(norm(p)):gsub("\\", "/")) end
M.resolve = M.abs

-- Relative path of p from base (both made absolute); errors if not inside base
-- unless allow_up is true.
function M.rel(p, base, allow_up)
  local a, b = M.parts(M.abs(p)), M.parts(M.abs(base))
  local i = 1
  while i <= #a and i <= #b and a[i]:lower() == b[i]:lower() do i = i + 1 end
  if i <= #b and not allow_up then error(("%s is not relative to %s"):format(p, base), 2) end
  local out = {}
  for _ = i, #b do out[#out + 1] = ".." end
  for j = i, #a do out[#out + 1] = a[j] end
  return #out == 0 and "." or table.concat(out, "/")
end
function M.is_relative_to(p, base) return (pcall(M.rel, p, base)) end

function M.stat(p) return fs.stat(norm(p)) end
function M.exists(p) return fs.stat(norm(p)) ~= nil end
function M.is_file(p) local s = fs.stat(norm(p)); return s ~= nil and not s.is_dir end
function M.is_dir(p) local s = fs.stat(norm(p)); return s ~= nil and s.is_dir end
function M.size(p) local s = fs.stat(norm(p)); return s and s.size end
function M.mtime(p) local s = fs.stat(norm(p)); return s and s.mtime end

function M.mkdir(p, parents)
  p = norm(p)
  if M.is_dir(p) then return true end
  if parents ~= false then
    local parent = M.parent(p)
    if parent ~= p and parent ~= "." and not M.is_dir(parent) then M.mkdir(parent, true) end
  end
  local okv, err = lfs.mkdir(p)
  if not okv and not M.is_dir(p) then error("mkdir " .. p .. ": " .. tostring(err), 2) end
  return true
end
M.makedirs = M.mkdir

-- Sorted entry names of a directory (no . / ..).
function M.list(dir)
  local out = {}
  for n in lfs.dir(norm(dir)) do if n ~= "." and n ~= ".." then out[#out + 1] = n end end
  table.sort(out)
  return out
end
-- Sorted full paths of a directory's entries.
function M.iterdir(dir)
  local out = {}
  for _, n in ipairs(M.list(dir)) do out[#out + 1] = M.join(dir, n) end
  return out
end

-- fnmatch-style pattern (*, ?, [abc], [!abc]) to Lua pattern; case-insensitive on Windows.
local function glob_to_lua(g)
  local out, i = { "^" }, 1
  while i <= #g do
    local c = g:sub(i, i)
    if c == "*" then out[#out + 1] = ".*"
    elseif c == "?" then out[#out + 1] = "."
    elseif c == "[" then
      local j = g:find("]", i + 1, true)
      if not j then out[#out + 1] = "%[" else
        local body = g:sub(i + 1, j - 1):gsub("^!", "^"):gsub("%%", "%%%%")
        out[#out + 1] = "[" .. body .. "]"; i = j
      end
    elseif c:match("%W") then out[#out + 1] = "%" .. c
    else out[#out + 1] = c end
    i = i + 1
  end
  out[#out + 1] = "$"
  return table.concat(out)
end
function M.fnmatch(name, pattern) return name:lower():match(glob_to_lua(pattern:lower())) ~= nil end

-- glob(dir, "*.json") non-recursive; glob(dir, "**/*.json") or rglob(dir, "*.json") recursive.
-- Results are sorted full paths.
function M.rglob(dir, pattern, out)
  out = out or {}
  dir = norm(dir)
  if not M.is_dir(dir) then return out end
  for _, n in ipairs(M.list(dir)) do
    local full = M.join(dir, n)
    if M.fnmatch(n, pattern) then out[#out + 1] = full end
    local attr = lfs.symlinkattributes(full)
    if attr and attr.mode == "directory" then M.rglob(full, pattern, out) end
  end
  table.sort(out)
  return out
end
function M.glob(dir, pattern)
  pattern = norm(pattern)
  if pattern:sub(1, 3) == "**/" then return M.rglob(dir, pattern:sub(4)) end
  local head, rest = pattern:match("^([^/]+)/(.+)$")
  local out = {}
  if head then
    for _, n in ipairs(M.list(dir)) do
      local full = M.join(dir, n)
      if M.fnmatch(n, head) and M.is_dir(full) then
        for _, x in ipairs(M.glob(full, rest)) do out[#out + 1] = x end
      end
    end
  elseif M.is_dir(dir) then
    for _, n in ipairs(M.list(dir)) do if M.fnmatch(n, pattern) then out[#out + 1] = M.join(dir, n) end end
  end
  table.sort(out)
  return out
end

-- Recursive walk: calls fn(dirpath, dirnames, filenames) top-down (like os.walk).
function M.walk(dir, fn)
  local dirs, files = {}, {}
  for _, n in ipairs(M.list(dir)) do
    if M.is_dir(M.join(dir, n)) then dirs[#dirs + 1] = n else files[#files + 1] = n end
  end
  fn(norm(dir), dirs, files)
  for _, d in ipairs(dirs) do M.walk(M.join(dir, d), fn) end
end

function M.read_bytes(p)
  local f, err = fs.open(norm(p), "rb")
  if not f then error(err, 2) end
  local s = f:read("a"); f:close()
  return s
end

-- opts: newline (nil = universal newlines), keep_bom
function M.read_text(p, opts)
  local s = M.read_bytes(p)
  if not (opts and opts.keep_bom) and s:sub(1, 3) == "\239\187\191" then s = s:sub(4) end
  if not (opts and opts.newline ~= nil) then s = s:gsub("\r\n?", "\n") end
  return s
end

-- Lines without terminators (Path.read_text().splitlines() for \n / \r\n / \r files).
function M.read_lines(p)
  local s, t = M.read_text(p), {}
  if s:sub(-1) ~= "\n" then s = s .. "\n" end
  for line in s:gmatch("(.-)\n") do t[#t + 1] = line end
  if #t == 1 and t[1] == "" and #s == 1 then t[1] = nil end
  return t
end

-- Replace path with temp atomically; retries transient sharing locks (preview readers).
function M.replace(src, dst)
  src, dst = norm(src), norm(dst)
  for attempt = 1, 8 do
    local okv, err = fs.rename(src, dst, true)
    if okv then return true end
    if attempt == 8 then error("replace " .. src .. " -> " .. dst .. ": " .. tostring(err), 2) end
    fs.sleep(0.1 * attempt)
  end
end

local function write_raw(p, data, atomic)
  p = norm(p)
  local target = atomic and (p .. ".tmp") or p
  local f, err = fs.open(target, "wb")
  if not f then error(err, 3) end
  f:write(data); f:close()
  if atomic then M.replace(target, p) end
end

-- write_bytes(p, data [, {atomic=true}])
function M.write_bytes(p, data, opts) write_raw(p, data, opts and opts.atomic) end

-- write_text(p, s [, {newline="\r\n"|"\n"|"", atomic=true, bom=true}])
-- Default newline translation is "\r\n" (Python text mode on Windows).
function M.write_text(p, s, opts)
  opts = opts or {}
  local nl = opts.newline
  if nl == nil then nl = "\r\n" end
  if nl ~= "" and nl ~= "\n" then s = s:gsub("\n", nl) end
  if opts.bom then s = "\239\187\191" .. s end
  write_raw(p, s, opts.atomic)
end
function M.append_text(p, s, opts)
  local nl = opts and opts.newline
  if nl == nil then nl = "\r\n" end
  if nl ~= "" and nl ~= "\n" then s = s:gsub("\n", nl) end
  local f = assert(fs.open(norm(p), "ab")); f:write(s); f:close()
end

function M.remove(p, missing_ok)
  if missing_ok and not M.exists(p) then return true end
  local okv, err = fs.remove(norm(p))
  if not okv then error(err, 2) end
  return true
end
M.unlink = M.remove

-- Recursive delete. Refuses junction/symlink traversal: links are removed, never followed.
function M.rmtree(p)
  p = norm(p)
  local attr = lfs.symlinkattributes(p)
  if not attr then return end
  if attr.mode == "directory" then
    for _, n in ipairs(M.list(p)) do M.rmtree(M.join(p, n)) end
  end
  M.remove(p)
end

function M.copy(src, dst)
  if M.is_dir(dst) then dst = M.join(dst, M.name(src)) end
  local okv, err = fs.copy(norm(src), norm(dst), false)
  if not okv then error(err, 2) end
  return dst
end
function M.copytree(src, dst)
  M.mkdir(dst)
  for _, n in ipairs(M.list(src)) do
    local a, b = M.join(src, n), M.join(dst, n)
    if M.is_dir(a) then M.copytree(a, b) else M.copy(a, b) end
  end
end
function M.rename(src, dst) local okv, err = fs.rename(norm(src), norm(dst), false); if not okv then error(err, 2) end end

function M.cwd() return (lfs.currentdir():gsub("\\", "/")) end
function M.chdir(p) assert(lfs.chdir(norm(p))) end
function M.touch(p) if not M.exists(p) then M.write_bytes(p, "") else lfs.touch(norm(p)) end end

-- Unique temporary file path in %TEMP% (not created).
local counter = 0
function M.temp(suffix)
  counter = counter + 1
  local base = (fs.getenv("TEMP") or fs.getenv("TMP") or "."):gsub("\\", "/")
  return ("%s/oklua_%d_%d_%d%s"):format(base, math.floor(fs.time() * 1000) % 100000000, math.random(1, 1 << 30), counter, suffix or "")
end

-- Hard links, file identity and disk space (via ok-luakit).
-- link(existing, new) -> true | nil, message, os_error (183 = new already exists); never replaces.
function M.link(src, dst)
  local r = require("ok.kit").call({ "hardlink", norm(src), norm(dst) })
  if r.linked then return true end
  return nil, r.message, r.os_error
end
-- file_id(p) -> {volume, index, links, size, mtime_ns} (st_dev/st_ino/st_nlink/st_size/st_mtime_ns)
function M.file_id(p) return require("ok.kit").call({ "file-id", norm(p) }) end
-- os.path.samefile
function M.samefile(a, b)
  local x, y = M.file_id(a), M.file_id(b)
  return x.volume == y.volume and x.index == y.index
end
-- shutil.disk_usage -> {total, free, available}
function M.disk_usage(p) return require("ok.kit").call({ "disk-free", (M.abs(p):gsub("/", "\\")) }) end

return M
