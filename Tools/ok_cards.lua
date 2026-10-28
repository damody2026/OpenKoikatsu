-- OK cards in bulk: convert original KKS cards, check them, time them, install them. See Docs/OK_BASE_LIBRARY_PLAN.md
-- (「OK 卡只靠自己開卡」與「批次轉換」).
--
--   Tools\lua\lua.exe Tools\ok_cards.lua convert <card list> <out dir> [--zipmods DIR] [--workers N]
--     ok-extract --oklib cards: worker processes each set up the library once, then convert their share (one pass per
--     card, okstrict.rs). Zipmods come from DIR only (default D:/KoikatuSunshine/mods), never from a game.
--   Tools\lua\lua.exe Tools\ok_cards.lua verify <card dir|list> [--jobs N] [--report FILE]
--     Every card in a new process with an empty temporary folder and no disk cache: every outfit must open strictly
--     (okLc/okLt and shared resources only). A gap is a conversion bug. Do not run conversions meanwhile.
--   Tools\lua\lua.exe Tools\ok_cards.lua cold <card dir|list> [--report FILE]
--     Outfit 0 opened once per card in a new process (empty temporary folder, no cache), one card at a time: the cold
--     read. Ask the other sessions to pause their games and builds first.
--   Tools\lua\lua.exe Tools\ok_cards.lua install <card list> <OK card dir> <crowd dir>
--     The listed cards' OK cards become <crowd dir>/C000.png.. in list order (old C###.png past them removed);
--     sources.tsv and selection.json are rewritten. Only after verify passed for all of them.
--
-- --exe picks ok-extract.exe (default the release build); the bench (ok-card-open-bench.exe) is taken beside it.
local ok = require "ok"
local fs = require "okfs"
local path, proc = ok.path, ok.proc

