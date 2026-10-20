-- Carry a street game's saved state from a heroine's old card identity to the new one.
--
-- A heroine's identity is apng-<sha256 of her card file>, so replacing or reconverting a card (R3-10) gives her a new
-- id: her name override, Profile and everything a game saved about her (relation, bonds, rumours, appointments, studies,
-- mind memories, chat history) would stay with the old id. When a card is replaced, add "old id": "new id" to
-- Content/IndependentResources/StreetSocialData/identity_aliases.json (and rename her Profile / name override); this
-- moves every saved game to the new id. The launcher (run_street_social_development.lua) runs it before each start.
--
--   lua.exe Tools/street_identity_migrate.lua [--game DIR]... [--aliases JSON] [--dry-run]
--     Games: the live game (Saved/StreetSocial) and every save slot (Saved/StreetSocial/Saves/*) unless --game names
--     them. Only with the game closed: it rewrites the files the game keeps open.
--
-- What moves: every JSON file of a game (relationships.json, its .bak, phone.json, free-talk-*.json, and accounts/,
-- history/, mind/) has the old id replaced in keys and strings; "a|b" pair keys stay sorted. Where the new id already
-- has its own entry (a game run after the card changed made her a stranger again), the old one wins: the same key, a
-- relations / free-talk entry with the same "id" / "character", and files named by the id (mind/*/<id>.json). Chat
-- histories (history/<id>*.jsonl) are joined, old lines first. The files it changes are copied first to
-- <game>/identity-backup-<time>/ (outside the folders a save slot copies).
local ok = require "ok"
local json, path = ok.json, ok.path

local FILES = { "relationships.json", "relationships.json.bak", "phone.json", "free-talk-log.json", "free-talk-memory.json" }
local DIRS = { "accounts", "history", "mind" }
local ID = "apng%-" .. string.rep("%x", 64)
-- Arrays whose objects are one per character, by this field.
local KEYED = { "id", "character" }

local function aliases(file)
  local map = {}
  if not path.is_file(file) then return map end
  local raw = json.decode(path.read_text(file)).aliases or {}
  for old, new in pairs(raw) do
    if not (old:match("^" .. ID .. "$") and new:match("^" .. ID .. "$")) or old == new then error("identity_aliases: bad pair " .. old .. " -> " .. tostring(new), 0) end
    map[old] = new
  end
  -- A card replaced twice: the oldest id goes straight to the newest.
  for old in pairs(map) do
    local seen, id = { [old] = true }, map[old]
    while map[id] do
      if seen[id] then error("identity_aliases: cycle at " .. id, 0) end
      seen[id] = true; id = map[id]
    end
    map[old] = id
  end
  return map
end

local function rename(s, map)
  local out = s:gsub(ID, function(id) return map[id] or id end)
  local a, b = out:match("^(" .. ID .. ")|(" .. ID .. ")$")
  if a and a > b then out = b .. "|" .. a end
  return out
end

-- The ids an object is the per-character entry of (its KEYED field), or nil.
local function keyed(v)
  if not json.is_object(v) then return nil end
  for _, k in ipairs(KEYED) do local id = rawget(v, k); if type(id) == "string" and id:match("^" .. ID .. "$") then return id end end
end

