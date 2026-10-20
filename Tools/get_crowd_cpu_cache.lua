-- L3 cache topology for the crowd LargestL3 affinity trial (port of
-- get_crowd_cpu_cache.ps1). Windows SDK CACHE_RELATIONSHIP records with a
-- single group mask; the record walk lives in ok-luakit `cpu-cache`.
--   Tools\lua\lua.exe Tools\get_crowd_cpu_cache.lua [--json]
-- Prints the records as the PowerShell table (CacheBytes, Mask, Group,
-- LogicalProcessors); --json prints them as JSON. require() returns M.read().
local ok = require "ok"
local C = require "crowd_launch_common"

local M = {}
M.columns = { "CacheBytes", "Mask", "Group", "LogicalProcessors" }

function M.read() return C.cpu_caches() end

-- Format-Table of the numeric records (right-aligned columns).
function M.table_text(rows)
  local widths = {}
  for i, c in ipairs(M.columns) do
    widths[i] = #c
    for _, r in ipairs(rows) do widths[i] = math.max(widths[i], #C.num(r[c])) end
  end
  local function line(cells)
    local out = {}
    for i, s in ipairs(cells) do out[i] = string.rep(" ", widths[i] - #s) .. s end
    return table.concat(out, " ")
  end
  local dashes = {}
  for i, c in ipairs(M.columns) do dashes[i] = string.rep("-", #c) end
  local lines = { "", line(M.columns), line(dashes) }
  for _, r in ipairs(rows) do
    local cells = {}
    for i, c in ipairs(M.columns) do cells[i] = C.num(r[c]) end
    lines[#lines + 1] = line(cells)
  end
  return table.concat(lines, "\n") .. "\n\n\n"
end

function M.main()
  local p = ok.args.parser("L3 cache records (CacheBytes, Mask, Group, LogicalProcessors).")
  p:flag("--json")
  local a = p:parse(arg)
  local rows = M.read()
  if a.json then ok.print_json(rows) elseif #rows > 0 then io.write(M.table_text(rows)) end
  return 0
end

if ok.is_main() then ok.main(M.main) end
return M
