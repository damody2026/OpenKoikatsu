-- The machine-wide Unreal slots (at most three Unreal instances): queue for one, take a free one, give it back.
-- Taking a slot and leaving the queue are one step, so a stale queue file never makes a lower-priority agent think
-- someone is still waiting ahead of it.
--
--   Tools\lua\lua.exe Tools\unreal_slot.lua status
--   Tools\lua\lua.exe Tools\unreal_slot.lua claim <name> "<what for>" [--wait]   (queues, then takes a free slot)
--   Tools\lua\lua.exe Tools\unreal_slot.lua release <name>                      (frees the slot and the queue file)
local ROOT = os.getenv("OK_UNREAL_SLOTS") or "C:/portable/OpenKoikatsu/Saved/UnrealSlots"
local SLOTS = 3

local function read(path)
  local f = io.open(path, "rb")
  if not f then return "" end
  local s = f:read("a"); f:close()
  return s
end
local function write(path, text)
  local f = assert(io.open(path, "wb")); f:write(text); f:close()
end
local function owner(i) return ROOT .. "/slot" .. i .. "/owner.txt" end
local function queue_file(name) return ROOT .. "/queue/" .. name .. ".txt" end
local function blank(s) return not s:find("%S") end

-- Unreal processes running anywhere on the machine (editor, game and commandlet builds).
local function unreal_count()
  local n = 0
  for _, exe in ipairs({ "UnrealEditor.exe", "UnrealEditor-Cmd.exe" }) do
    local p = io.popen(('tasklist /FI "IMAGENAME eq %s" /NH 2>nul'):format(exe))
    for line in p:lines() do if line:lower():find(exe:lower(), 1, true) then n = n + 1 end end
    p:close()
  end
  return n
end

local function now() return os.date("%Y-%m-%dT%H:%M:%S") end

-- The slot `name` holds, or a free one (only while fewer than SLOTS Unreal instances run); nil when none.
local function pick(name)
  for i = 1, SLOTS do
    if read(owner(i)):sub(1, #name + 1) == name .. " " then return i, true end
  end
  if unreal_count() >= SLOTS then return nil end
  for i = 1, SLOTS do if blank(read(owner(i))) then return i, false end end
end

local cmd, name, note = arg[1], arg[2], arg[3]
if cmd == "status" or not cmd then
  for i = 1, SLOTS do print(("slot%d: %s"):format(i, blank(read(owner(i))) and "(free)" or read(owner(i)):gsub("%s+$", ""))) end
  print("unreal running: " .. unreal_count())
  return
end
assert(name and name:match("^[%w%-_]+$"), "a name (letters, digits, - or _) is required")
if cmd == "claim" then
  local wait = arg[4] == "--wait" or arg[3] == "--wait"
  if note == "--wait" then note = nil end
  write(queue_file(name), ("%s %s"):format(note or name, now()))
  while true do
    local i, mine = pick(name)
    if i then
      if not mine then
        write(owner(i), ("%s %s %s"):format(name, note or "", now()))
        -- Another agent may have written the same slot between our read and write: keep it only if it is ours.
        if read(owner(i)):sub(1, #name + 1) ~= name .. " " then i = nil end
      end
      if i then
        os.remove(queue_file(name))
        print(("slot%d %s"):format(i, mine and "already held" or "claimed"))
        return
      end
    end
    if not wait then print("no free slot (queued as " .. name .. ")"); os.exit(1) end
    os.execute("ping -n 21 127.0.0.1 >nul")
  end
elseif cmd == "release" then
  local freed = false
  for i = 1, SLOTS do
    if read(owner(i)):sub(1, #name + 1) == name .. " " then write(owner(i), ""); freed = true; print(("slot%d released"):format(i)) end
  end
  os.remove(queue_file(name))
  if not freed then print("no slot held by " .. name) end
else
  error("unknown command " .. cmd .. " (status, claim, release)")
end
