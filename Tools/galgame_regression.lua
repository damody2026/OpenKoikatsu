-- Street galgame regression over the control channel (run after packaging a stage).
--
--   Tools\lua\lua.exe Tools\galgame_regression.lua [--launch] [--girl 3] [--port N] [--keep]
--
-- With --launch the newest stage starts with -Control (Tools/run_street_social_development.lua); otherwise a running
-- game on this checkout's control port is used. Heroine --girl is called over and talked to; the run checks:
--   scene      the galgame presenter takes the conversation (/galgame active, partner = the girl)
--   pov        her reply is framed in first person (presenter shot "pov")
--   portrait   the speaker's portrait is shown and captured
--   text       the line shows whole at once (typed = total)
--   skip       Space skips her voiced line (log "skipped the line")
--   rollback   the wheel scrolls back over two lines; speaking again rolls the conversation back to there (log
--              "rollback: back to line", her log and history shortened)
--   memory     the rollback takes her mind back too (the mind part of that log line is not "skipped"), and she is
--              asked about the fact told after the point rolled back to (her answer is printed; a mention is a warning)
-- Prints PASS / FAIL / WARN per check; exits 1 when any check fails. --keep leaves a launched game running.
local ok = require "ok"
local json = ok.json

local a = ok.args.parser("Street galgame regression")
  :opt("--girl", { type = "int", default = 3 })
  :opt("--port", { type = "int", default = 0 })
  :flag("--launch")
  :flag("--keep")
  :parse(arg)
local root = ok.root
local port = a.port
if port == 0 then port = math.tointeger(tonumber(ok.util.strip(ok.proc.output({ ok.lua_exe, root .. "/Tools/ok_instance.lua", "port", "street_control" })))) end
local base = "http://127.0.0.1:" .. port

local function call(p, body)
  local r = ok.kit.http_request(base .. p, { method = body and "POST" or "GET", data = body and json.encode(body) or nil,
    headers = body and { ["Content-Type"] = "application/json; charset=utf-8" } or nil, timeout = 60, compressed = false })
  if r.status ~= 200 then return nil, r.error or ("HTTP " .. tostring(r.status)) end
  return json.decode(r.body)
