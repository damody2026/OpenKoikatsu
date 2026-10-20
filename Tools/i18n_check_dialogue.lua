-- Development tool: check translated dialogue chunks (Tools/i18n_extract_dialogue.lua) and report what to redo.
--
-- For each chunk_NNN.json, done/chunk_NNN*.json must cover every key with zh-CN and en (and zh-TW where the source had no
-- Chinese), keep the placeholders ([H名] [H] [P] [H姓] [P名]) and the line breaks, and be valid JSON. Prints one line per
-- chunk and the list of chunks to redo; --merge writes <out>/translations.json {key: {zh-TW?, zh-CN, en}} from the good ones.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local T = util.truthy

-- \[(?:Hあだ名|Pあだ名|H名|H姓|P名|H|P)\] in regex alternation order.
local ALTS = { "Hあだ名", "Pあだ名", "H名", "H姓", "P名", "H", "P" }
local function placeholders(s)
  local out, i, n = {}, 1, #s
  while i <= n do
    local matched = false
    if s:byte(i) == 91 then
      for _, alt in ipairs(ALTS) do
        if s:sub(i + 1, i + #alt) == alt and s:sub(i + #alt + 1, i + #alt + 1) == "]" then
          out[#out + 1] = "[" .. alt .. "]"
          i = i + #alt + 2
          matched = true
          break
        end
      end
    end
    if not matched then i = i + 1 end
  end
  return out
end
local function strip_placeholders(s)
  local out, i, n = {}, 1, #s
  while i <= n do
    local matched = false
    if s:byte(i) == 91 then
      for _, alt in ipairs(ALTS) do
        if s:sub(i + 1, i + #alt) == alt and s:sub(i + #alt + 1, i + #alt + 1) == "]" then
          i = i + #alt + 2; matched = true; break
        end
      end
    end
    if not matched then out[#out + 1] = s:sub(i, i); i = i + 1 end
  end
  return table.concat(out)
end
-- Japanese left in a translation (placeholders removed first): [぀-ヿ]
local function has_kana(s)
  for _, cp in utf8.codes(s) do if cp >= 0x3040 and cp <= 0x30FF then return true end end
  return false
end
local function blank(s)
  s = s:gsub("\227\128\128", ""):gsub("\194[\133\160]", ""):gsub("\226\128[\128-\138\168\169\175]", ""):gsub("\226\129\159", "")
  return s:match("^[%s\28-\31]*$") ~= nil
end

-- Python repr() of a str (non-printable characters escaped like CPython).
local function repr_str(v)
  local q = (v:find("'", 1, true) and not v:find('"', 1, true)) and '"' or "'"
  local out = { q }
  for _, cp in utf8.codes(v) do
    local ch = utf8.char(cp)
    if ch == "\\" then out[#out + 1] = "\\\\"
    elseif ch == q then out[#out + 1] = "\\" .. q
    elseif cp == 9 then out[#out + 1] = "\\t"
    elseif cp == 10 then out[#out + 1] = "\\n"
    elseif cp == 13 then out[#out + 1] = "\\r"
    elseif cp < 0x20 or (cp >= 0x7F and cp <= 0xA0) or cp == 0xAD then out[#out + 1] = string.format("\\x%02x", cp)
    elseif (cp >= 0x2000 and cp <= 0x200F) or (cp >= 0x2028 and cp <= 0x202F) or (cp >= 0x205F and cp <= 0x206F) or cp == 0x3000 or cp == 0xFEFF then
      out[#out + 1] = string.format("\\u%04x", cp)
    else out[#out + 1] = ch end
  end
  out[#out + 1] = q
  return table.concat(out)
end
local function repr_list(list)
  local parts = {}
  for i, v in ipairs(list) do parts[i] = repr_str(v) end
  return "[" .. table.concat(parts, ", ") .. "]"
end
local function ulen_prefix(s, n) return util.usub(s, 1, n) end

local function load_done(folder, n)
  local merged, errors = json.object(), {}
  for _, f in ipairs(path.glob(path.join(folder, "done"), "chunk_" .. n .. "*.json")) do
    local okd, value = pcall(function() return json.decode(path.read_text(f)) end)
    if okd then
      for k, v in pairs(value) do merged[k] = v end
    else
      errors[#errors + 1] = path.name(f) .. ": " .. tostring(value)
    end
  end
  return merged, errors
end

local function main()
  local p = ok.args.parser()
  p:opt("--folder", { default = "Saved/StreetSocial/i18n/dialogue" })
  p:flag("--merge")
  local a = p:parse(arg)
  local folder = a.folder
  local redo, good, problems = {}, json.object(), 0
  -- Fixes made after a chunk was done (done/fix*.json, {key: {...}}) replace its entries.
  local fixes = json.object()
  for _, f in ipairs(path.glob(path.join(folder, "done"), "fix*.json")) do
    for k, v in pairs(json.decode(path.read_text(f))) do fixes[k] = v end
  end
  local chunks = {}
  for _, c in ipairs(path.glob(folder, "chunk_*.json")) do
    local part = util.split(path.stem(c), "_")[2]
    if part and util.isdigit(part) then chunks[#chunks + 1] = c end
  end
  chunks = util.sorted(chunks, function(c) return c:lower() end)
  for _, chunk in ipairs(chunks) do
    local n = util.split(path.stem(chunk), "_")[2]
    local rows = json.decode(path.read_text(chunk))
    local done, errors = load_done(folder, n)
    local row_keys = {}
    for _, r in ipairs(rows) do row_keys[r.key] = true end
    for k, v in pairs(fixes) do
      if done[k] ~= nil or row_keys[k] then done[k] = v end
    end
    local bad = util.copy(errors)
    for _, r in ipairs(rows) do
      local t = done[r.key]
      local need = { "zh-CN", "en" }
      if not T(r.zh) then need[#need + 1] = "zh-TW" end
      local missing = not json.is_object(t)
      if not missing then
        for _, x in ipairs(need) do
          if type(t[x]) ~= "string" or blank(t[x]) then missing = true end
        end
      end
      if missing then
        bad[#bad + 1] = r.key .. " missing"
      else
        local src = T(r.jp) and r.jp or r.zh -- the original's placeholders (a Chinese line may have dropped one)
        local want = util.sorted(placeholders(src))
        for _, lang in ipairs(need) do
          if has_kana(strip_placeholders(t[lang])) then
            bad[#bad + 1] = ("%s %s still has Japanese: %s"):format(r.key, lang, ulen_prefix(t[lang], 30))
          end
          local found = placeholders(t[lang])
          if not util.equal(util.sorted(found), want) then
            bad[#bad + 1] = ("%s %s placeholders %s != %s"):format(r.key, lang, repr_list(found), repr_list(want))
          end
        end
        if #bad == 0 or not util.any(bad, function(b) return util.startswith(b, r.key) end) then
          local entry = json.object()
          for _, k in ipairs(need) do entry[k] = t[k] end
          if T(r.zh) and type(t["zh-TW"]) == "string" and not blank(t["zh-TW"]) then entry["zh-TW"] = t["zh-TW"] end
          good[r.key] = entry
        end
      end
    end
    local ndone = util.len(done)
    local status = #bad == 0 and "ok" or (#bad .. " problems")
    if ndone == 0 and #errors == 0 then status = "not done" end
    if #bad > 0 or ndone == 0 then redo[#redo + 1] = n end
    problems = problems + #bad
    print(("chunk_%s: %d rows, %d translated, %s"):format(n, #rows, ndone, status) .. (#bad > 0 and (" e.g. " .. repr_list(util.slice(bad, 1, 2))) or ""))
  end
  print("redo: " .. (#redo > 0 and table.concat(redo, " ") or "(none)"))
  if a.merge then
    path.write_text(path.join(folder, "translations.json"), json.encode(good, { ensure_ascii = false }))
    print(("merged %d lines"):format(util.len(good)))
  end
end

if ok.is_main() then ok.main(main) end
