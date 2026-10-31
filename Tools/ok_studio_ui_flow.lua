-- Runs a scene workflow on OK Studio through the control channel, one step per line of a steps file, the way a person
-- would: buttons, list rows and fields are operated through their own handlers ("click", "type").
-- Needs a studio started with -Control (Tools/run_scene_editor.lua -- -Control -StudioControlPort=47890).
--   Tools/lua/lua.exe Tools/ok_studio_ui_flow.lua <steps file> [--port 47890]
-- Steps (Tools/ok_studio_flows/*.txt):
--   t:text[#i]   click the button / list row whose text is text (i-th match)      c:text[#i]  ... whose text contains text
--   g:tag[#i]    ... by widget tag (picture buttons)                               p:text[#i]  icon button whose tooltip contains text
--   y:field=text[!]  type into the field whose hint or text contains field (! = Enter)
--   o:op {json}  any control op      w:N  wait N seconds      s:name  UI screenshot (Saved/OKStudio/mcp_ui/<name>.png)
--   ?field=value step   the step only when the state's ui.field is (?field!=value: is not) value ("?timeline=false p:時間軸")
--   ?closed:window step  the step only when that window (state ui.windows) is not open ("?closed:shot t:截圖")
--   # comment.  {out} becomes Saved/OKStudio/flow/<stamp>, {stamp} the run's date and time.
-- Prints each step and its reply; exits 1 when a step failed.
local ok = require "ok"
local json, path = ok.json, ok.path
local steps, port = nil, "47890"
local i = 1
while i <= #arg do
  if arg[i] == "--port" then port = arg[i + 1]; i = i + 2 else steps = arg[i]; i = i + 1 end
end
assert(steps, "usage: ok_studio_ui_flow.lua <steps file> [--port N]")
local stamp = os.date("%Y%m%d_%H%M%S")
local out = ok.root .. "/Saved/OKStudio/flow/" .. stamp
path.mkdir(out, true)
local tmp = os.getenv("TEMP") .. "\\ok_studio_flow.json"

local function call(op, body)
  local f = assert(io.open(tmp, "wb")); f:write(type(body) == "string" and body or json.encode(body)); f:close()
  local p = io.popen(('curl -s -m 300 -X POST http://127.0.0.1:%s/studio/%s --data-binary "@%s"'):format(port, op, tmp))
  local reply = p:read("a"); p:close()
  return reply
end
local function sleep(s) os.execute(("ping -n %d 127.0.0.1 >nul"):format(math.floor(s) + 1)) end

local failed = 0
for line in io.lines(steps) do
  line = line:gsub("{out}", out):gsub("{stamp}", stamp)
  -- "?field=value step" / "?field!=value step": only when the state's ui.field is (is not) value (toggle buttons:
  -- open a window or menu that is not open yet).
  -- "?closed:window step": only when that window (ui.windows) is not open.
  local field, op, want, step = line:match("^%?([%w_]+)(!?=)(%S+)%s+(.*)$")
  local closed, step2 = line:match("^%?closed:(%S+)%s+(.*)$")
  if field or closed then
    local ok_, state = pcall(json.decode, call("state", {}))
    local ui = ok_ and state.ui or {}
    if field then
      local same = tostring(ui[field]) == want
      line = (same == (op == "=")) and step or ""
    else
      local open = false
      for _, w in ipairs(ui.windows or {}) do if w == closed or (type(w) == "table" and w.id == closed) then open = true end end
      line = open and "" or step2
    end
  end
  local kind, rest = line:match("^(%a):(.*)$")
  local reply
  if not kind then -- blank or comment
  elseif kind == "w" then sleep(tonumber(rest))
  elseif kind == "s" then reply = call("ui_shot", { name = rest })
  elseif kind == "o" then local op, body = rest:match("^(%S+)%s*(.*)$"); reply = call(op, body ~= "" and body or "{}")
  elseif kind == "y" then
    local field, text = rest:match("^(.-)=(.*)$")
    local enter = text:sub(-1) == "!"
    if enter then text = text:sub(1, -2) end
    reply = call("type", { field = field, text = text, enter = enter })
  else
    local text, index = rest:match("^(.-)#(%d+)$")
    text = text or rest
    reply = call("click", { text = text, index = tonumber(index or 0), tag = kind == "g" or nil,
      contains = (kind == "c" or kind == "p") or nil, tip = kind == "p" or nil })
  end
  if reply then
    print(line, reply:sub(1, 300))
    if not reply:find('"ok":true', 1, true) then failed = failed + 1 end
    sleep(0.6)
  end
end
print(("FLOW %s: %d failed steps, output %s"):format(steps, failed, out))
os.exit(failed == 0 and 0 or 1)