end
local results, failed = {}, false
local function report(status, name, detail)
  results[#results + 1] = ("%-4s %-9s %s"):format(status, name, detail or "")
  print(results[#results])
  if status == "FAIL" then failed = true end
end
local function wait(cond, seconds, step)
  local t = ok.time.time() + seconds
  repeat local v = cond(); if v then return v end; ok.time.sleep(step or 0.5) until ok.time.time() > t
  return nil
end

-- The game and its log.
local log
if a.launch then
  local out = ok.proc.output({ ok.lua_exe, root .. "/Tools/run_street_social_development.lua", "-Control", "-Window", "1600x900" })
  log = out:match("(%a:[/\\][^\r\n ]*Play%-%d+T%d+)")
  if log then log = log .. "/game.log" end
end
if not wait(function() local s = call("/state"); return s and s.all_ready end, 900, 5) then
  report("FAIL", "start", "the game on port " .. port .. " never got every character ready"); os.exit(1)
end
if not log then
  -- The running game's own log: the newest play folder of this checkout.
  local newest, when = nil, 0
  for _, d in ipairs(ok.path.glob(root .. "/Saved/StreetSocial", "Play-*")) do local m = ok.path.mtime(d .. "/game.log") or 0; if m > when then newest, when = d, m end end
  log = newest and newest .. "/game.log"
end
local function log_count(pattern) local n = 0; for line in io.lines(log) do if line:find(pattern, 1, true) then n = n + 1 end end; return n end
local function log_last(pattern) local last; for line in io.lines(log) do if line:find(pattern, 1, true) then last = line end end; return last end

-- A time she is about (in the evening she leaves for dinner and ends the talk), and a voice server that answers (the
-- skip check needs a voiced line).
local tts = os.getenv("OK_TTS_ENDPOINT") or ("http://127.0.0.1:" .. ok.util.strip(ok.proc.output({ ok.lua_exe, root .. "/Tools/ok_instance.lua", "port", "tts" })))
if ok.kit.http_request(tts .. "/health", { timeout = 5 }).status ~= 200 then report("WARN", "voice", "no TTS server at " .. tts .. ": the skip check will fail") end
local g = a.girl
call("/clock", { paused = false, hours = 10 }); ok.time.sleep(3)
-- Call her over and open the conversation (again whenever she has ended it: she remembers earlier runs, and a player
-- asking the same things again may well be told goodbye).
local function talking()
  local gg = call("/galgame") or {}; local s = call("/state") or {}
  return s.target == g and gg.active
end
local function open_talk()
  call("/summon", { slots = { g } }); ok.time.sleep(3)
  for _ = 1, 60 do
    local s = call("/state"); local her
    for _, x in ipairs(s.girls or {}) do if x.slot == g then her = x end end
    if not her then break end
    local dx, dy = her.x - s.player.x, her.y - s.player.y
    local d = math.sqrt(dx * dx + dy * dy)
    if d < 120 then break end
    call("/look", { yaw = math.deg(math.atan(dy, dx)), pitch = -12 })
    local t = math.min(1.5, math.max(0.25, (d - 120) / 400)); call("/key", { key = "W", seconds = t }); ok.time.sleep(t + 0.1)
  end
  for _ = 1, 3 do
    call("/key", { key = "E", seconds = 0.15 }); ok.time.sleep(4)
    local s = call("/state"); if s and s.target == g then break end
  end
end
open_talk()

-- Questions differ from run to run (the same ones every run bore her).
math.randomseed(os.time())
local function pick(list) return list[math.random(#list)] end
local openers = { "妳好，今天過得怎麼樣？", "嗨，最近有什麼新鮮事嗎？", "今天天氣不錯，妳在忙什麼呢？", "好久不見，最近還好嗎？" }
local longers = { "可以多說一點嗎？我很想聽妳講。", "那後來呢？再跟我多說一些吧。", "聽起來很有趣，可以說得詳細一點嗎？", "我想多了解妳一點，再多聊一些好嗎？" }
local colors = { "綠色", "藍色", "紫色", "橘色", "白色" }
local days = { { "七月七日", "七月七" }, { "三月十二日", "三月十二" }, { "十月三十日", "十月三十" }, { "五月二十日", "五月二十" } }

-- One exchange: say something (in an open conversation), wait for her next line (the log counts her lines).
local herLine = "Street galgame line c" .. g
local function say(text)
  if not talking() then open_talk() end
  local before = log_count(herLine)
  call("/clock", { paused = false }); call("/say", { text = text })
  local got = wait(function() return log_count(herLine) > before end, 90, 1)
  ok.time.sleep(1.5)
  return got and log_last(herLine)
end

local first = say(pick(openers))
local gg = call("/galgame") or {}
if gg.active and gg.partner == g then report("PASS", "scene", "presenter active with heroine " .. g)
else report("FAIL", "scene", "presenter active=" .. tostring(gg.active) .. " partner=" .. tostring(gg.partner) .. (first and "" or " (no reply logged)")) end
local story = gg.story or {}
if story.shot == "pov" then report("PASS", "pov", "first person")
else report("FAIL", "pov", "shot=" .. tostring(story.shot) .. " (a blocked first person gives way to the usual shot; try another spot)") end
if story.portrait and gg.portrait_captured then report("PASS", "portrait", "shown and captured")
else report("FAIL", "portrait", "shown=" .. tostring(story.portrait) .. " captured=" .. tostring(gg.portrait_captured)) end
if (story.total or 0) > 0 and story.shown == story.total then report("PASS", "text", ("whole line (%d/%d)"):format(story.shown, story.total))
else report("FAIL", "text", ("typed %s of %s"):format(tostring(story.shown), tostring(story.total))) end

-- Skip: ask again and press Space as soon as her voiced line starts.
do
  if not talking() then open_talk() end
  local before, skips = log_count(herLine), log_count("Street galgame: skipped the line of")
  call("/say", { text = pick(longers) })
  wait(function() return log_count(herLine) > before end, 90, 0.5)
  ok.time.sleep(0.8)
  call("/key", { key = "SpaceBar", seconds = 0.05 })
  if wait(function() return log_count("Street galgame: skipped the line of") > skips end, 5, 0.25) then report("PASS", "skip", "Space skipped her line")
  else report("FAIL", "skip", "no skip logged (was her line voiced?)") end
  ok.time.sleep(2)
end

-- Rollback and memory: two facts in one conversation, back over both lines' worth, then ask about the later fact.
local day = pick(days)
local before
for attempt = 1, 2 do
  if not talking() then open_talk() end
  say("我最喜歡的顏色是" .. pick(colors) .. "，記住喔。")
  say("跟妳說，我的生日是" .. day[1] .. "。")
  before = call("/galgame") or {}
  if talking() then break end
end
call("/key", { key = "MouseScrollUp", seconds = 0.05 }); ok.time.sleep(0.6)
call("/key", { key = "MouseScrollUp", seconds = 0.05 }); ok.time.sleep(0.6)
local scrolled = call("/galgame") or {}
if (scrolled.view_line or -1) >= 0 and (scrolled.view_line or 0) < (before.lines or 0) then report("PASS", "wheel", ("scrolled to line %d of %d"):format(scrolled.view_line, before.lines))
else report("FAIL", "wheel", "view_line=" .. tostring(scrolled.view_line) .. " lines=" .. tostring(before.lines) .. (talking() and "" or " (she had ended the conversation)")) end
local rollbacks = log_count("Street galgame rollback: back to line")
local answer = say("剛剛那句不算。我的生日是幾號，我有跟妳說過嗎？")
local rb = rollbacks < log_count("Street galgame rollback: back to line") and log_last("Street galgame rollback: back to line")
if rb then
  report("PASS", "rollback", rb:match("rollback: (.*)$"))
  local mind = rb:match("mind (.*)$") or ""
  if mind ~= "" and not mind:find("skip") and not mind:find("fail") then report("PASS", "memory", "mind " .. mind)
  else report("FAIL", "memory", "mind part: " .. mind) end
else report("FAIL", "rollback", "no rollback logged") end
if answer then
  local words = answer:match("%(voiced%): (.*)$") or answer:match(": (.*)$") or answer
  if words:find(day[2], 1, true) then report("WARN", "forgot", "she still mentions the birthday: " .. words)
  else report("PASS", "forgot", words) end
end

print(("%d checks, %s"):format(#results, failed and "FAILED" or "all passed"))
if a.launch and not a.keep then
  local pidfile = (log:gsub("game%.log$", "game.pid"))
  local pid = ok.path.exists(pidfile) and ok.util.strip(ok.path.read_text(pidfile)) or ""
  if pid ~= "" then ok.proc.run({ "taskkill", "/PID", pid, "/F" }, { capture = true }) end
end
os.exit(failed and 1 or 0)
