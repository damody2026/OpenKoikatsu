-- Replace missing private import libraries in a link response file with
-- libraries rebuilt from the engine DLL exports (dumpbin /exports + lib /def).
local ok = require "ok"
local P, proc = ok.path, ok.proc

local function win(p) return (P.norm(p):gsub("/", "\\")) end
local function check_run(args)
  local res = proc.run(args, { capture = true, text = false })
  if not res.ok then
    error(("Command '%s' returned non-zero exit status %d."):format(proc.cmdline(args), res.code), 0)
  end
  return res
end

local rsp = P.norm(arg[1])
local base = P.join(os.getenv("OK_ENGINE_ROOT") or "E:/PhysX/UE5.8", "Engine/Source")
local compiler = "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64"
local imports = P.parent(rsp) == "." and "Imports" or P.join(P.parent(rsp), "Imports")
if not P.is_dir(imports) then
  local okv, err = require("lfs").mkdir(imports)
  if not okv then error(err, 0) end
end
local text = P.read_text(rsp)
local found = {}
for line in (text .. "\n"):gmatch("([^\n]*)\n") do
  local raw = line:match('^"([^"]+%.lib)"%s*$')
  if raw then found[#found + 1] = raw end
end
for _, raw in ipairs(found) do
  if raw:find("/", 1, true) or raw:find("\\", 1, true) then
    local target = win(P.abs(P.join(base, raw)))
    if not P.exists(target) then
      local root = target:match("^(.-)\\Intermediate\\") or target
      local stem = P.stem(target)
      local dll = P.join(root, "Binaries/Win64", stem .. ".dll")
      if not P.exists(dll) then error("Missing DLL for " .. target, 0) end
      local result = check_run({ win(P.join(compiler, "dumpbin.exe")), "/exports", win(dll) })
      local symbols = {}
      for l in result.stdout:gsub("\r\n", "\n"):gsub("\r", "\n"):gmatch("([^\n]*)") do
        local m = l:match("^%s*%d+%s+%x+%s+%x+%s+(%S+)")
        if m and m ~= "[NONAME]" then symbols[#symbols + 1] = m end
      end
      if #symbols == 0 then error("No exports " .. win(dll), 0) end
      local definition = P.join(imports, stem .. ".def")
      P.write_text(definition, 'LIBRARY "' .. P.name(dll) .. '"\nEXPORTS\n' .. table.concat(symbols, "\n") .. "\n")
      local lib = P.join(imports, P.name(target))
      result = proc.run({ win(P.join(compiler, "lib.exe")), "/nologo", "/machine:x64", "/def:" .. win(definition), "/out:" .. win(lib) }, { capture = true, text = false })
      P.write_bytes(P.join(imports, stem .. ".log"), (result.stdout or "") .. (result.stderr or ""))
      if not result.ok then error(("Command returned non-zero exit status %d."):format(result.code), 0) end
      text = ok.util.replace(text, '"' .. raw .. '"', '"' .. lib .. '"')
      print("Private import: " .. P.name(dll))
    end
  end
end
P.write_text(rsp, text)
