-- Count filtered CPU events; missing child scopes cannot yield exclusive time.
local ok = require "ok"
local json, U = ok.json, ok.util

local function finite(x) return x == x and x ~= math.huge and x ~= -math.huge end

local function count(source, destination, start, finish_limit)
  local order, counts, maxima = {}, {}, {}
  local frames = 0
  for row in ok.csv.iter_dicts(source) do
    local thread = row.ThreadName
    if not (thread:find("GPU", 1, true) or thread:find("Frames", 1, true)) then
      local begin, finish = U.float(row.StartTime), U.float(row.EndTime)
      if finite(begin) and finite(finish) and not (begin < start or finish > finish_limit) then
        local timer = row.TimerName
        if not counts[timer] then order[#order + 1] = timer; counts[timer] = 0; maxima[timer] = 0.0 end
        counts[timer] = counts[timer] + 1
        maxima[timer] = math.max(maxima[timer], (finish - begin) * 1000)
        if thread:sub(1, #"RenderThread") == "RenderThread" and timer == "RenderFinish" then frames = frames + 1 end
      end
    end
  end
  if frames == 0 then error("ValueError: No complete CPU RenderFinish scopes", 0) end
  local scopes = json.object()
  for _, timer in ipairs(order) do
    scopes[timer] = json.object({ { "count", counts[timer] }, { "count_per_frame", counts[timer] / frames }, { "max_inclusive_elapsed_ms", maxima[timer] } })
  end
  local result = json.object({
    { "trace_start_seconds", start }, { "trace_end_seconds", finish_limit }, { "complete_render_frames", frames },
    { "scopes", scopes },
    { "note", "ParallelDraw counts are tasks, not draw calls. Filtered events cannot measure exclusive time. Inclusive elapsed time includes descheduling. Boundary-crossing scopes excluded. GPU queues excluded." } })
  ok.path.write_text(destination, json.encode(result, { indent = 2 }))
end

local p = ok.args.parser()
p:arg("events"); p:arg("destination")
p:opt("--start", { type = U.float, required = true })
p:opt("--end", { type = U.float, required = true })
local a = p:parse(arg)
if a["end"] <= a.start then p:fail("end must be greater than start") end
count(a.events, a.destination, a.start, a["end"])
