-- Shared helpers for the crowd launcher / measurement scripts (ported from
-- PowerShell). They keep the PowerShell semantics the launchers rely on:
-- parameter binding with validation (-Name -> --kebab-name), Join-Path /
-- GetFullPath spelling of paths, PowerShell truthiness and comparison rules on
-- JSON reports, Start-Process argument joining, and Win32 process control via
-- ok-luakit (priority, affinity, CPU times, cache topology).
local ok = require "ok"
local json, fs, util = ok.json, ok.fs, ok.util
local M = {}

local function fail(msg) error(msg, 0) end
M.fail = fail

-- [IO.Path]::GetFullPath: absolute, backslashes.
function M.full(p) return (fs.fullpath(tostring(p))) end
-- Join-Path (FileSystem provider): both parts with backslashes, one separator.
function M.join(a, b)
  a = tostring(a):gsub("/", "\\"):gsub("\\+$", "")
  b = tostring(b):gsub("/", "\\"):gsub("^\\+", "")
  return a .. "\\" .. b
end
-- $PSScriptRoot/.. and $PSScriptRoot of the former scripts.
M.root = M.full(ok.root)
M.tools = M.root .. "\\Tools"

-- Local time stamp like Get-Date -Format 'yyyyMMdd<sep>HHmmssfff'.
function M.stamp(sep)
  local t = fs.time()
  local s = math.floor(t)
  local ms = math.min(999, math.floor((t - s) * 1000))
  return os.date("%Y%m%d", s) .. sep .. os.date("%H%M%S", s) .. string.format("%03d", ms)
