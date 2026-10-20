-- Builds Tools/lua/okfs.dll (UTF-8 path primitives) with MinGW-w64 GCC.
-- Needs the Lua 5.4.8 headers: pass --lua-src <dir containing lua.h> (from
-- https://www.lua.org/ftp/lua-5.4.8.tar.gz, SHA-256 4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae).
-- A running lua.exe keeps okfs.dll locked; the old file is renamed aside (allowed
-- while loaded) and the new one moved into place.
--   Tools\lua\lua.exe Tools\lua\src\build_okfs.lua --lua-src <lua-5.4.8\src> [--gcc gcc]
-- This script itself runs on the okfs.dll it replaces: the loaded copy keeps working after the rename.
local ok = require "ok"
local path = ok.path

local function win(p) return (tostring(p):gsub("/", "\\")) end

local function main()
  local p = ok.args.parser("Build Tools/lua/okfs.dll with MinGW-w64 GCC")
  p:opt("--lua-src", { required = true })
  p:opt("--gcc", { default = "gcc" })
  local a = p:parse(arg)
  local src = win(ok.script_dir)
  local lua = win(path.parent(ok.script_dir))
  local dll = lua .. "\\okfs.dll"
  local new = lua .. "\\okfs.new.dll"
  local res = ok.proc.run({ a.gcc, "-O2", "-shared", "-s", "-o", new, src .. "\\okfs.c", "-I" .. a.lua_src,
    lua .. "\\lua54.dll", "-lkernel32", "-lshell32" })
  if not res.ok then error("okfs build failed", 0) end
  if path.exists(dll) then
    local aside = lua .. "\\okfs.old-" .. os.date("%Y%m%d%H%M%S") .. ".dll"
    path.rename(dll, aside)
    for _, old in ipairs(path.glob(lua, "okfs.old-*.dll")) do
      if win(old):lower() ~= aside:lower() then pcall(path.remove, old) end
    end
  end
  path.rename(new, dll)
  print(ok.hash.sha256_file(dll):upper())
end

ok.main(main)
