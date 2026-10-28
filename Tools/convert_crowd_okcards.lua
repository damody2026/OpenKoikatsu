-- Build the crowd card set (Content/CharacterCards/CrowdAPNG) as OK cards made from original KKS cards.
--
--   lua.exe Tools/convert_crowd_okcards.lua list <KKS card dir> <list file> [parts]
--     The .png cards directly in <KKS card dir> (subdirectories are not used), in file name order, written to
--     <list file>; with [parts] also <list file>.0 .. .<parts-1> for parallel
--     `ok-extract <game> --oklib okcard-verify <lib> <list>` runs.
--   Convert with `ok-extract <game> --oklib cards <lib> <list> <OK card dir>` (one process per list), then accept with
--   `ok-extract <game> --oklib okcard-check <lib> <OK card dir> <list> live` (output: OKCHECK lines in logs).
--   lua.exe Tools/convert_crowd_okcards.lua install <list file> <OK card dir> <card dir> <check log>... [--count N]
--     The first N (default 100) listed cards whose OK card, reopened from OK data alone, gives the original card's
--     game-mode digest (an OKCHECK line with closed == live) become <card dir>/C000.png, C001.png, ... in list order.
--     <card dir>/sources.tsv records the original of each, and they replace the card list of <card dir>/selection.json.
local ok = require "ok"
local P = ok.path
local voice = require "ok_card_voice"

local function list(src, out, parts)
  local names = {}
  for _, name in ipairs(P.list(src)) do
    if name:lower():match("%.png$") and P.is_file(P.join(src, name)) then names[#names + 1] = name end
  end
  table.sort(names)
  local lines = {}
  for i, name in ipairs(names) do lines[i] = P.abs(P.join(src, name)) end
  P.write_bytes(out, table.concat(lines, "\n") .. "\n")
  parts = tonumber(parts)
  if parts and parts > 1 then
    for p = 0, parts - 1 do
      local chunk = {}
      for i = p + 1, #lines, parts do chunk[#chunk + 1] = lines[i] end
      P.write_bytes(out .. "." .. p, table.concat(chunk, "\n") .. "\n")
    end
  end
  print(("%d cards listed in %s"):format(#lines, out))
end

local function install(listfile, okdir, dest, logs, count)
  local verdict = {}
  for _, log in ipairs(logs) do
    for line in io.lines(log) do
      local card, closed, live = line:match("^OKCHECK %a+\t([^\t]+)\t[^\t]*\t([^\t]+)\t([^\t]+)")
      if card then verdict[P.abs(card):lower()] = (closed == live and closed ~= "error" and #closed == 32) and "MATCH" or ("closed " .. closed .. " live " .. live) end
    end
  end
  local rows, cards, skipped = { "card\tsource" }, {}, {}
  for card in io.lines(listfile) do
    if card ~= "" and #cards < count then
      local made = P.join(okdir, P.name(card))
      local v = verdict[P.abs(card):lower()]
      if v == "MATCH" and P.is_file(made) then
        local name = ("C%03d.png"):format(#cards)
        P.write_bytes(P.join(dest, name), voice.keep(P.read_bytes(made), P.join(dest, name)), { atomic = true })
        cards[#cards + 1] = P.abs(P.join(dest, name)):gsub("/", "\\")
        rows[#rows + 1] = name .. "\t" .. card
      else
        skipped[#skipped + 1] = ("%s %s"):format(P.name(card), v or "not verified")
      end
    end
  end
  P.write_bytes(P.join(dest, "sources.tsv"), table.concat(rows, "\n") .. "\n")
  local selection = P.join(dest, "selection.json")
  if P.is_file(selection) then
    local s = ok.read_json(selection)
    s.cards = cards
    ok.write_json(selection, s, { indent = 2 })
  end
  print(("installed %d OK cards in %s"):format(#cards, dest))
  for _, f in ipairs(skipped) do print("  skipped: " .. f) end
end

ok.main(function()
  local cmd = arg[1]
  if cmd == "list" and arg[3] then return list(arg[2], arg[3], arg[4]) end
  if cmd == "install" and arg[5] then
    local logs, count = {}, 100
    local i = 5
    while arg[i] do
      if arg[i] == "--count" then count = tonumber(arg[i + 1]); i = i + 2 else logs[#logs + 1] = arg[i]; i = i + 1 end
    end
    return install(arg[2], arg[3], arg[4], logs, count)
  end
  error("usage: convert_crowd_okcards.lua list <KKS card dir> <list file> [parts] | install <list file> <OK card dir> <card dir> <check log>... [--count N]")
end)
