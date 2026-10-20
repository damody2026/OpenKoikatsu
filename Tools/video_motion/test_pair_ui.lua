-- Video pair playback in the real game UI (CharacterMCP): select a 【雙人】 item like a click,
-- wait for the male partner to auto-load, check both clocks advance together, then change
-- clothes (reloads the female) and check the partner stays in step. Saves three-view captures.
--
--   Tools\lua\lua.exe Tools\video_motion\test_pair_ui.lua [--item 【雙人】浴室雙人] [--card <png>] [--game <KKS root>] [--timeout 600]
local ok = require "ok"
local json = ok.json
local G = dofile(ok.root .. "/Tools/video_motion/game_mcp.lua")

local args = ok.args.parser("Video pair playback in the game UI")
  :opt("--item", { default = "【雙人】浴室雙人" }):opt("--card"):opt("--game"):opt("--editor")
  :opt("--timeout", { default = "600" }):opt("--ready-timeout", { default = "300" })
  :parse(arg)
local timeout = tonumber(args.timeout)
local game = G.launch({ out = ok.root .. "/Saved/Tests/VideoPairUI/" .. ok.time.stamp(), card = args.card, game = args.game, editor = args.editor })

-- Partner loaded and both clocks advance together (shared origin and clock).
local function both_playing(label, since)
  while true do
    local st = game:state()
    if st.busy == false and st.partner_loaded and st.partner_pose_mode == 2 and st.partner_ticking then
      local p1, f1 = st.partner_anim_phase or 0, st.anim_phase or 0
      ok.time.sleep(1)
      local st2 = game:state()
      local p2, f2 = st2.partner_anim_phase or 0, st2.anim_phase or 0
      game:check(label, p2 > p1 and f2 > f1 and math.abs(p2 - f2) < 0.1,
        ("female %.2f->%.2f male %.2f->%.2f after %.1fs | %s"):format(f1, f2, p1, p2, ok.time.clock() - since, tostring(st2.pair_status)))
      return st2
    end
    if ok.time.clock() - since > timeout then
      game:check(label, false, "partner not playing: " .. json.encode(st):sub(1, 400))
      return st
    end
    ok.time.sleep(1)
  end
end

local function capture(st, tag)
  local good, r = pcall(function() return game:call("views", { revision = st.revision }, 120) end)
  local images = {}
  if good and r.result and r.result.images then for _, i in ipairs(r.result.images) do images[#images + 1] = i.image end end
  print(("   capture %s: %s"):format(tag, #images > 0 and table.concat(images, ", ") or tostring(good and r.error or r)))
end

local good, err = xpcall(function()
  local st = game:wait_ready(tonumber(args["ready-timeout"] or args.ready_timeout or 300))
  print("   female loaded: " .. tostring(st.card))
  local r = game:call("video_motion", { item = args.item })
  game:check("select " .. args.item, r.ok, r.error or (r.result and r.result.video_status) or "")
  if not r.ok then return end
  st = both_playing("pair plays", ok.time.clock())
  capture(st, "pair")
  r = game:call("action", { command = "clothing", clothing_kind = 101, revision = st.revision }, 120)
  game:check("clothing half-off", r.ok, r.error or "")
  st = both_playing("partner stays after clothes", ok.time.clock())
  capture(st, "clothes")
end, debug.traceback)
os.exit(game:finish(not good and err or nil))