end
-- [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
function M.unix_ms() return math.floor(fs.time() * 1000) end
-- [DateTime]::UtcNow.ToString('o')
function M.utc_o()
  local t = fs.time()
  local s = math.floor(t)
  return os.date("!%Y-%m-%dT%H:%M:%S", s) .. string.format(".%07dZ", math.min(9999999, math.floor((t - s) * 1e7)))
end
M.clock = fs.clock
M.sleep = fs.sleep

-- Number to string like PowerShell ([string]$double, .NET Framework "G15").
function M.num(v)
  if math.type(v) == "integer" then return tostring(v) end
  if v == math.floor(v) and math.abs(v) < 1e15 then return string.format("%d", v) end
  return (string.format("%.15g", v):gsub("e", "E"))
end

---------------------------------------------------------------------------
-- PowerShell value semantics on decoded JSON (ConvertFrom-Json objects).
local function nul(v) return v == nil or v == json.null end
M.is_null = nul

-- if($v) / [bool]$v
function M.truthy(v)
  if nul(v) or v == false then return false end
  if v == true then return true end
  local t = type(v)
  if t == "number" then return v ~= 0 end
  if t == "string" then return v ~= "" end
  if t == "table" then
    if json.is_object(v) then return true end
    local n = #v
    if n == 0 then return next(v) ~= nil end
    if n == 1 then return M.truthy(v[1]) end
    return true
  end
  return true
end
local T = M.truthy

-- -eq / -ne: the right operand converts to the left operand's type; strings ignore case.
function M.eq(a, b)
  if nul(a) then return nul(b) end
  if nul(b) then return false end
  local ta, tb = type(a), type(b)
  if ta == "boolean" then return a == T(b) end
  if ta == "number" then
    if tb == "boolean" then b = b and 1 or 0
    elseif tb == "string" then b = tonumber(b); if b == nil then return false end
    elseif tb ~= "number" then return false end
    return a == b
  end
  if ta == "string" then
    if tb == "boolean" then b = b and "True" or "False"
    elseif tb == "number" then b = M.num(b)
    elseif tb ~= "string" then return false end
    return a:lower() == b:lower()
  end
  return rawequal(a, b)
end
function M.ne(a, b) return not M.eq(a, b) end

-- -lt/-le/-gt/-ge: $null sorts before everything.
function M.cmp(a, b)
  if nul(a) and nul(b) then return 0 end
  if nul(a) then return -1 end
  if nul(b) then return 1 end
  if type(a) == "string" then
    local sa, sb = a:lower(), (type(b) == "number" and M.num(b) or tostring(b)):lower()
    return sa < sb and -1 or (sa > sb and 1 or 0)
  end
  local na = type(a) == "boolean" and (a and 1 or 0) or a
  local nb = b
  if type(b) == "boolean" then nb = b and 1 or 0 elseif type(b) == "string" then nb = tonumber(b) end
  if type(na) ~= "number" or type(nb) ~= "number" then fail("Cannot compare " .. tostring(a) .. " and " .. tostring(b)) end
  return na < nb and -1 or (na > nb and 1 or 0)
end
function M.lt(a, b) return M.cmp(a, b) < 0 end
function M.le(a, b) return M.cmp(a, b) <= 0 end
function M.gt(a, b) return M.cmp(a, b) > 0 end
function M.ge(a, b) return M.cmp(a, b) >= 0 end
-- $x.Count
function M.count(v)
  if nul(v) then return 0 end
  if type(v) == "table" and not json.is_object(v) then return #v end
  return 1
end
-- Property lookup that ignores case like $obj.Name.
function M.prop(obj, name)
  if type(obj) ~= "table" then return nil end
  local v = obj[name]
  if v ~= nil then return v end
  local low = name:lower()
  for _, k in ipairs(json.keys(obj)) do if type(k) == "string" and k:lower() == low then return obj[k] end end
  return nil
end
-- Property names in order ($obj.PSObject.Properties.Name).
function M.props(obj) return json.keys(obj) end
-- $obj.PSObject.Properties['name'] exists
function M.has(obj, name) return type(obj) == "table" and M.prop(obj, name) ~= nil end

-- Value text as Format-List shows it.
local function show(v)
  if nul(v) then return "" end
  if v == true then return "True" elseif v == false then return "False" end
  if type(v) == "number" then return M.num(v) end
  if type(v) == "table" then
    if json.is_object(v) then
      local parts = {}
      for _, k in ipairs(json.keys(v)) do parts[#parts + 1] = k .. "=" .. show(v[k]) end
      return "@{" .. table.concat(parts, "; ") .. "}"
    end
    local parts = {}
    for i = 1, math.min(#v, 4) do parts[i] = show(v[i]) end
    if #v > 4 then parts[#parts + 1] = "..." end
    return "{" .. table.concat(parts, ", ") .. "}"
  end
  return tostring(v)
end
M.show = show
-- $obj | Select-Object a,b,c  (five or more properties: list view on stdout).
function M.print_select(obj, names)
  local width = 0
  for _, n in ipairs(names) do width = math.max(width, #n) end
  local lines = { "", "" }
  for _, n in ipairs(names) do lines[#lines + 1] = n .. string.rep(" ", width - #n) .. " : " .. show(type(obj) == "table" and obj[n]) end
  lines[#lines + 1] = ""
  lines[#lines + 1] = ""
  io.write(table.concat(lines, "\n"), "\n")
  io.stdout:flush()
end
-- Write-Output
function M.out(s) io.write(tostring(s), "\n"); io.stdout:flush() end

---------------------------------------------------------------------------
-- Files.
-- Get-Content -Raw | ConvertFrom-Json
function M.read_json(p)
  if not ok.path.is_file(p) then fail("Cannot find path '" .. tostring(p) .. "' because it does not exist.") end
  return json.decode(ok.path.read_text(p))
end
-- ConvertTo-Json | Set-Content [-Encoding UTF8]: indented, CRLF, trailing newline (bom for -Encoding UTF8).
function M.write_json(p, v, bom)
  ok.path.write_text(p, json.encode(v, { indent = 4, ensure_ascii = false }) .. "\n", { bom = bom })
end
-- [IO.File]::WriteAllText(p, (v | ConvertTo-Json), UTF8 without BOM): no trailing newline.
function M.write_all_json(p, v)
  ok.path.write_text(p, json.encode(v, { indent = 4, ensure_ascii = false }), {})
end
-- $value | Set-Content p
function M.set_content(p, value) ok.path.write_text(p, tostring(value) .. "\n") end
-- Get-FileHash ... .Hash (upper-case hex).
function M.sha256(p) return ok.hash.file(p, "sha256"):upper() end
function M.sha1(p) return ok.hash.sha1(ok.path.read_bytes(p)):upper() end
-- Test-Path -LiteralPath
function M.exists(p) return p ~= nil and p ~= "" and ok.path.exists(p) end
-- Case-insensitive literal search in a log (Select-String -SimpleMatch).
function M.file_contains(p, needle)
  local data = ok.path.read_bytes(p)
  if data:sub(1, 2) == "\255\254" then data = data:sub(3):gsub("%z", "") end
  return data:lower():find(needle:lower(), 1, true) ~= nil
end
function M.read_text(p)
  local data = ok.path.read_bytes(p)
  if data:sub(1, 2) == "\255\254" then return (data:sub(3):gsub("%z", "")) end
  return (data:gsub("^\239\187\191", ""))
end

---------------------------------------------------------------------------
-- Parameters. spec = { {"Name", kind, default=, set=, range={lo,hi}, pattern=, mandatory=, cli=}, ... }
-- kind: "string" | "int" | "double" | "switch" | "int[]".
-- Switches that default to true, or are marked negatable (bound-ness matters),
-- also accept --no-<name> (PowerShell -Name:$false).
local function kebab(name)
  local s = name:gsub("(%u)(%u%l)", "%1-%2"):gsub("([%l%d])(%u)", "%1-%2"):gsub("(%a)(%d)", "%1-%2")
  return "--" .. s:lower()
end
M.kebab = kebab
local function cli_of(s) return s.cli or kebab(s[1]) end

local function bad(name, v, why)
  fail(("Cannot validate argument on parameter '%s'. %s (value: %s)"):format(name, why, tostring(v)))
end
local function to_int(name, v)
  if type(v) == "boolean" then return v and 1 or 0 end
  local n = tonumber(v)
  if n == nil then fail(("Cannot process argument transformation on parameter '%s'. Cannot convert value \"%s\" to type \"System.Int32\"."):format(name, tostring(v))) end
  if math.type(n) == "float" then n = math.tointeger(util.round(n)) end
  return n
end
local function check_value(s, v)
  local name = s[1]
  if s.range and (v < s.range[1] or v > s.range[2]) then
    bad(name, v, ("The argument is outside the range %s..%s"):format(M.num(s.range[1]), M.num(s.range[2])))
  end
  if s.set then
    local hit
    for _, c in ipairs(s.set) do
      if (type(c) == "string" and type(v) == "string" and c:lower() == v:lower()) or c == v then hit = c end
    end
    if hit == nil then bad(name, v, "The argument does not belong to the set " .. util.join(",", s.set)) end
    v = hit
  end
  if s.pattern and not ok.re.search(s.pattern, v) then bad(name, v, "The argument does not match the pattern " .. s.pattern) end
  return v
end
local function convert(s, v)
  local name, kind = s[1], s[2]
  if kind == "switch" then return T(v) end
  if v == json.null then
    -- $null bound to [string] / [int] / [int[]] becomes '' / 0 / @().
    if kind == "string" then return "" elseif kind == "int[]" then return {} else return kind == "int" and 0 or 0.0 end
  end
  if kind == "string" then
    if type(v) == "number" then v = M.num(v) elseif type(v) == "boolean" then v = v and "True" or "False" end
    return check_value(s, tostring(v))
  end
  if kind == "int" then return check_value(s, to_int(name, v)) end
  if kind == "double" then
    local n = tonumber(v)
    if n == nil then fail(("Cannot convert value \"%s\" to type \"System.Double\" (parameter '%s')"):format(tostring(v), name)) end
    return check_value(s, n + 0.0)
  end
  if kind == "int[]" then
    local list = type(v) == "table" and v or { v }
    local out = {}
    for _, item in ipairs(list) do
      if type(item) == "string" then
        for piece in item:gmatch("[^,%s]+") do out[#out + 1] = check_value(s, to_int(name, piece)) end
      else out[#out + 1] = check_value(s, to_int(name, item)) end
    end
    return out
  end
  error("unknown parameter kind " .. tostring(kind))
end

-- bind(spec, input, script) -> values, bound   (input keyed by PowerShell parameter names)
function M.bind(spec, input, script)
  local known = {}
  for _, s in ipairs(spec) do known[s[1]] = s end
  for k in pairs(input) do
    if not known[k] then fail(("%s: A parameter cannot be found that matches parameter name '%s'."):format(script or "script", tostring(k))) end
  end
  local P, bound = {}, {}
  for _, s in ipairs(spec) do
    local name, v = s[1], input[s[1]]
    if v == nil then
      if s.mandatory then fail(("%s: missing mandatory parameter %s (%s)"):format(script or "script", name, cli_of(s))) end
      v = s.default
      if s[2] == "switch" and v == nil then v = false end
      if type(v) == "table" then v = util.copy(v) end
    else
      bound[name] = true
      v = convert(s, v)
    end
    P[name] = v
  end
  return P, bound
end

-- Command line (`arg`) -> input table keyed by parameter names (values still raw strings).
function M.parse_cli(spec, description, list)
  local p = ok.args.parser(description)
  for _, s in ipairs(spec) do
    local cli = cli_of(s)
    local dest = cli:sub(3):gsub("-", "_")
    s.dest = dest
    local help = s[1] .. (s.default ~= nil and s[2] ~= "switch" and (" (default " .. show(s.default) .. ")") or "")
    if s[2] == "switch" then
      p:flag(cli, { dest = dest, help = help })
      if s.default == true or s.negatable then p:flag("--no-" .. cli:sub(3), { dest = "no_" .. dest }) end
    elseif s[2] == "int[]" then
      p:opt(cli, { dest = dest, nargs = "+", help = help .. " (comma or space separated)" })
    else
      p:opt(cli, { dest = dest, help = help, required = s.mandatory })
    end
  end
  local a = p:parse(list or arg)
  local input = {}
  for _, s in ipairs(spec) do
    if s[2] == "switch" then
      if a[s.dest] then input[s[1]] = true elseif a["no_" .. s.dest] then input[s[1]] = false end
    elseif a[s.dest] ~= nil then input[s[1]] = a[s.dest] end
  end
  return input
end

-- Input table -> command-line list for a child lua.exe (Start-Job of a script).
function M.to_cli(spec, input)
  local out = {}
  for _, s in ipairs(spec) do
    local v = input[s[1]]
    if v ~= nil then
      local cli = cli_of(s)
      if s[2] == "switch" then
        if T(v) then out[#out + 1] = cli elseif s.default == true or s.negatable then out[#out + 1] = "--no-" .. cli:sub(3) end
      elseif s[2] == "int[]" then
        local parts = {}
        for i, x in ipairs(v) do parts[i] = M.num(x) end
        if #parts > 0 then out[#out + 1] = cli; out[#out + 1] = table.concat(parts, ",") end
      else
        out[#out + 1] = cli
        out[#out + 1] = type(v) == "number" and M.num(v) or tostring(v)
      end
    end
  end
  return out
end

---------------------------------------------------------------------------
-- Processes.
-- Start-Process exe -ArgumentList args: Windows PowerShell joins the list with
-- spaces and adds no quoting (arguments carry their own quotes); a list ends
-- with one trailing space like Start-Process builds it.
-- opts: hide (WindowStyle Hidden), cwd. Returns an okfs proc (pid/wait/poll/kill).
function M.command_line(exe, args)
  local tail
  if type(args) == "table" then tail = #args > 0 and (table.concat(args, " ") .. " ") or "" else tail = tostring(args or "") end
  return '"' .. tostring(exe) .. '"' .. (tail ~= "" and (" " .. tail) or "")
end
function M.start_process(exe, args, opts)
  opts = opts or {}
  if not ok.path.is_file(exe) then fail("This command cannot be run because the file '" .. tostring(exe) .. "' does not exist.") end
  return ok.proc.popen(M.command_line(exe, args), { shell = false, stdout = "NUL", stderr = "NUL", hide = opts.hide, cwd = opts.cwd })
end
-- $proc.PriorityClass (get, or set then get).
function M.priority(pid, class)
  return ok.kit.call({ "proc-priority", pid, class }).priority
end
-- $proc.ProcessorAffinity (get, or set then get).
function M.affinity(pid, mask)
  return ok.kit.call({ "proc-affinity", pid, mask and string.format("%d", mask) or nil }).mask
end
-- Get-Process | CPU: {pid -> {name, cpu}}
function M.process_cpu()
  local out = {}
  for _, p in ipairs(ok.kit.call({ "proc-times" })) do out[p.pid] = { name = p.name, cpu = p.cpu_seconds } end
  return out
end
-- Background CPU samples: processes present in `before` whose CPU grew > .01 s,
-- in Get-Process order (by name, then pid): {pid, name, cpu}.
function M.background_cpu(before)
  local rows = {}
  for pid, p in pairs(M.process_cpu()) do
    local b = before[pid]
    if b then
      local delta = p.cpu - b.cpu
      if delta > .01 then rows[#rows + 1] = { pid = pid, name = p.name, cpu = delta } end
    end
  end
  table.sort(rows, function(x, y)
    local a, b = x.name:lower(), y.name:lower()
    if a ~= b then return a < b end
    return x.pid < y.pid
  end)
  return rows
end
-- Get-Process -Id pid; WaitForExit(ms) -> {exists, exited, path}
function M.wait_pid(pid, ms) return ok.kit.call({ "proc-wait", pid, ms }) end
function M.kill_pid(pid) return ok.kit.call({ "proc-kill", pid }).killed end
-- get_crowd_cpu_cache: L3 cache records.
function M.cpu_caches() return ok.kit.call({ "cpu-cache" }) end

return M
