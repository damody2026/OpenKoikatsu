-- ok_card_voice: a card's own TTS voice (its okVo chunk, Tools/ok-tts/src/cardvoice.rs) across reconversions.
--   local voice = require "ok_card_voice"
--   path.write_bytes(dest, voice.keep(new_bytes, dest), { atomic = true })
-- A card converted from an original KKS card has no voice: written over a card that has one, it would drop it (on
-- 10-25 the street heroines were reconverted from their original cards and lost the voices written into them on 10-24).
-- Card identities skip okVo (oklib.rs card_identity, prepare_street_social_selection.lua), so carrying it changes none.
local ok = require "ok"
local path = ok.path

local M = {}

-- The okVo chunk whole (length, type, data, CRC), or nil, and where IEND starts (an original card's data follows IEND).
-- Chunks are walked: the four bytes "okVo" also turn up inside a card pack.
function M.chunk(data)
  local at, voice = 9, nil
  while at + 11 <= #data do
    local len = string.unpack(">I4", data, at)
    local kind = data:sub(at + 4, at + 7)
    if kind == "okVo" then voice = data:sub(at, at + 11 + len) end
    if kind == "IEND" then return voice, at end
    at = at + 12 + len
  end
  return voice, nil
end

-- The new card's bytes for `dest`: when it has no voice and the card at `dest` has one, that voice before its IEND
-- (where ok-tts card-voice puts it).
function M.keep(made, dest)
  local own, iend = M.chunk(made)
  if own or not path.is_file(dest) then return made end
  local voice = M.chunk(path.read_bytes(dest))
  if not voice then return made end
  assert(iend, "the new card for " .. dest .. " has no IEND")
  print("kept the voice of " .. dest)
  return made:sub(1, iend - 1) .. voice .. made:sub(iend)
end

return M
