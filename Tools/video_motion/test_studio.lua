-- Studio scene end to end (Docs/STUDIO_SCENE.md) through CharacterMCP: enter the studio, apply a
-- pair video motion, key both characters, check interpolation / playback clocks, save and reload
-- a scene, drag the guide handles, change clothes, take UI screenshots and leave.
--
--   Tools\lua\lua.exe Tools\video_motion\test_studio.lua [--item 【雙人】浴室雙人] [--card <png>] [--game <KKS root>]
local ok = require "ok"
local G = dofile(ok.root .. "/Tools/video_motion/game_mcp.lua")
local near, list = G.near, G.list

local args = ok.args.parser("Studio scene end to end")
  :opt("--item", { default = "【雙人】浴室雙人" }):opt("--card"):opt("--game"):opt("--editor")
  :opt("--timeout", { default = "420" }):opt("--ready-timeout", { default = "300" })
  :parse(arg)
local timeout = tonumber(args.timeout)
local game = G.launch({ out = ok.root .. "/Saved/Tests/Studio/" .. ok.time.stamp(), card = args.card, game = args.game, editor = args.editor })

local function studio(op, fields, raw)
  fields = fields or {}
  fields.op = op
  local r = game:call("studio", fields, 60)
  if raw then return r end
  if not r.ok then error("studio " .. op .. ": " .. tostring(r.error), 0) end
  return r.result.studio or {}
end
local function objects() return (game:state().studio or {}).objects or { {}, {} } end
local function shot(tag)
  local r = game:call("screenshot", { target = "viewport", ui = true }, 120)
  print(("   screenshot %s: %s"):format(tag, tostring(r.result and r.result.image or r.error)))
end

