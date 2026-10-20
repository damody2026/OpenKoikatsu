-- In-game video import without Python (Docs/VIDEO_MOTION_NATIVE_PLAN.md P11).
-- Presses 開始轉換 in the video motion panel (which runs Tools/ok-video/target/release/ok-video.exe),
-- waits for the result, then plays it.
--
--   Tools\lua\lua.exe Tools\video_motion\test_import.lua --video <mp4> [--name 名稱] [--static] [--pair] [--female-left]
--     [--game <KKS root>] [--card <png>] [--ready-timeout 300] [--timeout 1200]
local ok = require "ok"
local json, path = ok.json, ok.path
local G = dofile(ok.root .. "/Tools/video_motion/game_mcp.lua")

local args = ok.args.parser("In-game video import without Python")
  :opt("--video", { required = true }):opt("--name"):flag("--static"):flag("--pair"):flag("--female-left")
  :opt("--card"):opt("--game"):opt("--editor"):opt("--timeout", { default = "1200" }):opt("--ready-timeout", { default = "300" })
  :parse(arg)
local name = args.name or path.stem(args.video)
local game = G.launch({ out = ok.root .. "/Saved/Tests/VideoImport/" .. ok.time.stamp(), card = args.card, game = args.game, editor = args.editor })

local good, err = xpcall(function()
  game:wait_ready(tonumber(args["ready-timeout"] or args.ready_timeout or 300))
  game:check("editor ready", true)
  local r = game:call("video_import", { path = path.abs(args.video):gsub("/", "\\"), name = name, static = args.static and true or false,
    pair = args.pair and true or false, male_left = not args.female_left })
  game:check("import started", r.ok, r.error or "")
  if not r.ok then return end
  local began, last = ok.time.clock(), ""
  while true do
    local st = game:state()
    if st.video_status ~= last then last = st.video_status; print("   " .. (last or ""):gsub("\n", " | ")); io.stdout:flush() end
    if st.video_importing == false then break end
    if ok.time.clock() - began > tonumber(args.timeout) then error("import timeout", 0) end
    ok.time.sleep(2)
  end
  local seconds = ok.time.clock() - began
  local dir = ok.root .. "/Saved/VideoMotions/"
  if args.pair then
    game:check("pair manifest written", path.is_file(dir .. name .. ".pair.json"), name .. ".pair.json")
    local fj = dir .. name .. "_女.json"
    local info = path.is_file(fj) and json.decode(path.read_text(fj)) or {}
    game:check("converted by ok-video", info.converter == "ok-video", tostring(info.converter))
    local sel = game:call("video_motion", { item = "【雙人】" .. name })
    game:check("pair selectable", sel.ok, sel.error or "")
    local loaded, t0 = false, ok.time.clock()
    while ok.time.clock() - t0 < 120 do
      local st = game:state()
      if st.partner_loaded and st.partner_pose_mode == 2 then loaded = true; break end
      ok.time.sleep(1)
    end
    game:check("partner plays male half", loaded)
  else
    game:check("okanim written", path.is_file(dir .. name .. ".okanim"), name .. ".okanim")
    local info = path.is_file(dir .. name .. ".json") and json.decode(path.read_text(dir .. name .. ".json")) or {}
    game:check("converted by ok-video", info.converter == "ok-video", ("bad_frames=%s frames=%s"):format(tostring(info.bad_frames), tostring(info.frames)))
    local sel = game:call("video_motion", { item = name })
    game:check("motion selectable", sel.ok, sel.error or "")
    ok.time.sleep(1.5)
    local st = game:state()
    game:check("motion playing", st.selected_clip == "video:|" .. name, tostring(st.selected_clip) .. " " .. tostring(st.animation_status))
  end
  print(("import took %.0fs; status: %s"):format(seconds, (last or ""):gsub("\n", " | ")))
end, debug.traceback)
os.exit(game:finish(not good and err or nil))
