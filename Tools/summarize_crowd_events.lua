-- Aggregate exported timing events without mixing GPU queues into CPU threads.
--
-- UE 5.8 ExportTimerStatistics includes GPU queues even with a CPU thread filter.
-- Use the event export for attribution; inclusive times overlap across threads.
local ok = require "ok"
local U = ok.util

local function finite(x) return x == x and x ~= math.huge and x ~= -math.huge end

local function aggregate(source, destination)
  local totals, order = {}, {}
  local stacks, thread_order = {}, {}
  local function stack_of(thread)
    local s = stacks[thread]
    if not s then s = {}; stacks[thread] = s; thread_order[#thread_order + 1] = thread end
    return s
  end
  local function finish(thread)
    local s = stacks[thread]
    local event = table.remove(s)
    local duration = math.max(0.0, event[3] - event[2])
    local key = thread .. "\0" .. event[1]
    local row = totals[key]
    if not row then row = { thread, event[1], 0, 0.0, 0.0 }; totals[key] = row; order[#order + 1] = row end
    row[3] = row[3] + 1
    row[4] = row[4] + duration
    row[5] = row[5] + math.max(0.0, duration - event[5])
    if #s > 0 then
      local parent = s[#s]
      parent[5] = parent[5] + math.max(0.0, math.min(event[3], parent[3]) - math.max(event[2], parent[2]))
    end
  end
  for event in ok.csv.iter_dicts(source) do
    local thread = event.ThreadName
    if not thread:find("Frames", 1, true) then
      local start, stop = U.float(event.StartTime), U.float(event.EndTime)
      -- Trace.Stop can leave open scopes; their exported end is infinity.
      -- They cannot provide a measured duration and must be excluded.
      if finite(start) and finite(stop) then
        local depth = math.tointeger(tonumber(event.Depth)) or error("ValueError: invalid literal for int(): " .. tostring(event.Depth), 0)
        local s = stack_of(thread)
        while #s > 0 and (s[#s][4] >= depth or s[#s][3] <= start) do finish(thread) end
        s[#s + 1] = { event.TimerName, start, stop, depth, 0.0 }
      end
    end
  end
  for _, thread in ipairs(thread_order) do
    while #stacks[thread] > 0 do finish(thread) end
  end
  local w = ok.csv.writer()
  w:row({ "thread", "timer", "count", "inclusive_ms", "exclusive_ms", "inclusive_mean_ms" })
  for _, r in ipairs(U.sorted(order, function(r) return r[5] end, true)) do
    w:row({ r[1], r[2], r[3], r[4] * 1000, r[5] * 1000, r[4] * 1000 / r[3] })
  end
  w:save(destination)
end

local p = ok.args.parser()
p:arg("source"); p:arg("destination")
local a = p:parse(arg)
aggregate(a.source, a.destination)
