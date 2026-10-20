-- Static checks for the repository's Lua scripts.
--   Tools\lua\lua.exe Tools\lua\lint.lua [files...]   (default: every tracked/new .lua under Tools/)
-- 1. luacheck (vendored): undefined variables and accidental globals (W11x), syntax errors.
-- 2. ok API: every ok.<module>.<name> (and `local x = ok.<module>` aliases) must exist.
-- Exit code 1 when anything is reported.
local ok = require "ok"
local vendor = ok.root .. "/Tools/lua/vendor"

local files = {}
for i = 1, #arg do files[#files + 1] = arg[i] end
if #files == 0 then
  local res = ok.proc.run({ "git", "ls-files", "-c", "-o", "--exclude-standard", "--", "*.lua" }, { capture = true, cwd = ok.root })
  for line in res.stdout:gmatch("[^\n]+") do
    if not line:find("^old_py/") and not line:find("^Tools/lua/vendor/") then files[#files + 1] = ok.root .. "/" .. line end
  end
end

-- 1. luacheck in a child process (it calls os.exit).
local list = ok.path.temp(".txt")
ok.path.write_text(list, table.concat(files, "\n") .. "\n", { newline = "\n" })
local runner = ok.path.temp(".lua")
ok.path.write_text(runner, ([[
package.path = %q .. "/?.lua;" .. %q .. "/?/init.lua;" .. package.path
local files = {}
for l in io.lines(%q) do if l ~= "" then files[#files + 1] = l end end
arg = { [0] = "luacheck", "--std", "lua54", "--only", "0", "11", "--codes", "--no-color", "--formatter", "plain", table.unpack(files) }
require("luacheck.main")
]]):format(vendor, vendor, list), { newline = "\n" })
local res = ok.proc.run({ ok.lua_exe, runner }, { capture = true })
ok.path.remove(list, true); ok.path.remove(runner, true)
local problems = 0
for line in (res.stdout .. res.stderr):gmatch("[^\n]+") do problems = problems + 1; print(line) end

-- 2. ok API references.
local modules = {}
for _, f in ipairs(ok.path.glob(ok.root .. "/Tools/lua/ok", "*.lua")) do
  local m = ok.path.stem(f)
  if m ~= "init" then modules[m] = ok[m] end
end
local top = {}
for k in pairs(ok) do top[k] = true end
local function strip(line)
  return (line:gsub("\\.", "__"):gsub('"[^"]*"', '""'):gsub("'[^']*'", "''"):gsub("%[%[.-%]%]", "''"):gsub("%-%-.*$", ""))
end
for _, file in ipairs(files) do
  local src = ok.path.read_text(file)
  local alias = {}
  for name, m in src:gmatch("local%s+([%w_]+)%s*=%s*ok%.([%w_]+)%s*[\n;]") do if modules[m] then alias[name] = m end end
  for names, mods in src:gmatch("local%s+([%w_,%s]+)%s*=%s*(ok%.[%w_]+%s*,[%w_%.%s,]+)") do
    local ns, ms = {}, {}
    for n in names:gmatch("[%w_]+") do ns[#ns + 1] = n end
    for m in mods:gmatch("ok%.([%w_]+)") do ms[#ms + 1] = m end
    for i, n in ipairs(ns) do if ms[i] and modules[ms[i]] then alias[n] = ms[i] end end
  end
  local lineno = 0
  for line in (src .. "\n"):gmatch("(.-)\n") do
    lineno = lineno + 1
    local code = " " .. strip(line)
    for m, name in code:gmatch("[^%w_.]ok%.([%w_]+)%.([%w_]+)") do
      if modules[m] and modules[m][name] == nil then
        problems = problems + 1; print(("%s:%d: ok.%s.%s does not exist"):format(file, lineno, m, name))
      end
    end
    for m in code:gmatch("[^%w_.]ok%.([%w_]+)") do
      if not modules[m] and not top[m] then
        problems = problems + 1; print(("%s:%d: ok.%s does not exist"):format(file, lineno, m))
      end
    end
    for a, name in code:gmatch("[^%w_.]([%w_]+)%.([%w_]+)") do
      local m = alias[a]
      if m and modules[m][name] == nil and not code:find("function%s+" .. a .. "%." .. name) then
        problems = problems + 1; print(("%s:%d: %s.%s (ok.%s) does not exist"):format(file, lineno, a, name, m))
      end
    end
  end
end
print(("%d files, %d problems"):format(#files, problems))
os.exit(problems == 0 and 0 or 1)