local root = path.parent(path.parent(path.abs(arg[0])))
local lib = path.join(root, "Content/IndependentResources/base_v1")
local cmd = arg[1]
local opts, pos = { exe = path.join(root, "Tools/ok-extract/target/release/ok-extract.exe"), zipmods = "D:/KoikatuSunshine/mods" }, {}
local i = 2
while i <= #arg do
  local a = arg[i]
  if a:sub(1, 2) == "--" then opts[a:sub(3)] = arg[i + 1]; i = i + 2 else pos[#pos + 1] = a; i = i + 1 end
end
local bench = path.join(path.parent(opts.exe), "ok-card-open-bench.exe")

local function cards_of(input)
  if path.is_dir(input) then return path.glob(input, "*.png") end
  local out = {}
  for line in io.lines(input) do if line:match("%S") then out[#out + 1] = line end end
  return out
end

-- A new process for `args` with its own empty temporary folder and no disk cache; returns a handle to poll.
local function start(args, out)
  local tmp = path.temp("")
  path.remove(tmp, true)
  path.mkdir(tmp, true)
  local saved = {}
  for k, v in pairs({ TMP = tmp, TEMP = tmp, OK_LIBRARY_CACHE = false, OK_NOCACHE = "1" }) do saved[k] = fs.getenv(k) or false; fs.setenv(k, v or nil) end
  local p = proc.popen(args, { stdout = out, stderr = "NUL", hide = true })
  for k, v in pairs(saved) do fs.setenv(k, v or nil) end
  return { p = p, tmp = tmp, out = out }
end
local function finish(job)
  job.p:wait()
  local text = path.is_file(job.out) and path.read_text(job.out) or ""
  path.remove(job.out, true)
  path.rmtree(job.tmp)
  return text
end

-- Runs `make(card)` for every card, `jobs` at a time; `done(card, text)` gets each one's output.
local function each(cards, jobs, make, done)
  local running, next_card = {}, 1
  while next_card <= #cards or #running > 0 do
    while #running < jobs and next_card <= #cards do
      local card = cards[next_card]; next_card = next_card + 1
      running[#running + 1] = { card = card, job = start(make(card), path.temp(".out")) }
    end
    local finished = false
    for k = #running, 1, -1 do
      if running[k].job.p:poll() ~= nil then
        local r = table.remove(running, k)
        done(r.card, finish(r.job)); finished = true
      end
    end
    if not finished then fs.sleep(0.2) end
  end
end

local function report(file, header, rows)
  if not file then return end
  path.write_text(file, header .. "\n" .. table.concat(rows, "\n") .. "\n")
end

if cmd == "convert" and pos[2] then
  local res = proc.run({ opts.exe, opts.zipmods, "--oklib", "cards", lib, pos[1], pos[2] },
    { env = { OK_CARDS_WORKERS = opts.workers or false, OK_LIBRARY_CACHE = false, OK_NOCACHE = "1" } })
  os.exit(res.ok and 0 or 1)
elseif cmd == "verify" and pos[1] then
  local cards, rows, failed = cards_of(pos[1]), {}, 0
  local jobs = tonumber(opts.jobs) or 8
  each(cards, jobs, function(card) return { opts.exe, "X:/NoGame", "--oklib", "strict-check", lib, card } end, function(card, text)
    local line = text:match("OKSTRICT [^\n]*") or ("OKSTRICT error\t" .. card .. "\tno result")
    local passed = line:find("^OKSTRICT ok") ~= nil
    if not passed then failed = failed + 1 end
    rows[#rows + 1] = (passed and "ok\t" or "FAIL\t") .. path.name(card) .. "\t" .. (passed and "" or line:gsub("\t", " "):sub(1, 4000))
    print(rows[#rows]:sub(1, 300)); io.stdout:flush()
  end)
  table.sort(rows)
  report(opts.report, "result\tcard\tgaps", rows)
  print(("verify: %d cards, %d failed"):format(#cards, failed))
  os.exit(failed == 0 and 0 or 1)
elseif cmd == "cold" and pos[1] then
  local cards, rows, times = cards_of(pos[1]), {}, {}
  each(cards, 1, function(card) return { bench, "--sequence", lib, card .. "@0" } end, function(card, text)
    local s = tonumber(text:match('"seconds":%s*([%d.]+)'))
    rows[#rows + 1] = ("%s\t%s"):format(s and ("%.2f"):format(s) or "error", path.name(card))
    if s then times[#times + 1] = s end
    print(rows[#rows]); io.stdout:flush()
  end)
  table.sort(times)
  local sum = 0; for _, t in ipairs(times) do sum = sum + t end
  local mean = #times > 0 and sum / #times or 0
  print(("cold: %d cards, mean %.2fs, median %.2fs, max %.2fs, over 3s %d"):format(#times, mean, times[math.max(1, #times // 2 + 1)] or 0,
    times[#times] or 0, (function() local n = 0; for _, t in ipairs(times) do if t > 3 then n = n + 1 end end; return n end)()))
  report(opts.report, "seconds\tcard", rows)
elseif cmd == "install" and pos[3] then
  local list, okdir, dest = pos[1], pos[2], pos[3]
  local rows, cards = { "card\tsource" }, {}
  for _, card in ipairs(cards_of(list)) do
    local made = path.join(okdir, path.name(card))
    assert(path.is_file(made), "missing " .. made)
    local name = ("C%03d.png"):format(#cards)
    path.write_bytes(path.join(dest, name), path.read_bytes(made), { atomic = true })
    cards[#cards + 1] = path.abs(path.join(dest, name)):gsub("/", "\\")
    rows[#rows + 1] = name .. "\t" .. card
  end
  local removed = 0
  for _, name in ipairs(path.list(dest)) do
    local n = tonumber(name:match("^C(%d%d%d)%.png$") or "")
    if n and n >= #cards then path.remove(path.join(dest, name)); removed = removed + 1 end
  end
  path.write_bytes(path.join(dest, "sources.tsv"), table.concat(rows, "\n") .. "\n")
  local selection = path.join(dest, "selection.json")
  if path.is_file(selection) then
    local s = ok.read_json(selection)
    s.cards = cards
    ok.write_json(selection, s, { indent = 2 })
  end
  print(("installed %d OK cards in %s, removed %d old"):format(#cards, dest, removed))
else
  io.stderr:write("usage: ok_cards.lua convert|verify|cold|install ... (see the top of this file)\n")
  os.exit(2)
end
