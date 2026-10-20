-- CPU-only scope costs from an unfiltered TimingInsights event export.
--
-- Export every timer (*) for a bounded window. Filtered exports cannot give
-- exclusive costs because omitted children would be charged to their parents.
-- Thread totals overlap; never add them to obtain frame time.
local ok = require "ok"
local U = ok.util

local function fail(msg) error("ValueError: " .. msg, 0) end
local function int(s) return math.tointeger(tonumber(s)) or fail("invalid literal for int() with base 10: '" .. tostring(s) .. "'") end

local function summarize(rows, start, stop)
  local threads, thread_order = {}, {}
  for row in rows do
    local name = row.ThreadName
    if name:sub(1, 3) == "GPU" or name:sub(-7) == " Frames" then fail("Expected a CPU-only event export") end
    local a, b = math.max(start, U.float(row.StartTime)), math.min(stop, U.float(row.EndTime))
    if b > a then
      local key = row.ThreadId .. "\0" .. name
      local t = threads[key]
      if not t then t = { id = row.ThreadId, name = name, events = {} }; threads[key] = t; thread_order[#thread_order + 1] = t end
      local events = t.events
      events[#events + 1] = { a, b, int(row.Depth), row.TimerName, 0.0, "<root>", #events + 1 }
    end
  end
  local totals, order = {}, {}
  for _, thread in ipairs(thread_order) do
    local events = thread.events
    table.sort(events, function(x, y)
      if x[1] ~= y[1] then return x[1] < y[1] end
      if x[3] ~= y[3] then return x[3] < y[3] end
      if x[2] ~= y[2] then return x[2] > y[2] end
      return x[7] < y[7]
    end)
    local stack = {}
    for _, event in ipairs(events) do
      while #stack > 0 and (stack[#stack][2] <= event[1] or stack[#stack][3] >= event[3]) do stack[#stack] = nil end
      if #stack > 0 then
        local parent = stack[#stack]
        if event[2] > parent[2] + 1e-7 then fail("Crossing scopes: event export is not a nested CPU timeline") end
        parent[5] = parent[5] + (event[2] - event[1])
        event[6] = parent[4]
      end
      stack[#stack + 1] = event
    end
    for _, e in ipairs(events) do
      local a, b, timer, children, parent = e[1], e[2], e[4], e[5], e[6]
      local duration = b - a
      if children > duration + 1e-7 then fail("Overlapping child scopes; exclusive cost is unreliable") end
      local key = thread.id .. "\0" .. thread.name .. "\0" .. timer .. "\0" .. parent
      local result = totals[key]
      if not result then result = { key = { thread.id, thread.name, timer, parent }, 0, 0.0, 0.0, 0.0 }; totals[key] = result; order[#order + 1] = result end
      result[1] = result[1] + 1
      result[2] = result[2] + duration
      result[3] = result[3] + math.max(0.0, duration - children)
      result[4] = math.max(result[4], duration)
    end
  end
  return order
end

local function main()
  local p = ok.args.parser("CPU-only scope costs from an unfiltered TimingInsights event export.")
  p:arg("events")
  p:opt("--start", { type = U.float, required = true })
  p:opt("--end", { type = U.float, required = true })
  p:opt("--output", { required = true })
  p:opt("--thread-inventory", { help = "Filter an unfiltered -threads=* export by metadata IDs; preserves duplicate CPU names" })
  p:flag("--all-timers", { help = "Confirm the input was exported with -timers=*" })
  local args = p:parse(arg)
  if not args.all_timers then p:fail("the following arguments are required: --all-timers") end
  if args["end"] <= args.start then p:fail("end must exceed start") end
  if ok.path.exists(args.output) then p:fail("Use a fresh output path") end
  local rows = ok.csv.iter_dicts(args.events)
  local selected = rows
  if args.thread_inventory then
    local known, cpu = {}, {}
    for row in ok.csv.iter_dicts(args.thread_inventory) do
      known[row.Id] = true
      if row.Group ~= "GPU" and row.Group ~= "Frame Tracks" and row.Group ~= "Verse"
          and row.Name:sub(1, 3) ~= "GPU" and row.Name:sub(-7) ~= " Frames" then cpu[row.Id] = true end
    end
    selected = function()
      while true do
        local row = rows()
        if row == nil then return nil end
        if not known[row.ThreadId] then fail("Event thread missing from metadata inventory: " .. row.ThreadId) end
        if cpu[row.ThreadId] then return row end
      end
    end
  end
  local totals = summarize(selected, args.start, args["end"])
  local w = ok.csv.writer()
  w:row({ "ThreadId", "ThreadName", "TimerName", "ParentTimer", "Count", "InclusiveMs", "ExclusiveMs", "MeanInclusiveMs", "MaxInclusiveMs" })
  local sorted = U.sorted(totals, function(r) return { r.key[1], -r[3] } end)
  for _, r in ipairs(sorted) do
    local k = r.key
    w:row({ k[1], k[2], k[3], k[4], r[1], r[2] * 1000, r[3] * 1000, r[2] / r[1] * 1000, r[4] * 1000 })
  end
  w:save(args.output)
end

ok.main(main)
