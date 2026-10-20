-- Bounded acceptance of component/RT fixes; raw native mismatch remains visible.
local ok = require "ok"
local json, P, U = ok.json, ok.path, ok.util
local O = json.object

local ROOT = P.parent(ok.script_dir)
local OUT = P.join(ROOT, "Saved/Tests/ClothesComponent341")
P.mkdir(OUT)

local function win(p) return (P.norm(p):gsub("/", "\\")) end
local function sha(p) return ok.hash.file(p, "sha256") end
local function read(p) return json.decode(P.read_text(p)) end
local function nullable(v) if v == nil then return json.null end; return v end

local function log_pass(name, token)
  local p = P.join(ROOT, "Saved/Logs", name)
  assert(P.read_bytes(p):find(token, 1, true), name)
  return O({ { "path", win(p) }, { "sha256", sha(p) } })
end

-- numpy std of the RGB pixels (uint8 array), computed by ok-luakit with numpy's rounding.
local function rgb_std(p)
  local img = ok.image.load(p, "RGB")
  local raw = P.temp(".raw"); P.write_bytes(raw, img:bytes())
  local s = ok.kit.call({ "u8-stats", raw }); P.remove(raw, true)
  return img.width, img.height, s.std + 0.0
end

local runtime = P.join(ROOT, "Saved/Tests/PhoebeComponent341FinalRuntime")
local baseline = P.join(ROOT, "Saved/Tests/PhoebeOpaque340Runtime")
local comparisons = json.array({})
for _, outfit in ipairs({ 0, 1, 2, 3, 5, 6 }) do
  local p = P.join(runtime, ("outfit%d/runtime.json"):format(outfit))
  local old = P.join(baseline, ("outfit%d/runtime.json"):format(outfit))
  local current, previous = read(p), read(old)
  local a, b = current.parts, previous.parts
  assert(#a == #b, "AssertionError")
  for i = 1, #a do assert(a[i].role == b[i].role and a[i].name == b[i].name, "AssertionError") end
  local changed = json.array({})
  for i = 1, #a do
    local now, before = a[i], b[i]
    -- All exported texture files, including shader extras, are independent
    -- byte artifacts. Report changes; matching this baseline is not native parity.
    local keys = {}
    for _, k in ipairs(json.keys(now.textures)) do keys[k] = true end
    for _, k in ipairs(json.keys(before.textures)) do keys[k] = true end
    for _, key in ipairs(U.sorted_keys(keys)) do
      local x, y = now.textures[key], before.textures[key]
      if x == nil or y == nil or sha(P.join(P.parent(p), x)) ~= sha(P.join(P.parent(old), y)) then
        changed[#changed + 1] = O({ { "part", now.index }, { "name", now.name }, { "texture", key } })
      end
    end
  end
  comparisons[#comparisons + 1] = O({ { "outfit", outfit }, { "parts", #a }, { "sha256", sha(p) }, { "changed_textures", changed } })
end

local effective = read(P.join(runtime, "EffectiveInputs.json"))
local raw = read(P.join(runtime, "DumpAudit.json"))
assert(U.truthy(effective.passed) and #effective.rows == 120, "AssertionError")
local capture_path = P.join(ROOT, "Saved/Logs/ClothesComponent341FinalCaptures.json")
local captures = read(capture_path)
if json.is_object(captures) then captures = { captures } end
local images = json.array({})
for _, capture in ipairs(captures) do
  assert(capture.exit == 0 and #capture.images == 16, "AssertionError")
  for _, item in ipairs(capture.images) do
    local p = item.path
    assert(sha(p) == item.sha256, "AssertionError")
    local w, h, std = rgb_std(p)
    assert(h == 1080 and w == 900 and std > 10, "AssertionError")
    images[#images + 1] = O({ { "path", win(p) }, { "sha256", sha(p) }, { "std", std } })
  end
end
local logs = json.array({ log_pass("ClothesComponent341FinalTestsR2.log", "22 passed; 0 failed"),
  log_pass("ClothesComponent341TextureTests.log", "4 passed; 0 failed"),
  log_pass("ClothesComponent341Prefabs.log", "1 passed; 0 failed"),
  log_pass("ClothesComponent341FinalUEBuild.log", "Result: Succeeded"),
  log_pass("PhoebeComponent341FinalExportR2.log", "test result: ok.") })
local sources = {}
for _, n in ipairs({ "lib.rs", "unity.rs", "sqlcache.rs", "dump_outfit_audit.rs" }) do sources[#sources + 1] = P.join(ROOT, "Tools/ok-extract/src", n) end
sources[#sources + 1] = P.join(ROOT, "Plugins/KKUnityBundle/Binaries/Win64/UnrealEditor-KKUnityBundle.dll")
sources[#sources + 1] = P.join(ROOT, "Tools/ok-extract/target/release/ok_extract.lib")
local source_rows = json.array({})
for _, p in ipairs(sources) do source_rows[#source_rows + 1] = O({ { "path", win(p) }, { "sha256", sha(p) } }) end
local report = O({ { "passed", true },
  { "scope", "component association, RT assignment/overlay controls, six outfit effective inputs and four-card RHI captures; excludes full-scene/native-pixel parity" },
  { "pipeline", 221 }, { "comparisons", comparisons }, { "effective_checks", #effective.rows },
  { "raw_summary", nullable(raw.summary) }, { "all_dumps_match", nullable(raw.all_dumps_match) },
  { "images", images }, { "logs", logs }, { "sources", source_rows } })
P.write_text(P.join(OUT, "Acceptance.json"), json.encode(report, { ensure_ascii = false, indent = 2 }))
local parts, changed = 0, 0
for _, x in ipairs(comparisons) do parts = parts + x.parts; changed = changed + #x.changed_textures end
print(json.encode(O({ { "passed", true }, { "parts", parts }, { "changed_textures", changed },
  { "effective_checks", 120 }, { "images", #images }, { "all_dumps_match", report.all_dumps_match } }), { ensure_ascii = false }))
