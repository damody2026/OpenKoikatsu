-- Small synchronous stdio MCP client for local scripts and acceptance tests.
--   local Client = require "client"
--   local c = Client.new(mailbox, guard, server)   -- guard(): raise to abort waiting
--   local result, extra = c:call("character_state")
--   c:close()
local ok = require "ok"
local json = ok.json

local Client = {}
Client.__index = Client

local function trace(self, line)
  ok.path.append_text(self.trace, line .. "\n")
end

function Client.new(mailbox, guard, server)
  local self = setmetatable({ guard = guard, index = 0 }, Client)
  self.trace = ok.path.join(mailbox, "client-trace.log")
  ok.path.mkdir(mailbox)
  server = server or ok.path.join(ok.script_dir, "bin/ok-character-mcp.exe")
  -- stderr goes to a file: an unread stderr pipe could fill and stall the server.
  self.process = ok.proc.popen({ server, "--mailbox", mailbox, "--timeout", "1800" },
    { stderr = ok.path.join(mailbox, "server-stderr.log") })
  local okv, err = pcall(function()
    self:rpc("initialize", json.object({ { "protocolVersion", "2025-11-25" },
      { "clientInfo", json.object({ { "name", "character-fit-client" }, { "version", "1" } }) },
      { "capabilities", json.object() } }))
    self.process:write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
  end)
  if not okv then self:close(); error(err, 0) end
  return self
end

function Client:rpc(method, params)
  self.index = self.index + 1
  trace(self, ("%s send %d %s %s"):format(ok.util.num(ok.time.time()), self.index, method, params.name or ""))
  local request = json.object({ { "jsonrpc", "2.0" }, { "id", self.index }, { "method", method }, { "params", params } })
  self.process:write(json.encode(request) .. "\n")
  local long = method == "tools/call" and (params.name == "body_fit" or params.name == "body_sensitivity")
  local deadline = ok.time.clock() + (long and 7200 or 1830)
  local line, err
  while true do
    if self.guard then self.guard() end
    line, err = self.process:read_line(0.5)
    if line or err == "eof" then break end
    if ok.time.clock() >= deadline then error("MCP response timeout", 0) end
  end
  trace(self, ("%s received %d"):format(ok.util.num(ok.time.time()), self.index))
  if not line then error("MCP server closed unexpectedly", 0) end
  local response = json.decode(line)
  if response.id ~= self.index then error("Unexpected MCP response ID", 0) end
  if response.error ~= nil then error(json.encode(response.error), 0) end
  return response.result
end

-- call(name, arguments, expect_error) -> decoded first text content, remaining contents
function Client:call(name, arguments, expect_error)
  local result = self:rpc("tools/call", json.object({ { "name", name }, { "arguments", arguments or json.object() } }))
  if expect_error then
    assert(ok.util.truthy(result.isError), json.encode(result))
    return result
  end
  if ok.util.truthy(result.isError) then error(result.content[1].text, 0) end
  return json.decode(result.content[1].text), ok.util.slice(result.content, 2)
end

function Client:close()
  if not self.process then return end
  self.process:close_stdin()
  if not self.process:wait(30) then
    -- Only the private server subprocess created by this client.
    self.process:kill()
    self.process:wait(10)
  end
  self.process = nil
end

return Client
