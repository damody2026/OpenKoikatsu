-- Development tool: split the original-game dialogue (catalogue nodes and topic lines) into chunks for translation.
--
-- Each unique line (by its Japanese and Traditional Chinese text) is one row: {key, personality, jp, zh}. Rows are sorted by
-- personality so a chunk keeps one speaking voice. Writes <out>/chunk_NNN.json and <out>/index.json (key -> where used).
-- The translations come back as <out>/done/chunk_NNN.json: {key: {"zh-CN": ..., "en": ...}} (Tools/i18n_merge_dialogue.lua).
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local T = util.truthy
local M = {}

function M.key_of(jp, zh) return ok.hash.sha1(jp .. "\31" .. zh):sub(1, 16) end
local key_of = M.key_of
local function get(t, k, d) local v = t[k]; if v == nil then return d end; return v end

local function main()
  local p = ok.args.parser()
  p:opt("--data", { default = "Content/IndependentResources/StreetSocialData/RuntimeV5" })
  p:opt("--overlay", { default = "Content/IndependentResources/StreetSocialData/RuntimeV2/zh_tw.json" })
  p:opt("--out", { required = true })
  p:opt("--size", { type = "int", default = 250 })
  local a = p:parse(arg)
  local data, out = a.data, a.out
  path.mkdir(out)
  local overlay = {}
  if path.is_file(a.overlay) then overlay = get(json.decode(path.read_text(a.overlay)), "translations", {}) end
  local rows, order, index = {}, {}, json.object()
  local function add(jp, zh, personality, where)
    if not T(zh) then zh = get(overlay, jp, "") end
    if not (T(jp) or T(zh)) then return end
    local k = key_of(jp, zh)
    if rows[k] == nil then
      rows[k] = json.object({ { "key", k }, { "personality", personality }, { "jp", jp }, { "zh", zh } })
      order[#order + 1] = rows[k]
    end
    if index[k] == nil then index[k] = {} end
    table.insert(index[k], where)
  end
  local cat = json.decode(path.read_text(path.join(data, "catalogue.json")))
  for _, n in ipairs(cat.nodes) do
    add(get(n, "text_jp", ""), get(n, "text_zh", ""), get(n, "personality", -1), "node:" .. n.id)
  end
  local topics = json.decode(path.read_text(path.join(data, "topics.json")))
  for pi, prof in ipairs(topics.profiles) do
    local pers = get(prof, "personality", -1)
    for li, listen in ipairs(prof.listen) do
      for kind, seq in pairs(listen) do
        if kind ~= "topic" then
          for si, part in ipairs(seq) do
            add(get(part, "text_jp", ""), get(part, "text_zh", ""), pers, ("topic:%d:%d:%s:%d"):format(pi - 1, li - 1, kind, si - 1))
          end
        end
      end
    end
    for kind, choices in pairs(get(prof, "offer", json.object())) do
      for ci, seq in ipairs(choices) do
        for si, part in ipairs(seq) do
          add(get(part, "text_jp", ""), get(part, "text_zh", ""), pers, ("offer:%d:%s:%d:%d"):format(pi - 1, kind, ci - 1, si - 1))
        end
      end
    end
  end
  local ordered = util.sorted(order, function(r) return { r.personality, r.key } end)
  local chunks = {}
  for i = 1, #ordered, a.size do chunks[#chunks + 1] = util.slice(ordered, i, i + a.size - 1) end
  for i, chunk in ipairs(chunks) do
    path.write_text(path.join(out, ("chunk_%03d.json"):format(i - 1)), json.encode(chunk, { ensure_ascii = false, indent = 0 }))
  end
  path.write_text(path.join(out, "index.json"), json.encode(index, { ensure_ascii = false }))
  local without = 0
  for _, r in ipairs(ordered) do if not T(r.zh) then without = without + 1 end end
  print(("%d unique lines, %d chunks, %d without Chinese"):format(#ordered, #chunks, without))
end

if ok.is_main() then ok.main(main) end

return M