local function migrate(v, map, olds)
  if type(v) == "string" then return rename(v, map) end
  if json.is_object(v) then
    local keys = json.keys(v)
    local moved = {}
    for _, k in ipairs(keys) do local r = rename(k, map); if r ~= k then moved[r] = true end end
    local out = json.object()
    for _, k in ipairs(keys) do
      local r = rename(k, map)
      -- The new id's own entry gives way to the one carried from the old id.
      if not (r == k and moved[k]) then out[r] = migrate(rawget(v, k), map, olds) end
    end
    return out
  end
  if json.is_array(v) then
    local carried = {}
    for _, e in ipairs(v) do local id = keyed(e); if id and map[id] then carried[map[id]] = true end end
    local out, strings = json.array({}), {}
    for _, e in ipairs(v) do
      local id = keyed(e)
      if not (id and carried[id]) then
        local m = migrate(e, map, olds)
        -- Lists of ids (who heard a rumour) keep one of each.
        if type(m) == "string" and m:match("^" .. ID .. "$") then
          if not strings[m] then strings[m] = true; out[#out + 1] = m end
        else out[#out + 1] = m end
      end
    end
    return out
  end
  return v
end

local function mentions(text, olds)
  for old in pairs(olds) do if text:find(old, 1, true) then return true end end
  return false
end

-- Every file of a game that holds or is named by an old id.
local function affected(game, olds)
  local out = {}
  local function consider(file)
    if path.name(file):match("^trace.*%.jsonl$") then return end
    if mentions(path.name(file), olds) or mentions(path.read_bytes(file), olds) then out[#out + 1] = file end
  end
  for _, f in ipairs(FILES) do local p = path.join(game, f); if path.is_file(p) then consider(p) end end
  for _, d in ipairs(DIRS) do
    local dir = path.join(game, d)
    if path.is_dir(dir) then path.walk(dir, function(at, _, files) for _, f in ipairs(files) do consider(path.join(at, f)) end end) end
  end
  return out
end

local function migrate_game(game, map, dry)
  local olds = {}
  for old in pairs(map) do olds[old] = true end
  local files = affected(game, olds)
  if #files == 0 then return 0 end
  print(("%s: %d file(s) name an old id"):format(game, #files))
  if dry then for _, f in ipairs(files) do print("  " .. path.rel(f, game)) end; return #files end
  local backup = path.join(game, "identity-backup-" .. os.date("%Y%m%dT%H%M%S"))
  for _, f in ipairs(files) do
    local copy = path.join(backup, path.rel(f, game))
    path.mkdir(path.parent(copy), true)
    path.copy(f, copy)
  end
  for _, f in ipairs(files) do
    local target = path.join(path.parent(f), rename(path.name(f), map))
    if path.suffix(f):lower() == ".jsonl" then
      -- Chat history: the old lines, then the lines the new id already has.
      local text = rename(path.read_bytes(f), map)
      if target ~= f and path.is_file(target) then text = text .. path.read_bytes(target) end
      path.write_bytes(target, text, { atomic = true })
    else
      local value = migrate(json.decode(path.read_text(f)), map, olds)
      path.write_text(target, json.encode(value, { ensure_ascii = false, indent = 2 }) .. "\n", { atomic = true })
    end
    if target ~= f then path.remove(f) end
    print("  " .. path.rel(f, game) .. (target ~= f and (" -> " .. path.name(target)) or ""))
  end
  print("  backup: " .. backup)
  return #files
end

ok.main(function()
  local games, dry = {}, false
  local alias_file = path.join(ok.root, "Content/IndependentResources/StreetSocialData/identity_aliases.json")
  local i = 1
  while arg[i] do
    if arg[i] == "--game" then games[#games + 1] = arg[i + 1]; i = i + 2
    elseif arg[i] == "--aliases" then alias_file = arg[i + 1]; i = i + 2
    elseif arg[i] == "--dry-run" then dry = true; i = i + 1
    else error("usage: street_identity_migrate.lua [--game DIR]... [--aliases JSON] [--dry-run]", 0) end
  end
  local map = aliases(alias_file)
  if next(map) == nil then return end
  if #games == 0 then
    local live = path.join(ok.root, "Saved/StreetSocial")
    if path.is_file(path.join(live, "relationships.json")) then games[#games + 1] = live end
    for _, slot in ipairs(path.glob(path.join(live, "Saves"), "*")) do if path.is_dir(slot) then games[#games + 1] = slot end end
  end
  local total = 0
  for _, game in ipairs(games) do total = total + migrate_game(game, map, dry) end
  if total == 0 then print("identity aliases: no saved game names an old id") end
end)
