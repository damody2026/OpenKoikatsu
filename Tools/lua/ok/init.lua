-- ok: shared library for OpenKoikatsu Lua scripts.
--   local ok = require "ok"
-- Loading it makes io.open/io.lines/io.popen/os.remove/os.rename/os.execute/
-- os.getenv UTF-8 aware (Windows wide APIs), re-reads `arg` as UTF-8, switches
-- the console to UTF-8 and puts the calling script's folder on package.path so
-- sibling modules load with require.
local fs = require "okfs"

if not rawget(_G, "__ok_patched") then
  rawset(_G, "__ok_patched", true)
  io.open, io.popen = fs.open, fs.popen
  local raw_lines = io.lines
  io.lines = function(path, ...)
    if path == nil then return raw_lines() end
    local f = assert(fs.open(path, "r"))
    local it = f:lines(...)
    return function()
      local a, b, c = it()
      if a == nil then f:close() end
      return a, b, c
    end, nil, nil, f
  end
  os.remove, os.execute, os.getenv = fs.remove, fs.execute, fs.getenv
  os.rename = function(a, b) return fs.rename(a, b, false) end
  fs.utf8_console()
  -- lua.exe receives argv in the ANSI code page; rebuild arg from the wide command line.
  if arg then
    rawset(_G, "__ok_arg0_raw", arg[0])
    local raw, n, count = fs.args(), 0, #arg
    for i in pairs(raw) do if i > n then n = i end end
    for k = 0, count do
      local v = raw[n - count + k]
      if v then arg[k] = v end
    end
  end
end

local function script_dir()
  local s = arg and arg[0]
  if not s then return "." end
  s = s:gsub("\\", "/")
  return s:match("^(.*)/[^/]*$") or "."
end

local ok = {}
ok.fs = fs
ok.script_dir = fs.fullpath(script_dir()):gsub("\\", "/")
package.path = ok.script_dir .. "/?.lua;" .. package.path

-- Repository root: this file lives at <root>/Tools/lua/ok/init.lua.
do
  local src = debug.getinfo(1, "S").source:gsub("^@", "")
  ok.root = (fs.fullpath(src .. "/../../../..")):gsub("\\", "/")
end
ok.lua_exe = ok.root .. "/Tools/lua/lua.exe"
-- Shared Tools/ modules are reachable from any script directory.
package.path = package.path .. ";" .. ok.root .. "/Tools/?.lua"

-- Unreal Engine root this project builds with (forward slashes, no trailing slash).
-- Tools/ok_engine.lua prints it for .cmd launchers. Order: OK_ENGINE_ROOT, Tools/engine.local.json {"engine": ...}, E:/PhysX/UE5.8.
function ok.engine_root()
  local root = os.getenv("OK_ENGINE_ROOT")
  if not root or root == "" then
    local f = io.open(ok.root .. "/Tools/engine.local.json", "rb")
    if f then local s = f:read("a"); f:close(); root = ok.json.decode((s:gsub("^\239\187\191", ""))).engine end
  end
  if not root or root == "" then root = "E:/PhysX/UE5.8" end
  return (root:gsub("\\", "/"):gsub("/+$", ""))
end

-- Koikatsu Sunshine installation root (forward slashes). Same rule as
-- KKLocalMods.cpp: abdata + UserData + KoikatsuSunshine.exe/Koikatu.exe.
-- OK_GAME_ROOT wins when valid; otherwise the known install locations are
-- probed (the original C:/portable copy kept only BepInEx after the move to D:).
ok.LEGACY_GAME_ROOT = "C:/portable/KoikatuSunshine"
local function is_game_root(d)
  return d and fs.stat(d .. "/abdata") and fs.stat(d .. "/UserData")
    and (fs.stat(d .. "/KoikatsuSunshine.exe") or fs.stat(d .. "/Koikatu.exe")) and true or false
end
ok.is_game_root = is_game_root
local game_root_cache
-- game_root() errors when nothing is installed; game_root(true) then returns the
-- legacy path instead (for CLI defaults that are only used if needed).
-- game_root(soft, "\\") formats with backslashes.
function ok.game_root(soft, sep)
  if not game_root_cache then
    local env = fs.getenv("OK_GAME_ROOT")
    local candidates = { env and env:gsub("\\", "/"):gsub("/+$", "") or nil, ok.LEGACY_GAME_ROOT, "D:/KoikatuSunshine", "E:/KoikatuSunshine" }
    for i = 1, 4 do
      if is_game_root(candidates[i]) then game_root_cache = candidates[i] break end
    end
  end
  local r = game_root_cache
  if not r then
    if not soft then error("Koikatsu Sunshine installation not found; set OK_GAME_ROOT", 2) end
    r = ok.LEGACY_GAME_ROOT
  end
  return sep and (r:gsub("/", sep)) or r
end
-- Map a path written against the legacy install root onto the actual one.
function ok.game_path(p)
  p = tostring(p):gsub("\\", "/")
  local legacy = ok.LEGACY_GAME_ROOT
  if p:sub(1, #legacy):lower() == legacy:lower() and not is_game_root(legacy) then
    return ok.game_root() .. p:sub(#legacy + 1)
  end
  return p
end

-- Python's `if __name__ == "__main__"`: call from a module's main chunk as
--   if ok.is_main() then main() end
-- True when the chunk was started by lua.exe, false when loaded via require.
-- dofile/loadfile of another script also counts as "not main".
function ok.is_main()
  local caller = debug.getinfo(3, "f")
  if caller ~= nil and caller.func == require then return false end
  local info = debug.getinfo(2, "S")
  local src = info and info.source
  if src and src:sub(1, 1) == "@" and arg and arg[0] then
    if src:sub(2) == rawget(_G, "__ok_arg0_raw") then return true end
    local a = fs.fullpath(src:sub(2)):gsub("/", "\\"):lower()
    local b = fs.fullpath(arg[0]):gsub("/", "\\"):lower()
    return a == b
  end
  return true
end

setmetatable(ok, { __index = function(t, k)
  local m = require("ok." .. k)
  rawset(t, k, m)
  return m
end })

-- Convenience re-exports used by nearly every script.
function ok.read_json(p) return ok.json.decode(ok.path.read_text(p)) end
function ok.write_json(p, v, opts) return ok.path.write_text(p, ok.json.encode(v, opts)) end
function ok.print_json(v, opts) io.write(ok.json.encode(v, opts), "\n") end
-- Python-style exit: ok.exit(true|false|code|message)
function ok.exit(v)
  io.stdout:flush()
  if type(v) == "string" then io.stderr:write(v, "\n"); os.exit(1) end
  if v == nil or v == true then os.exit(0) end
  if v == false then os.exit(1) end
  os.exit(v)
end
-- Run main inside error reporting that exits non-zero (like an uncaught Python exception).
function ok.main(fn, ...)
  local res = table.pack(xpcall(fn, debug.traceback, ...))
  if not res[1] then io.stdout:flush(); io.stderr:write(tostring(res[2]), "\n"); os.exit(1) end
  if res[2] ~= nil then ok.exit(res[2]) end
  return table.unpack(res, 2, res.n)
end

return ok
