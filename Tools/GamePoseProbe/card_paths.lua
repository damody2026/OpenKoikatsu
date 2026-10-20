-- Resolve user/default character cards without assuming their sex.
local ok = require "ok"
local path = ok.path

local M = {}

-- resolve_card(game, name) -> absolute card path (forward slashes).
-- Errors: "ValueError: Expected card filename", "FileNotFoundError: <name>",
-- "ValueError: Ambiguous character card: <name>".
function M.resolve_card(game, name)
  if name:find("[/\\]") or name:match("^%a:") or name == "" or name == "." or name == ".." or path.suffix(name) ~= ".png" then
    error("ValueError: Expected card filename", 2)
  end
  local matches = {}
  for _, parent in ipairs({ "UserData/chara/female", "UserData/chara/male", "DefaultData/chara/female", "DefaultData/chara/male" }) do
    local p = path.join(game, parent, name)
    if path.is_file(p) then matches[#matches + 1] = p end
  end
  if #matches == 0 then error("FileNotFoundError: " .. name, 2) end
  local first = ok.hash.sha256_file(matches[1])
  for i = 2, #matches do
    if ok.hash.sha256_file(matches[i]) ~= first then error("ValueError: Ambiguous character card: " .. name, 2) end
  end
  return path.abs(matches[1])
end

return M
