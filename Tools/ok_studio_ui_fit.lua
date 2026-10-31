-- U28 small-window check: with OK Studio running under -Control (e.g. 1280x720:
--   UnrealEditor.exe OpenKoikatsu.uproject -game -windowed -ResX=1280 -ResY=720 -Control -ExecCmds=OKStudio.Play)
-- opens every floating window and detail page in turn and lists what does not fit (control op ui_fit): widgets reaching
-- out of their window, text cut short, windows outside the view. A UI screenshot of each goes to
-- Saved/OKStudio/mcp_ui/<tag>_<name>.png. Exits 1 when anything does not fit.
-- Tools/lua/lua.exe Tools/ok_studio_ui_fit.lua --scene <.okscene with a character> [--tag fit] [--port 47890]
local ok = require "ok"
local json = require "ok.json"
local p = ok.args.parser("OK Studio window fit check")
p:opt("--scene", { required = true })
p:opt("--tag", { default = "fit" })
p:opt("--port", { default = "47890" })
local a = p:parse(arg)
local tmp = os.getenv("TEMP") .. "\\ok_ui_fit.json"
local function call(op, body)
  local f = assert(io.open(tmp, "wb")); f:write(json.encode(body or {})); f:close()
  local h = io.popen(('curl -s -m 600 -X POST http://127.0.0.1:%s/studio/%s --data-binary "@%s"'):format(a.port, op, tmp))
  local out = h:read("a"); h:close()
  local good, v = pcall(json.decode, out)
  return good and v or { ok = false, error = out }
end
local function sleep(s) os.execute("ping -n " .. (s + 1) .. " 127.0.0.1 >nul") end
local function idle()
  for _ = 1, 200 do
    local s = call("state")
    if s.ok and (s.busy == nil or s.busy == "") then return s end
    sleep(3)
  end
  error("studio stays busy")
end
for _ = 1, 60 do if call("state").ok then break end sleep(5) end
assert(call("load", { path = a.scene }).ok, "scene did not load")
local s = idle()
local character
for _, o in ipairs(s.objects or {}) do if o.kind == "character" then character = o.id break end end
local total = 0
local function check(name)
  sleep(1)
  local r = call("ui_fit")
  assert(r.ok, "ui_fit: " .. tostring(r.error))
  call("ui_shot", { name = a.tag .. "_" .. name })
  total = total + #r.problems
  print(("%-16s %d  [%s]"):format(name, #r.problems, r.view))
  for _, x in ipairs(r.problems) do print("    " .. x.region, x.what, x.widget, "over " .. x.over) end
end
check("shell")
local floats = { "timeline", "mmd", "cards", "scenes", "quick", "poselib", "effects", "maps", "color", "settings", "shot", "export",
  "report", "resources", "backups", "recent", "stories", "material", "animctl", "help", "dialog" }
for _, id in ipairs(floats) do
  call("ui_fit", { open = id })
  check("float_" .. id)
  call("ui_fit", { close = id })
end
if character then call("select", { ids = { character } }) else print("no character in the scene: its pages are not checked") end
-- EPage: 1 object, 2 state, 3 bones (7 sub pages), 4 anim, 5 scene, 8 item add, 9 light add, 10 camera.
local pages = { [1] = "object", [2] = "state", [3] = "bones", [4] = "anim", [5] = "scene", [8] = "itemadd", [9] = "lightadd", [10] = "camera" }
for n = 1, 10 do
  if pages[n] then
    if n == 3 then
      for k = 0, 6 do call("ui_fit", { page = 3, bones = k }); check("page_bones" .. k) end
    else
      call("ui_fit", { page = n }); check("page_" .. pages[n])
    end
  end
end
call("ui_fit", { page = 0 })
print("problems", total)
os.exit(total == 0 and 0 or 1)
