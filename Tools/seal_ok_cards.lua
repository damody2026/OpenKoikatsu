-- Seals OK cards: each then opens from its own data (okLc, okLt) and the base library's shared resources only
-- (Docs/OK_BASE_LIBRARY_PLAN.md「OK 卡只靠自己開卡」). Every card is backed up, sealed into a work folder
-- (ok-extract --oklib seal), opened strictly in a fresh process (--oklib strict-check), and only then put in place of
-- the original. A card that already opens strictly is left as it is.
--
--   Tools\lua\lua.exe Tools\seal_ok_cards.lua --backup <dir> [--lib <base library>] [--exe <ok-extract.exe>]
--       [--shard i/n] [--log <file>] <card or folder>...
--
-- A folder stands for the OK cards (PNG with an okLb chunk) under it. --shard i/n takes every n-th card from the i-th:
-- run n of these at once. The backup keeps each card's path relative to the project (or its drive).
local ok = require "ok"
local path, proc = ok.path, ok.proc

local root = path.parent(path.parent(path.abs(arg[0])))
local opts, inputs = { lib = path.join(root, "Content/IndependentResources/base_v1"), exe = path.join(root, "Tools/ok-extract/target/release/ok-extract.exe") }, {}
local i = 1
while i <= #arg do
  local a = arg[i]
  if a == "--backup" or a == "--lib" or a == "--exe" or a == "--shard" or a == "--log" then opts[a:sub(3)] = arg[i + 1]; i = i + 2
  else inputs[#inputs + 1] = a; i = i + 1 end
end
if not opts.backup or #inputs == 0 then error("usage: seal_ok_cards.lua --backup <dir> [--lib L] [--exe E] [--shard i/n] [--log F] <card or folder>...", 0) end
local shard, shards = 1, 1
if opts.shard then shard, shards = opts.shard:match("^(%d+)/(%d+)$"); shard, shards = tonumber(shard), tonumber(shards) end

-- The PNG chunk types of a file (headers only: the image data is skipped).
local function chunks(file)
  local f = io.open(file, "rb")
  if not f then return {} end
  local kinds = {}
  if f:read(8) == "\137PNG\r\n\26\n" then
    while true do
      local head = f:read(8)
      if not head or #head < 8 then break end
      local len, kind = string.unpack(">I4c4", head)
      kinds[kind] = true
      if kind == "IEND" then break end
      f:seek("cur", len + 4)
    end
  end
  f:close()
  return kinds
end

local cards = {}
for _, input in ipairs(inputs) do
  local list = path.is_dir(input) and path.rglob(input, "*.png") or { input }
  for _, c in ipairs(list) do if chunks(c).okLb then cards[#cards + 1] = path.abs(c) end end
end
table.sort(cards)

local function log(line)
  line = os.date("[%H:%M:%S] ") .. line
  print(line); io.stdout:flush()
  if opts.log then path.append_text(opts.log, line .. "\n") end
end

local function backup_of(card)
  local rel = path.is_relative_to(card, root) and path.rel(card, root) or card:gsub("^(%a):", "%1")
  return path.join(opts.backup, rel)
end

local function strict_check(card)
  local res = proc.run({ opts.exe, "X:/NoGame", "--oklib", "strict-check", opts.lib, card }, { capture = true })
  local out = res.stdout or ""
  return out:find("OKSTRICT ok", 1, true) ~= nil, out:match("OKSTRICT error\t[^\t]*\t([^\n]*)") or (res.stderr or ""):sub(-400)
end

local work = path.join(opts.backup, ("work_%d"):format(shard))
path.mkdir(work, true)
local done, failed = 0, 0
for n, card in ipairs(cards) do
  if (n - 1) % shards == shard - 1 then
    local t = os.time()
    local before = path.size(card)
    if chunks(card).okLt and strict_check(card) then
      log(("SKIP %s (already opens strictly)"):format(card))
    else
      local saved = backup_of(card)
      if not path.exists(saved) then path.mkdir(path.parent(saved), true); path.copy(card, saved) end
      local sealed = path.join(work, path.name(card))
      local res = proc.run({ opts.exe, "X:/NoGame", "--oklib", "seal", opts.lib, card, "--out", work }, { capture = true })
      local line = (res.stdout or ""):match("OKSEAL [^\n]*") or ("OKSEAL error " .. (res.stderr or ""):sub(-400))
      if not line:find("^OKSEAL ok") or not path.is_file(sealed) then
        failed = failed + 1
        log(("FAIL seal %s: %s"):format(card, line))
      else
        local passed, why = strict_check(sealed)
        if not passed then
          failed = failed + 1
          log(("FAIL strict %s: %s"):format(card, why))
        else
          path.replace(sealed, card)
          done = done + 1
          log(("OK %s %d -> %d bytes (%+.1f%%) %s %ds"):format(card, before, path.size(card), (path.size(card) / before - 1) * 100,
            line:match("(added [^\t]*)") or "", os.time() - t))
        end
      end
    end
  end
end
log(("shard %d/%d: sealed %d, failed %d"):format(shard, shards, done, failed))
os.exit(failed == 0 and 0 or 1)
