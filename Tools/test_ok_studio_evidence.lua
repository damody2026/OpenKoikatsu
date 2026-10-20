-- Reject stale, missing, corrupt or ambiguously identified Studio evidence.
-- Tools/lua/lua.exe Tools/test_ok_studio_evidence.lua
local ok = require "ok"
local evidence = require "ok_studio_evidence"
local path = ok.path
local root = path.join(ok.root, "Saved/Tests/StudioEvidence", os.date("%Y%m%d-%H%M%S") .. ("-%08x"):format(math.random(0, 0x7fffffff)))
local original = path.read_bytes(path.join(ok.root, "Docs/images/OKStudio/HandoffE20261020/koikatu_cs0000010.png"))
path.mkdir(path.join(root, "images"), true)
path.write_bytes(path.join(root, "images/one.png"), original)
path.write_bytes(path.join(root, "images/two.png"), original)
local one = "OK studio compare: one.png pictured  (memory 4.8 GB)\n"
local two = "OK studio compare: two.png pictured  (memory 5.1 GB)\n"
local done = "OK studio compare: RESULT 2 / 2 pictured into fixture\n"
local passed = 0
local function check(value, label)
  assert(value, label)
  passed = passed + 1
  print("PASS " .. label)
end
local function rejected(log, fragment)
  local _, errors = evidence.collect(root, log, 2)
  return table.concat(errors, "\n"):find(fragment, 1, true) ~= nil
end
local images, errors = evidence.collect(root, one .. two .. done, 2)
check(#images == 2 and #errors == 0 and images[1].sha256 == ok.hash.sha256(original), "valid run owns decodable images and matching hash")
local upper_images, upper_errors = evidence.collect(root, one:gsub("one.png", "one.PNG", 1) .. two .. done, 2)
check(#upper_errors == 0 and upper_images[1].file == "images/one.png", "input uppercase extension maps to actual lowercase PNG output")
check(rejected(one .. two, "RESULT"), "missing success marker rejected")
check(rejected(one .. one .. done, "duplicate"), "duplicate scene rejected")
check(rejected(one .. done, "validated 1 / 2"), "result marker cannot replace missing picture records")
check(rejected(one .. two:gsub("two.png", "../two.png", 1) .. done, "unsafe"), "path traversal rejected")
path.rename(path.join(root, "images/two.png"), path.join(root, "two.png"))
check(rejected(one .. two .. done, "two.png:"), "image outside run images directory cannot substitute for missing output")
path.write_bytes(path.join(root, "images/two.png"), original:sub(1, 60))
check(rejected(one .. two .. done, "truncated"), "truncated PNG rejected")
path.write_bytes(path.join(root, "images/two.png"), original .. "KoiKatuChara")
check(rejected(one .. two .. done, "trailing payload"), "appended private payload rejected")
local small = ok.image.new(2, 2, "RGB", { 255, 0, 0 })
small:save(path.join(root, "images/two.png"))
check(rejected(one .. two .. done, "1280x360"), "unexpected comparison layout rejected")
path.write_bytes(path.join(root, "images/two.png"), original)
evidence.write(root, { tag = "fixture", revision = "fixture", finished = true, passed = true,
  tests = { { name = "CompareKStudio", passed = true, log = "fixture.log", images = images } } })
local md = path.read_text(path.join(root, "ACCEPTANCE.md"))
check(md:find("(images/one.png)", 1, true) and md:find("(images/two.png)", 1, true)
  and not md:find(ok.root, 1, true), "Markdown embeds run-relative images without machine-specific paths")
local international = "日本語 場景 (1)#%.png"
path.write_bytes(path.join(root, "images", international), original)
local intl_images, intl_errors = evidence.collect(root, "OK studio compare: " .. international .. " pictured  (memory 4.8 GB)\n"
  .. "OK studio compare: RESULT 1 / 1 pictured into fixture\n", 1)
check(#intl_images == 1 and #intl_errors == 0, "Unicode and spaces in source scene names accepted")
evidence.write(root, { tag = "fixture", revision = "fixture", finished = true, passed = true,
  tests = { { name = "CompareKStudio", passed = true, log = "fixture.log", images = intl_images } } })
md = path.read_text(path.join(root, "ACCEPTANCE.md"))
check(md:find("%20", 1, true) and md:find("%28", 1, true) and md:find("%23%25.png", 1, true),
  "Markdown links percent-encode Unicode, spaces, parentheses, fragment and percent characters")
local fixture_dir = path.join(root, "images/TransparencyTest")
path.mkdir(fixture_dir, true)
local pixel = ok.image.new(64, 64, "RGB", { 100, 100, 100 })
local names = { "background-low", "background-high", "opaque-control", "alpha-control-low", "alpha-control-high" }
for i = 0, 47 do for _, bg in ipairs({ "low", "high" }) do names[#names + 1] = ("%02d-%s"):format(i, bg) end end
for _, name in ipairs(names) do pixel:save(path.join(fixture_dir, name .. ".png")) end
local probes, probe_errors = evidence.collect_transparency(root)
check(#probes == 101 and #probe_errors == 0, "all 101 transparency screenshots validated")
evidence.write(root, { tag = "fixture", revision = "fixture", finished = true, passed = true,
  tests = { { name = "TransparencyTest", passed = true, log = "fixture.log", images = probes } } })
md = path.read_text(path.join(root, "ACCEPTANCE.md"))
local _, embeds = md:gsub("!%[", "")
check(embeds == 13 and md:find("透明度 GPU 測試", 1, true), "transparency report embeds 13 labeled representative images")
local altered = path.join(fixture_dir, "47-high.png")
path.write_bytes(altered, path.read_bytes(altered) .. "KoiKatuChara")
local _, invalid_probes = evidence.collect_transparency(root)
check(#invalid_probes == 1 and invalid_probes[1]:find("trailing payload", 1, true), "unembedded transparency image payload is rejected too")
path.write_text(path.join(root, "fixture-results.json"), ok.json.encode({ passed = passed, fixture = root }, { indent = 2 }) .. "\n")
print(("%d PASS; %s"):format(passed, root))