local good, err = xpcall(function()
  game:wait_ready(tonumber(args["ready-timeout"] or args.ready_timeout or 300))
  local r = game:call("studio", { op = "enter" }, 60)
  local s = r.result and r.result.studio
  game:check("enter studio", r.ok and s ~= nil, r.error or "")
  if not s then return end
  game:check("female starts at origin", near(s.objects[1].pos, { 0, 0, 0 }, 1e-3), list(s.objects[1].pos))

  -- pair video: partner auto-loads, both get clips
  studio("video", { item = args.item })
  local since, objs = ok.time.clock(), nil
  repeat
    s = game:state().studio or {}
    objs = s.objects or { {}, {} }
    if objs[2].loaded and objs[2].pose_mode == 2 and objs[2].clip and objs[2].clip ~= "" then break end
    ok.time.sleep(1)
  until ok.time.clock() - since > timeout
  game:check("pair video loads partner", objs[2].loaded and tostring(objs[2].clip):sub(1, 7) == "video:|",
    ("%.1fs clip=%s female=%s"):format(ok.time.clock() - since, tostring(objs[2].clip), tostring(objs[1].clip)))
  game:check("timeline length follows clip", math.abs((s.length or 0) - (objs[1].anim_duration or -1)) < 0.05, "length=" .. tostring(s.length))

  -- keys on the female: 0 s at origin, 2 s moved + turned (auto key)
  studio("select", { index = 0 })
  studio("time", { time = 0 })
  studio("transform", { pos = { 0, 0, 0 }, rot = { 0, 0, 0 }, scale = { 1, 1, 1 } })
  studio("time", { time = 2 })
  studio("transform", { pos = { 1, 0, 0 }, rot = { 0, 90, 0 }, scale = { 1.2, 1.2, 1.2 } })
  studio("time", { time = 1 })
  ok.time.sleep(0.3)
  local f = objects()[1]
  game:check("female has 2 keys", f.keys == 2, tostring(f.keys))
  game:check("linear midpoint position", near(f.pos, { 0.5, 0, 0 }, 0.01), list(f.pos))
  game:check("slerp midpoint rotation", math.abs(f.rot[2] - 45) < 0.5, list(f.rot))
  game:check("scale midpoint", near(f.scale, { 1.1, 1.1, 1.1 }, 0.01), list(f.scale))
  studio("time", { time = 0 })
  studio("interp", { interp = 2 }) -- constant: stays at key 0 until 2 s
  studio("time", { time = 1.5 })
  ok.time.sleep(0.3)
  f = objects()[1]
  game:check("constant interpolation holds", near(f.pos, { 0, 0, 0 }, 0.01), list(f.pos))

  -- male: one key offset to the side
  studio("select", { index = 1 })
  studio("time", { time = 0 })
  studio("transform", { pos = { 0, 0, 0.6 }, rot = { 0, 180, 0 } })
  local m = objects()[2]
  game:check("male keyed", m.keys == 1 and near(m.pos, { 0, 0, 0.6 }, 0.01), list(m.pos))
  shot("keyed")

  -- playback: both clocks follow the timeline
  studio("play", { play = true })
  ok.time.sleep(1.5)
  s = game:state().studio
  f, m = s.objects[1], s.objects[2]
  game:check("playing advances time", s.playing and s.time > 0.8, ("t=%.2f"):format(s.time))
  game:check("female clock = timeline", math.abs(f.anim_phase - s.time * f.speed) < 0.1, ("phase=%.2f t=%.2f"):format(f.anim_phase, s.time))
  game:check("male clock = timeline", math.abs(m.anim_phase - s.time * m.speed) < 0.1, ("phase=%.2f"):format(m.anim_phase))
  studio("play", { play = false })
  shot("playing")

  -- scene round trip
  studio("save", { name = "test_studio" })
  studio("select", { index = 0 }); studio("clear_keys")
  studio("select", { index = 1 }); studio("clear_keys")
  s = studio("load", { name = "test_studio" })
  game:check("scene reload restores keys", s.objects[1].keys == 2 and s.objects[2].keys == 1,
    ("%s/%s %s"):format(tostring(s.objects[1].keys), tostring(s.objects[2].keys), tostring(s.status)))

  -- guide object: same math as a mouse drag on the handles
  studio("select", { index = 0 })
  studio("time", { time = 4 })
  ok.time.sleep(0.3)
  local before = objects()[1]
  local d = studio("guide_drag", { mode = 0, axis = 0, dx = 0, dy = 0 }).screen_direction
  r = studio("guide_drag", { mode = 0, axis = 0, dx = d[1] * 100, dy = d[2] * 100 }, true)
  local after = ((r.result or {}).studio or {}).objects or { {} }
  local dx = (after[1].pos or { 0 })[1] - before.pos[1]
  game:check("move handle drags along X", r.ok and dx > 0.05 and math.abs(after[1].pos[2] - before.pos[2]) < 1e-3, ("dx=%.3fm %s"):format(dx, r.error or ""))
  d = studio("guide_drag", { mode = 1, axis = 1, dx = 0, dy = 0 }).screen_direction
  before = objects()[1]
  after = studio("guide_drag", { mode = 1, axis = 1, dx = d[1] * 100, dy = d[2] * 100 }).objects[1]
  local turn = (after.rot[2] - before.rot[2] + 540) % 360 - 180
  game:check("rotate ring turns about up", math.abs(math.abs(turn) - 100 / 85 * 180 / math.pi) < 2, ("turn=%.1fdeg"):format(turn))
  after = studio("guide_drag", { mode = 2, axis = 3, dx = 100, dy = 0 }).objects[1]
  game:check("uniform scale handle", math.abs(after.scale[1] / before.scale[1] - math.exp(1)) < 0.02, list(after.scale))
  studio("guide_drag", { mode = 2, axis = 3, dx = -100, dy = 0 }) -- back to the previous size
  game:call("camera", { revision = game:state().revision, pitch = 25, yaw = 30, distance = 420 }, 60)
  for mode = 0, 2 do
    studio("mode", { mode = mode })
    ok.time.sleep(0.5)
    shot("guide mode " .. mode)
  end

  -- clothes inside the studio keep the partner
  r = game:call("action", { command = "clothing", clothing_kind = 101, revision = game:state().revision }, 120)
  ok.time.sleep(2)
  game:check("clothes change keeps studio and partner", r.ok and objects()[2].loaded, r.error or "")
  shot("clothes")

  studio("exit")
  game:check("exit studio", game:state().studio == nil)
end, debug.traceback)
os.exit(game:finish(not good and err or nil))
