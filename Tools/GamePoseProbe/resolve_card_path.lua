-- Resolve a character card file name against the game's user and default card folders
-- (female/male). Required by run_dump_cards.lua and run_renderer_native.lua.
--   local resolve = require("resolve_card_path").resolve_kk_card_path
--   local card = resolve(ok.game_root(), "KK_384283.png")   -- absolute path, backslashes
local ok = require "ok"
local path = ok.path

local M = {}

function M.resolve_kk_card_path(game_root, name)
  -- [IO.Path]::GetFileName($Name) -ne $Name -or GetExtension -ne '.png' (PowerShell -ne ignores case)
  if name == "" or name:find("[/\\:]") or path.suffix(name):lower() ~= ".png" then error("Expected a card filename", 0) end
  local matches = {}
  for _, parent in ipairs({ "UserData/chara/female", "UserData/chara/male", "DefaultData/chara/female", "DefaultData/chara/male" }) do
    local candidate = path.join(game_root, parent, name)
    if path.is_file(candidate) then matches[#matches + 1] = (path.abs(candidate):gsub("/", "\\")) end
  end
  if #matches == 0 then error("Missing card: " .. name, 0) end
  if #matches > 1 then
    local first = ok.hash.sha256_file(matches[1])
    for i = 2, #matches do
      if ok.hash.sha256_file(matches[i]) ~= first then
        error("Ambiguous card filename: " .. name .. " (" .. table.concat(matches, ", ") .. ")", 0)
      end
    end
  end
  return matches[1]
end

return M
