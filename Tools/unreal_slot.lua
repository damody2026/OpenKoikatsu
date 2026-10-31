-- Machine-wide Unreal slots shared by every agent and checkout (C:, E:, F:): at most three Unreal processes at once.
-- slotN\owner.txt names who holds a slot (first word = agent), queue\<agent>.txt says who is waiting.
--
--   Tools\lua\lua.exe Tools\unreal_slot.lua claim <agent> <note>    take a free slot, or join the queue (exit 2)
--   Tools\lua\lua.exe Tools\unreal_slot.lua release <agent>         give back every slot the agent holds
--   Tools\lua\lua.exe Tools\unreal_slot.lua yield <agent>           exit 3 when someone of higher priority is waiting
--   Tools\lua\lua.exe Tools\unreal_slot.lua status
--
-- claim only succeeds when no agent of higher priority is queued, a slot is free and fewer than three Unreal processes
-- run; it re-reads owner.txt after writing and removes the agent's own queue entry in the same step, so a stale queue
-- file never makes lower-priority agents wait for someone who already runs.
local ok = require "ok"
local path, proc = ok.path, ok.proc

local ROOT = "C:/portable/OpenKoikatsu/Saved/UnrealSlots"
local SLOTS = 3
-- Lower runs first; agents not listed rank after these.
local PRIORITY = { F = 1, ["e-galgame"] = 2, ["c-conquest"] = 3, ["e-okcard"] = 3, ["c-street-city"] = 4 }

local function rank(agent) return PRIORITY[agent] or 9 end
local function owner_file(i) return path.join(ROOT, "slot" .. i, "owner.txt") end
local function queue_file(agent) return path.join(ROOT, "queue", agent .. ".txt") end

local function owner(i)
  local f = owner_file(i)
  if not path.is_file(f) then return nil end
  local text = path.read_text(f) or ""
  local who = text:match("^%s*(%S+)")
  return who, text
end

-- Unreal processes on the machine; the launcher stub at a stage root (…\Windows\OpenKoikatsu.exe) is not counted.
local function unreal_count()
  local out = proc.output({ "powershell", "-NoProfile", "-Command",
    "Get-CimInstance Win32_Process | ?{$_.Name -match '^(UnrealEditor(-Cmd)?|OpenKoikatsu|StreetSocialCity(-Cmd)?)\\.exe$'} | %{$_.ExecutablePath}" }) or ""
  local n = 0
  for line in out:gmatch("[^\r\n]+") do
    if not line:lower():match("\\windows\\[^\\]+%.exe$") then n = n + 1 end
  end
  return n
end

local function queued()
  local list = {}
  local dir = path.join(ROOT, "queue")
  if path.is_dir(dir) then
    for _, n in ipairs(path.list(dir)) do
      local agent = n:match("^(.-)%.txt$")
      if agent then list[#list + 1] = agent end
    end
  end
  return list
end

local function waiting_above(agent)
  for _, other in ipairs(queued()) do
    if other ~= agent and rank(other) < rank(agent) then return other end
  end
end

local function enqueue(agent, note)
  path.mkdir(path.join(ROOT, "queue"), true)
  if not path.is_file(queue_file(agent)) then
    path.write_text(queue_file(agent), agent .. " " .. note .. " " .. os.date("%Y-%m-%d %H:%M:%S") .. "\n")
  end
end

local function claim(agent, note)
  local held
  for i = 1, SLOTS do if owner(i) == agent then held = i end end
  local blocker = waiting_above(agent)
  if not held and blocker then
    enqueue(agent, note)
    io.write("queued: ", blocker, " has higher priority and is waiting\n")
    return 2
  end
  if not held then
    if unreal_count() >= SLOTS then enqueue(agent, note); io.write("queued: ", SLOTS, " Unreal processes already run\n"); return 2 end
    for i = 1, SLOTS do
      if not owner(i) then
        path.mkdir(path.parent(owner_file(i)), true)
        path.write_text(owner_file(i), agent .. " " .. note .. " " .. os.date("%Y-%m-%d %H:%M:%S") .. "\n")
        if owner(i) == agent then held = i; break end
      end
    end
  end
  if not held then enqueue(agent, note); io.write("queued: no free slot\n"); return 2 end
  if path.is_file(queue_file(agent)) then path.remove(queue_file(agent)) end
  io.write("slot", held, "\n")
  return 0
end

local function release(agent)
  local n = 0
  for i = 1, SLOTS do
    if owner(i) == agent then path.write_text(owner_file(i), ""); n = n + 1 end
  end
  io.write("released ", n, " slot(s)\n")
  return 0
end

local function status()
  for i = 1, SLOTS do
    local who, text = owner(i)
    io.write("slot", i, ": ", who and (text:gsub("%s+$", "")) or "(free)", "\n")
  end
  io.write("queue: ", table.concat(queued(), ", "), "\n")
  io.write("unreal processes: ", unreal_count(), "\n")
  return 0
end

local function main()
  local cmd, agent = arg[1], arg[2]
  local note = table.concat(arg, " ", 3)
  if cmd == "claim" and agent then os.exit(claim(agent, note))
  elseif cmd == "release" and agent then os.exit(release(agent))
  elseif cmd == "yield" and agent then
    local other = waiting_above(agent)
    if other then io.write("yield to ", other, "\n"); os.exit(3) end
    io.write("keep\n"); os.exit(0)
  elseif cmd == "status" then os.exit(status())
  else error("usage: unreal_slot.lua claim|release|yield <agent> [note] | status", 0) end
end

ok.main(main)
