-- Audit a complete UE capture log and compare every PNG to a prior batch.
-- This numeric report deliberately leaves visual acceptance to a separate review.
local ok = require "ok"
local G = require "gpp"
local json, path, re = ok.json, ok.path, ok.re
local O, A, F, I, check = G.O, G.A, G.F, G.I, G.check

local M = {}

local DOC = "Audit a complete UE capture log and compare every PNG to a prior batch.\n" ..
  "This numeric report deliberately leaves visual acceptance to a separate review."

-- A completion fence alone cannot accept an empty viewport.
function M.image_content_presence(png)
  local r = ok.kit.call({ "probe", "content-presence", png })
  local ratio, center = r.foreground_fraction + 0.0, r.center_foreground_fraction + 0.0
  return O("foreground_fraction", ratio, "center_foreground_fraction", center,
    "passed", ratio >= 0.01 and center >= 0.01)
end

-- bytes.decode('utf-8', errors='replace'): invalid bytes become U+FFFD.
local function decode_replace(s)
  if utf8.len(s) then return s end
  local out, i, n = {}, 1, #s
  while i <= n do
    local len = utf8.len(s, i, i) and #utf8.char(utf8.codepoint(s, i)) or nil
    if len and utf8.len(s:sub(i, i + len - 1)) then
      out[#out + 1] = s:sub(i, i + len - 1); i = i + len
    else
      out[#out + 1] = "\239\191\189"; i = i + 1
    end
  end
  return table.concat(out)
end
M.decode_replace = decode_replace

-- str.splitlines() after universal newlines (\n, \v, \f, \x1c-\x1e, U+0085, U+2028/9).
local function splitlines(s)
  local t, start, i, n = {}, 1, 1, #s
  while i <= n do
    local b = s:byte(i)
    local w = 0
    if b == 10 or b == 11 or b == 12 or b == 28 or b == 29 or b == 30 or b == 13 then w = 1
    elseif b == 0xC2 and s:byte(i + 1) == 0x85 then w = 2
    elseif b == 0xE2 and s:byte(i + 1) == 0x80 and (s:byte(i + 2) == 0xA8 or s:byte(i + 2) == 0xA9) then w = 3 end
    if w > 0 then
      t[#t + 1] = s:sub(start, i - 1)
      if b == 13 and s:byte(i + 1) == 10 then w = 2 end
      i = i + w; start = i
    else i = i + 1 end
  end
  if start <= n then t[#t + 1] = s:sub(start) end
  return t
end
M.splitlines = splitlines

local function has(s, sub) return s:find(sub, 1, true) ~= nil end
local function pyname(p) return path.name((p:gsub("^%s+", ""):gsub("%s+$", ""))) end

local function main()
  local p = ok.args.parser(DOC)
  for _, a in ipairs({ "inventory", "capture", "prior", "log", "output" }) do p:arg(a) end
  p:flag("--allow-new-cards", { help = "Report newly added cards without inventing prior PNG comparisons" })
  p:flag("--require-material-readiness", { help = "Require every task to finish shader compilation before capture" })
  p:flag("--require-gpu-readiness", { help = "Require GPU resource completion before every task starts camera timers" })
  p:flag("--require-view-gpu-readiness", { help = "Require GPU completion for each individual screenshot path" })
  p:flag("--require-shadow-readiness", { help = "Require a frozen-pose shadow refresh with unchanged clocks before every image" })
  p:flag("--require-frozen-frame", { help = "Require Actor Tick disabled at each individual screenshot" })
  p:flag("--require-camera-readiness", { help = "Require a settled camera cache for every screenshot" })
  local args = p:parse(arg)
  local inventory = json.decode(G.read(args.inventory))
  local log = decode_replace(path.read_text(args.log))
  local lines = splitlines(log)
  local pv = re.findall([[OK Studio PV display audit passed=(\d).*?palette_error_cm=([\d.eE+-]+)]], log)
  local attachments = re.findall([[OK attachment display part=.*?max_vertex_error_cm=([\d.eE+-]+)]], log)
  local tasks = re.findall([[ok.ShotPreview task (\d+)/(\d+) card=(.*?) variant=]], log)
  local errors = G.filter(lines, function(l) return has(l, "Error:") end)
  local commonui = G.filter(errors, function(l) return has(l, "LogUIActionRouter: Error: Using CommonUI without") end)
  local built_in = G.filter(errors, function(l) return has(l, "LogAutomationTest: Error:") end)
  local known = {}
  for _, l in ipairs(commonui) do known[l] = true end
  for _, l in ipairs(built_in) do known[l] = true end
  local unexpected = G.filter(errors, function(l) return not known[l] end)
  local shader_failures = G.filter(lines, function(l) return has(l, "Failed to compile Material") or has(l, "LogShaderCompilers: Error:") end)
  local readiness = re.findall([[ok.ShotPreview material readiness pending_before=(\d+) pending_after=(\d+)]], log)
  local gpu_readiness = re.findall([[ok.ShotPreview GPU resource readiness completed=(\d+)]], log)
  local view_gpu_readiness = re.findall([[ok.ShotPreview view GPU readiness completed=1 path=(.*)]], log)
  local shadow_readiness = re.findall([[ok.ShotPreview shadow readiness captured=1 pose_clock_held=(\d+) materials=(\d+)]], log)
  local frame_readiness = re.findall([[ok.ShotPreview frame readiness frozen=1 path=(.*)]], log)
  local camera_readiness = re.findall([[ok.ShotPreview camera readiness frames=(\d+) position_error_cm=([\d.eE+-]+) rotation_error_deg=([\d.eE+-]+) fov=([\d.eE+-]+) path=(.*)]], log)
  local content, records = {}, {}
  local function outfits(item) local o = item.outfits; if o == nil then return { 0 } end; return o end
  local expected_tasks = G.sum(inventory, function(item) return #outfits(item) end)
  for _, item in ipairs(inventory) do
    for _, outfit in ipairs(outfits(item)) do
      for _, suffix in ipairs({ "3d", "3d_side", "3d_back", "face" }) do
        local name = path.stem(item.card) .. "_outfit" .. ok.util.str(outfit) .. "_" .. suffix .. ".png"
        local newp = path.join(args.capture, name)
        local presence = M.image_content_presence(newp)
        content[#content + 1] = O("name", name, "foreground_fraction", presence.foreground_fraction,
          "center_foreground_fraction", presence.center_foreground_fraction, "passed", presence.passed)
        local sha = G.digest(newp)
        local oldp = path.join(args.prior, name)
        if not path.exists(oldp) and args.allow_new_cards then
          check(G.size_is(newp, 900, 1080))
          records[#records + 1] = O("name", name, "sha256", sha, "prior_available", false, "mean_rgb_difference_255", json.null)
        else
          local w, h = G.size(oldp)
          local nw, nh = G.size(newp)
          check(nw == w and nh == h and w == 900 and h == 1080)
          local mean = ok.kit.call({ "probe", "mean-abs-diff", newp, oldp }).mean + 0.0
          records[#records + 1] = O("name", name, "sha256", sha, "prior_available", true, "mean_rgb_difference_255", mean)
        end
      end
    end
  end
  local passed = (#pv == #tasks and #tasks == expected_tasks and G.all(pv, function(r) return I(r[1]) == 1 end)
    and #attachments > 0 and G.max(G.floats(attachments)) <= 2e-5
    and G.max(pv, function(r) return F(r[2]) end) <= 2e-5 and #unexpected == 0 and #shader_failures == 0
    and G.all(content, function(row) return row.passed end))
  local record_names = G.map(records, function(r) return r.name end)
  if args.require_material_readiness then
    passed = passed and #readiness == expected_tasks and G.all(readiness, function(r) return I(r[2]) == 0 end)
  end
  if args.require_gpu_readiness then
    passed = passed and #gpu_readiness == expected_tasks and G.all(gpu_readiness, function(v) return I(v) == 1 end)
  end
  if args.require_view_gpu_readiness then
    local names = G.map(view_gpu_readiness, pyname)
    passed = passed and #names == #records and G.set_eq(names, record_names)
  end
  if args.require_shadow_readiness then
    passed = passed and #shadow_readiness == #records and G.all(shadow_readiness, function(r) return I(r[1]) == 1 and I(r[2]) > 0 end)
  end
  if args.require_frozen_frame then
    local names = G.map(frame_readiness, pyname)
    passed = passed and #names == #records and G.set_eq(names, record_names)
  end
  if args.require_camera_readiness then
    local names = G.map(camera_readiness, function(r) return pyname(r[5]) end)
    passed = passed and #names == #records and G.set_eq(names, record_names) and G.all(camera_readiness, function(r)
      return I(r[1]) >= 2 and F(r[2]) <= .01 and F(r[3]) <= .01 and 0 < F(r[4]) and F(r[4]) < 180 end)
  end
  local attach_max = #attachments > 0 and G.max(G.floats(attachments)) or json.null
  local palette_max = #pv > 0 and G.max(pv, function(r) return F(r[2]) end) or json.null
  local report = O("passed", passed,
    "scope", "numeric capture/display regression and measured PNG differences; visual review required separately",
    "cards", #inventory, "images", #records, "pv_pass", G.sum(pv, function(r) return I(r[1]) end),
    "palette_max_error_cm", palette_max, "attachment_rows", #attachments, "attachment_max_error_cm", attach_max,
    "preexisting_commonui_error", A(commonui), "unreal_builtin_error_test_lines", #built_in,
    "unexpected_errors", A(unexpected), "files", A(records))
  report.shader_compile_failures = A(shader_failures)
  report.capture = G.winpath(path.abs(args.capture))
  report.inventory_sha256 = G.digest(args.inventory)
  report.log_sha256 = G.digest(args.log)
  report.material_readiness_required = args.require_material_readiness
  report.material_readiness = A(G.map(readiness, function(r) return O("pending_before", I(r[1]), "pending_after", I(r[2])) end))
  report.gpu_readiness_required = args.require_gpu_readiness
  report.gpu_readiness = A(G.map(gpu_readiness, function(v) return I(v) end))
  report.view_gpu_readiness_required = args.require_view_gpu_readiness
  report.view_gpu_readiness = A(view_gpu_readiness)
  report.shadow_readiness_required = args.require_shadow_readiness
  report.shadow_readiness = A(G.map(shadow_readiness, function(r) return O("pose_clock_held", I(r[1]), "materials", I(r[2])) end))
  report.frozen_frame_required = args.require_frozen_frame
  report.frozen_frame_readiness = A(frame_readiness)
  report.camera_readiness_required = args.require_camera_readiness
  report.camera_readiness = A(G.map(camera_readiness, function(r)
    return O("frames", I(r[1]), "position_error_cm", F(r[2]), "rotation_error_deg", F(r[3]), "fov", F(r[4]), "path", r[5]) end))
  report.image_content = A(content)
  path.write_text(args.output, json.encode(report, { indent = 2, ensure_ascii = false }) .. "\n")
  local summary = json.object()
  for _, k in ipairs({ "passed", "cards", "images", "pv_pass", "attachment_rows", "attachment_max_error_cm" }) do summary[k] = report[k] end
  print(json.encode(summary))
  if not passed then return 1 end
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
