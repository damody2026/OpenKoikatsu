-- Development client for the StreetSocial control channel (game started with run_street_social_development.lua -Control).
-- The channel is HTTP on 127.0.0.1:%OK_CONTROL_PORT%, else this checkout's port (local-instance.json,
-- Tools/ok_instance.lua; 47870 without it); requests go through curl.exe.
local ok = require "ok"
local json, util, proc = ok.json, ok.util, ok.proc
local T = util.truthy

local DOC = [[Development client for the StreetSocial control channel (game started with run_street_social_development.lua -Control).

  street_play.lua state [--raw]          what is going on (compact summary; --raw for the full JSON)
  street_play.lua phone TEXT...          type into the open phone thread and send
  street_play.lua clock pause | run [S]  the game clock; run S real seconds, then pause
  street_play.lua talk TEXT...           say it with the clock running until she has answered; prints her lines
  street_play.lua goto girl N | place ID [MAX]   walk there with the clock running, then pause
  street_play.lua outing [MAX]           let the clock run until the walk with her ends
  street_play.lua until HH:MM [MAX]      let the clock run until that time
  street_play.lua say TEXT...            he says something to her
  street_play.lua buttons                labels of the buttons on screen
  street_play.lua click LABEL...         press the button with this label
  street_play.lua key E [seconds]        press a key (E, F, Escape, W/A/S/D with seconds to hold, LeftShift, One..Four)
  street_play.lua walk W 3               hold a movement key for N seconds (real walking)
  street_play.lua look YAW [PITCH]       point the camera
  street_play.lua nav girl N | place ID | stop
  street_play.lua shot                   screenshot; prints the png path
  street_play.lua wait SECONDS           just wait (the game keeps its own real-time clock)
  street_play.lua rules [reload | eval JSON]  the game rules DLL: status, hot reload, or ask it directly
  street_play.lua mic off|on               off: what the room microphone hears is ignored (testing with people nearby)
  street_play.lua money AMOUNT           development money (through the books; negative takes it away)

Lua is a development convenience here; the game side is C++ (Source/OpenKoikatsu/OKStreetSocialControl.inl).
]]

local BASE = "http://127.0.0.1:" .. (os.getenv("OK_CONTROL_PORT") or tostring(require("ok_instance").ports().street_control))

