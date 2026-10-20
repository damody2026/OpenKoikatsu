-- Publish a verified capture, preserving and verifying the previous PNG batch.
local ok = require "ok"
local G = require "gpp"
local json, path, util = ok.json, ok.path, ok.util
local O, A, check = G.O, G.A, G.check

local M = {}
local DOC = "Publish a verified capture, preserving and verifying the previous PNG batch."

local function digest(p) return G.digest(p) end
M.digest = digest
local function read(p) return G.read_json(p) end
-- Path(p).resolve() compared like Windows paths (case-insensitive).
local function norm_key(p) return G.winpath(path.abs(p)):lower() end
local function truthy(v)
  if v == nil or v == json.null or v == false or v == 0 or v == "" then return false end
  if type(v) == "table" then return next(v) ~= nil end
  return true
end
local function same_map(a, b)
  for k, v in pairs(a) do if b[k] ~= v then return false end end
  for k in pairs(b) do if a[k] == nil then return false end end
  return true
end

local function main()
  local p = ok.args.parser(DOC)
  for _, name in ipairs({ "audit", "regression", "visual", "baseline", "official", "archive", "output" }) do p:arg(name) end
  p:opt("--source-gpu-audit", { help = "Require zero clothes CPU fallback for this source batch" })
  local args = p:parse(arg)
  local audit = read(args.audit)
  local regression = read(args.regression)
  local visual = read(args.visual)
  local baseline = read(args.baseline)
  check(audit.result == "pass" and truthy(regression.passed) and truthy(visual.passed) and baseline.result == "pass")
  check(visual.audit_sha256 == digest(args.audit), "Visual review refers to another batch")
  if args.source_gpu_audit then
    local gpu = read(args.source_gpu_audit)
    check(truthy(gpu.import_passed) and truthy(gpu.full_clothes_gpu_passed))
    check(truthy(regression.frozen_frame_required) and #regression.frozen_frame_readiness == regression.images)
    check(gpu.pipeline == visual.pipeline)
    check(G.set_eq(G.map(gpu.cards, function(r) return norm_key(r.card) end), G.map(audit.cards, function(r) return norm_key(r.card) end)))
    check(G.all(gpu.cards, function(r) return r.exit == 0 and not truthy(r.cpu_fallback) end))
    check(visual.source_gpu_audit_sha256 == digest(args.source_gpu_audit))
  end
  local files, names = {}, {}
  for _, card in ipairs(audit.cards) do
    for _, key in ipairs(json.keys(card.views)) do
      local v = card.views[key]
      local name = path.name(v.path)
      if files[name] == nil then names[#names + 1] = name end
      files[name] = v
    end
  end
  check(#names == audit.screenshot_count and audit.screenshot_count == regression.images)
  local reg_map, file_map = {}, {}
  for _, r in ipairs(regression.files) do reg_map[r.name] = r.sha256 end
  for _, name in ipairs(names) do file_map[name] = files[name].sha256 end
  check(same_map(reg_map, file_map), "Numeric audit refers to another capture")
  local old, old_names = {}, {}
  for _, r in ipairs(baseline.files) do
    if old[r.name] == nil then old_names[#old_names + 1] = r.name end
    old[r.name] = r.official_sha256
  end
  check(G.set_eq(old_names, G.glob_names(args.official, "*.png")), "Official batch changed since baseline")
  check(G.all(old_names, function(n) return files[n] ~= nil end), "New batch must retain all previous control cards")
  for _, name in ipairs(old_names) do check(digest(path.join(args.official, name)) == old[name], "('" .. name .. "', 'baseline mismatch')") end
  for _, name in ipairs(names) do check(digest(files[name].path) == files[name].sha256, "('" .. name .. "', 'staging mismatch')") end
  -- Archive is deliberately a fresh child of official/old. Copy, never move
  -- or recursively remove a previous batch.
  local official, archive = path.abs(args.official), path.abs(args.archive)
  local old_dir = path.join(official, "old")
  check(path.is_relative_to(archive, old_dir) and archive:lower() ~= old_dir:lower())
  if path.exists(archive) then error("FileExistsError: [WinError 183] Cannot create a file when that file already exists: '" .. G.winpath(archive) .. "'") end
  path.mkdir(archive, true)
  for _, name in ipairs(old_names) do path.copy(path.join(official, name), path.join(archive, name)) end
  for _, name in ipairs(old_names) do check(digest(path.join(archive, name)) == old[name], "('" .. name .. "', 'archive mismatch')") end
  for _, name in ipairs(names) do path.copy(files[name].path, path.join(official, name)) end
  local records = {}
  for _, name in ipairs(util.sorted(names)) do
    local row = files[name]
    local current = digest(path.join(official, name)); check(current == row.sha256)
    records[#records + 1] = O("name", name, "staging_sha256", row.sha256, "official_sha256", current,
      "baseline_sha256", old[name] or json.null,
      "archive_sha256", old[name] ~= nil and digest(path.join(archive, name)) or json.null)
  end
  check(G.set_eq(names, G.glob_names(official, "*.png")))
  G.write_json(args.output, O("result", "pass", "cards", audit.card_count, "images", #names,
    "official", G.winpath(official), "archive", G.winpath(archive), "archive_images", #old_names,
    "archive_verified_before_publish", true, "audit_sha256", digest(args.audit),
    "visual_sha256", digest(args.visual), "files", A(records)))
  print(("PASS published=%d archived=%d"):format(#names, #old_names))
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