-- GET when body is nil, otherwise POST the JSON body; the reply is JSON.
local function call(path, body)
  local args = { "curl.exe", "-sS", "--fail", "--max-time", "30", "-H", "Content-Type: application/json; charset=utf-8" }
  local input
  if body ~= nil then
    input = json.encode(body, { ensure_ascii = false })
    for _, a in ipairs({ "-X", "POST", "--data-binary", "@-" }) do args[#args + 1] = a end
  end
  args[#args + 1] = BASE .. path
  local res = proc.run(args, { capture = true, text = false, input = input })
  if not res.ok then error(("control channel %s failed (%d): %s"):format(path, res.code, util.strip(res.stderr or "")), 0) end
  return json.decode(res.stdout)
end

local S = util.str
local function D(x) return math.tointeger(x) or math.tointeger(x >= 0 and math.floor(x) or math.ceil(x)) end
local function F(fmt, x) return string.format(fmt, x) end
local function orv(v, d) if T(v) then return v end return d end
local function sorted_by_dist(girls) return util.sorted(girls, function(g) return g.dist end) end
-- Python print(x): str() of the value (dict/list as repr).
local function pprint(...)
  local parts = {}
  for i = 1, select("#", ...) do parts[i] = S((select(i, ...))) end
  print(table.concat(parts, " "))
end

local function summary(s)
  local out = {}
  local t, p = s.time, s.player
  out[#out + 1] = ("time %s%s  player (%s,%s) yaw %s place=%s cursor=%s cash=%s"):format(
    S(t.clock), T(t.paused) and " [paused]" or "", F("%.0f", p.x), F("%.0f", p.y), F("%.0f", p.yaw), S(orv(p.place, "-")), S(p.cursor_mode), S(p.cash))
  out[#out + 1] = "message: " .. S(s.message)
  if T(s.notice) then out[#out + 1] = "notice: " .. S(s.notice) end
  local a = s.activity
  out[#out + 1] = ("party=%s bluff=%s"):format(S(s.party), S(s.bluff))
  out[#out + 1] = ("target=%s partner=%s pair=%s companion=%s outing=%s(%s@%s step %s/%s) nav=%s/%s/%s facility=%s"):format(
    S(s.target), S(s.partner), S(s.pair.partner), S(a.companion), S(a.outing_girl), S(a.outing_activity), S(a.outing_place),
    S(a.outing_step), S(a.outing_points), S(a.nav_girl), S(a.nav_point), S(a.nav_name), S(a.facility))
  local f = s.free_talk
  out[#out + 1] = ("free_talk open=%s live=%s ready=%s busy=%s mic=%s speaking=%s queued=%s farewell=%s"):format(
    S(f.open), S(f.live), S(f.live_ready), S(f.busy), F("%.4f", f.mic_level), S(f.mic_speaking), S(f.queued_events), S(f.farewell_pending))
  if T(f.caption_player) or T(f.caption_her) then
    out[#out + 1] = "  him: " .. S(f.caption_player)
    out[#out + 1] = "  her: " .. S(f.caption_her)
  end
  if T(f.heard) then out[#out + 1] = "  heard: " .. S(f.heard) end
  if T(s.cart) then
    out[#out + 1] = "cart: " .. table.concat(util.map(s.cart, function(c)
      local name = c.name; if name == nil then name = c.item end
      return (T(c.for_her) and "她:" or "他:") .. S(name)
    end), ", ")
  end
  if T(s.table) then
    out[#out + 1] = "table: " .. table.concat(util.map(s.table, function(d)
      return ("%s%s %s%d%%"):format(T(d.for_her) and "她:" or "他:", S(d.name), T(d.served) and "" or "(備餐中)", D(d.percent))
    end), ", ")
  end
  if T(s.shop) then out[#out + 1] = ("shop=%s closed=%s pending_serves=%s"):format(S(s.shop), S(s.shop_closed), S(s.pending_serves)) end
  local girls = sorted_by_dist(s.girls)
  for i = 1, math.min(3, #girls) do
    local g = girls[i]
    local flags = {}
    for _, k in ipairs({ "target", "companion", "outing", "pair", "life_pose", "commute", "scheduled", "held", "promised_treat" }) do
      if T(g[k]) then flags[#flags + 1] = k end
    end
    out[#out + 1] = ("  #%d %s dist=%s place=%s clip=%s%s speaking=%s favor=%s cash=%s owes=%s satiety=%s flags=%s"):format(
      D(g.slot), S(g.name), F("%.0f", g.dist), S(orv(g.place, "-")), S(orv(g.clip, "-")), T(g.playing) and "(playing)" or "", S(g.speaking),
      F("%.0f", g.favor), S(g.cash), S(g.owes_player), F("%.0f", g.satiety), table.concat(flags, ","))
  end
  return table.concat(out, "\n")
end

local function num(s)
  local n = tonumber(s)
  if n == nil then error("could not convert string to float: " .. util.repr(s), 0) end
  return n + 0.0
end
local function int(s)
  local n = math.tointeger(tonumber(s))
  if n == nil then error("invalid literal for int() with base 10: " .. util.repr(s), 0) end
  return n
end
local function join(list, from) return table.concat(util.slice(list, from), " ") end

local function main(argv)
  if #argv < 1 then
    print(DOC)
    return 1
  end
  local cmd = argv[1]
  local rest = util.slice(argv, 2)
  local sleep = ok.time.sleep
  if cmd == "state" then
    local s = call("/state")
    print(util.contains_value(rest, "--raw") and json.encode(s, { ensure_ascii = false, indent = 1 }) or summary(s))
  elseif cmd == "say" then
    pprint(call("/say", json.object({ { "text", join(rest, 1) } })))
  elseif cmd == "chat" then
    -- say something, then wait until she has answered and finished speaking; print what she said
    local r = call("/say", json.object({ { "text", join(rest, 1) } }))
    if not T(r.ok) then
      pprint(r)
      return 1
    end
    local t0 = ok.time.time()
    sleep(1.0)
    while ok.time.time() - t0 < 90 do
      local s = call("/state")
      local f = s.free_talk
      local speaking = util.any(s.girls, function(g) return T(g.speaking) end)
      if not T(f.busy) and not speaking and f.queued_events == 0 then break end
      sleep(1.0)
    end
    local s = call("/state")
    print(("[%s] her: %s"):format(F("%.0fs", ok.time.time() - t0), S(s.free_talk.caption_her)))
    print(summary(s))
  elseif cmd == "watch" then
    -- one compact line every few seconds: time, player, activity, nearest girls
    local total = rest[1] and num(rest[1]) or 30
    local every = rest[2] and num(rest[2]) or 3
    local t0 = ok.time.time()
    while ok.time.time() - t0 < total do
      local s = call("/state")
      local a = s.activity
      local near = sorted_by_dist(s.girls)[1]
      local flags = {}
      for _, k in ipairs({ "target", "companion", "outing", "pair", "life_pose", "commute", "held" }) do if T(near[k]) then flags[#flags + 1] = k end end
      print(("%s p=(%s,%s)%s tgt=%s pair=%s comp=%s out=%s(%s %s/%s) nav=%s | #%d d=%s yaw %s/%s clip=%s%s fl=%s | cart=%d table=%d | %s"):format(
        S(s.time.clock), F("%.0f", s.player.x), F("%.0f", s.player.y), T(s.player.place) and ("@" .. S(s.player.place)) or "",
        S(s.target), S(s.pair.partner), S(a.companion), S(a.outing_girl), S(a.outing_activity), S(a.outing_step), S(a.outing_points), S(a.nav_girl),
        D(near.slot), F("%.0f", near.dist), F("%.0f", near.yaw_body), F("%.0f", near.yaw_target), S(orv(near.clip, "-")), T(near.playing) and "*" or "",
        table.concat(flags, ","), #s.cart, #s.table, util.usub(S(orv(s.message, "")), 1, 40)))
      sleep(every)
    end
  elseif cmd == "clock" then
    -- clock pause | run [SECONDS]: the game clock; run with SECONDS lets it go that long (real seconds), then pauses
    local run = rest[1] == "run"
    pprint(call("/clock", json.object({ { "paused", not run } })))
    if run and rest[2] then
      sleep(num(rest[2]))
      call("/clock", json.object({ { "paused", true } }))
      print(summary(call("/state")))
    end
  elseif cmd == "talk" then
    -- talk TEXT: say it with the clock running, wait until she has answered and stopped speaking, pause the clock;
    -- prints each line she said meanwhile and the notice
    local r = call("/say", json.object({ { "text", join(rest, 1) } }))
    if not T(r.ok) then
      pprint(r)
      return 1
    end
    call("/clock", json.object({ { "paused", false } }))
    local t0, seen, last = ok.time.time(), {}, nil
    sleep(1.0)
    while ok.time.time() - t0 < 120 do
      local s = call("/state")
      local f = s.free_talk
      local her = S(orv(f.caption_her, ""))
      if her ~= "" and her ~= last then seen[#seen + 1] = her; last = her end
      local speaking = util.any(s.girls, function(g) return T(g.speaking) end)
      if ok.time.time() - t0 > 3 and not T(f.busy) and not speaking and f.queued_events == 0 then break end
      sleep(0.7)
    end
    call("/clock", json.object({ { "paused", true } }))
    local s = call("/state")
    for _, line in ipairs(seen) do print("  her: " .. line) end
    print(("  %s | %s | %s"):format(S(s.time.clock), S(orv(s.message, "")), S(orv(s.notice, ""))))
  elseif cmd == "goto" or cmd == "outing" or cmd == "until" then
    -- goto girl N | place ID [MAX]: walk there with the clock running; stops when the walk ends or she talks to him
    -- outing [MAX]: let the clock run until the walk with her ends
    -- until HH:MM [MAX]: let the clock run until that time
    -- each pauses the clock at the end (MAX real seconds, 180 by default)
    local max = 180
    if cmd == "goto" then
      call("/navigate", json.object({ { rest[1], rest[1] == "girl" and int(rest[2]) or rest[2] } }))
      max = rest[3] and num(rest[3]) or max
    elseif cmd == "outing" then
      max = rest[1] and num(rest[1]) or max
    else
      max = rest[2] and num(rest[2]) or max
    end
    call("/clock", json.object({ { "paused", false } }))
    local t0 = ok.time.time()
    sleep(2.0)
    while ok.time.time() - t0 < max do
      local s = call("/state")
      local a = s.activity
      if cmd == "goto" and ((a.nav_girl == -1 and not T(a.nav_point)) or s.target ~= -1) then break end
      if cmd == "outing" and a.outing_girl == -1 then break end
      if cmd == "until" and S(s.time.clock) >= rest[1] then break end
      sleep(1.0)
    end
    call("/clock", json.object({ { "paused", true } }))
    print(summary(call("/state")))
  elseif cmd == "phone" then
    pprint(call("/phone", json.object({ { "text", join(rest, 1) } })))
  elseif cmd == "mic" then
    pprint(call("/mic", json.object({ { "ignore", rest[1] == "off" } })))
  elseif cmd == "bluff" then
    -- bluff [start|play C..|pick C..|call|stop]: the card table; cards by number (0..51, shown as n:card).
    local cards = util.map(util.slice(rest, 2), int)
    local b = call("/bluff", json.object({ { "do", rest[1] or "" }, { "cards", cards } }))
    print(("active=%s intro=%s window=%s turn=%s rank=%s pile=%s last=%s notice=%s"):format(S(b.active), S(b.intro), S(b.window), S(b.turn), S(b.rank), S(b.pile), S(b.last_seat), S(b.notice)))
    local pairs_ = {}
    for i = 1, math.min(#b.seats, #b.counts) do pairs_[i] = "(" .. util.repr(b.seats[i]) .. ", " .. util.repr(b.counts[i]) .. ")" end
    print("seats [" .. table.concat(pairs_, ", ") .. "]")
    print("hand " .. table.concat(util.map(b.hand, S), " "))
    for _, line in ipairs(b.log) do print("  " .. S(line)) end
  elseif cmd == "summon" then
    -- summon N [N ...]: put those heroines (slots) next to him (development)
    pprint(call("/summon", json.object({ { "slots", util.map(rest, int) } })))
  elseif cmd == "locale" then
    -- locale [zh-TW|zh-CN|en] [TW|CN|US]: the language, and (development) the save's region
    local body = json.object()
    for _, x in ipairs(rest) do
      body[(x == "TW" or x == "CN" or x == "US") and "region" or "lang"] = x
    end
    pprint(call("/locale", body))
  elseif cmd == "saves" then
    -- saves [open|start|close|save N|load N|new]: the save slots and the start menu
    local body = json.object({ { "op", rest[1] or "" } })
    if #rest > 1 then body.slot = rest[2] end
    local r = call("/saves", body)
    print(("menu=%s notice=%s"):format(S(r.menu), S(r.notice)))
    local slots = r.slots
    if slots == nil then slots = json.object() end
    for k, v in pairs(slots) do print(("  %s: %s"):format(S(k), S(orv(v, "-")))) end
  elseif cmd == "money" then
    pprint(call("/money", json.object({ { "amount", num(rest[1]) } })))
  elseif cmd == "cursor" then
    pprint(call("/cursor", json.object({ { "on", rest[1] == "on" } })))
  elseif cmd == "menu" then
    local m = call("/menu")
    print(("place=%s closed=%s"):format(S(m.place), S(m.closed)))
    for _, i in ipairs(m.items) do
      print(("  %s %s %s %s"):format(S(i.id), S(i.name), S(i.price), T(i.consumable) and "food/drink" or "goods"))
    end
  elseif cmd == "buttons" then
    for _, b in ipairs(call("/buttons").buttons) do pprint(b) end
  elseif cmd == "click" then
    -- click LABEL [--nth N]: N = which of several buttons with the same label (0 = first)
    local nth = 0
    local i = util.index_of(rest, "--nth")
    if i then
      nth = int(rest[i + 1])
      local kept = {}
      for k, v in ipairs(rest) do if k ~= i and k ~= i + 1 then kept[#kept + 1] = v end end
      rest = kept
    end
    local player = call("/state").player
    if player == nil then player = {} end
    local was = player.cursor
    if not T(was) then -- the game only takes mouse events once cursor mode has been on for a frame
      call("/cursor", json.object({ { "on", true } }))
      sleep(0.4)
    end
    local r = call("/click", json.object({ { "label", join(rest, 1) }, { "index", nth } }))
    local err = r.error; if err == nil then err = "" end
    if not T(r.ok) and S(err):find("scrolled", 1, true) then -- it was below the fold: the panel scrolled, press again
      sleep(0.8)
      r = call("/click", json.object({ { "label", join(rest, 1) }, { "index", nth } }))
    end
    if not T(was) then
      sleep(0.3)
      call("/cursor", json.object({ { "on", false } }))
    end
    pprint(r)
  elseif cmd == "key" or cmd == "walk" then
    pprint(call("/key", json.object({ { "key", rest[1] }, { "seconds", #rest > 1 and num(rest[2]) or 0.0 } })))
    if cmd == "walk" and #rest > 1 then sleep(num(rest[2])) end
  elseif cmd == "look" then
    local body = json.object({ { "yaw", num(rest[1]) } })
    if #rest > 1 then body.pitch = num(rest[2]) end
    pprint(call("/look", body))
  elseif cmd == "nav" then
    if rest[1] == "girl" then
      pprint(call("/navigate", json.object({ { "girl", int(rest[2]) } })))
    elseif rest[1] == "place" then
      -- nav place ID [FLOOR]: the place's door, or that floor's elevator landing (0 = ground)
      local body = json.object({ { "place", rest[2] } })
      if #rest > 2 then body.floor = int(rest[3]) end
      pprint(call("/navigate", body))
    else
      pprint(call("/navigate", json.object({ { "stop", true } })))
    end
  elseif cmd == "shot" then
    local r = call("/screenshot", json.object())
    sleep(1.5)
    pprint(r.path)
  elseif cmd == "rules" then
    if rest[1] == "reload" then
      print(json.encode(call("/rules", json.object({ { "action", "reload" } })), { ensure_ascii = false }))
    elseif rest[1] == "eval" then
      local request = json.decode(join(rest, 2))
      print(json.encode(call("/rules", json.object({ { "action", "eval" }, { "request", request } })), { ensure_ascii = false, indent = 1 }))
    else
      print(json.encode(call("/rules", json.object({ { "action", "status" } })), { ensure_ascii = false, indent = 1 }))
    end
  elseif cmd == "mind" then
    -- mind [reload]: the heroine mind's status; reload = a new ok-mind build, or deepseek.json read again (idle only)
    print(json.encode(call("/mind", json.object({ { "action", rest[1] or "status" } })), { ensure_ascii = false, indent = 1 }))
  elseif cmd == "wait" then
    sleep(num(rest[1]))
  else
    print(DOC)
    return 1
  end
  return 0
end

ok.main(function() return main(arg) end)
