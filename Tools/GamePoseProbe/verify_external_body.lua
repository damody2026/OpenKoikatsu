-- Stage288 source Animator parity and external Unreal animation display gates.
local ok = require "ok"
local G = require "gpp"
local json, util = ok.json, ok.util
local O, F, I, check, findall = G.O, G.F, G.I, G.check, G.findall
local EC = require "verify_eye_clamp"
local ROOT, LOGS, CARDS, read = EC.ROOT, EC.LOGS, EC.CARDS, EC.read

local digest = G.digest

local function main()
  local native = read(LOGS .. "/ExternalBodyRust288Parity.log")
  check(G.has(native, "1 passed; 0 failed"))
  local parity = findall([[ExternalAnimator card=(.+?) frames=180 position_max_m=([\d.]+) rotation_max=([\d.]+) attachment_max_m=([\d.]+)]], native)
  check(#parity == 5 and G.set_eq(G.map(parity, function(r) return r[1] end), CARDS))
  check(G.all(parity, function(r) return F(r[2]) == F(r[3]) and F(r[3]) == F(r[4]) and F(r[4]) == 0 end))
  check(G.has(native, "frames=900 bone_samples=539100 attachment_vertices=19528380 rejected=30"))
  local tests = read(LOGS .. "/ExternalBodyUnrealAutomation288Console.log")
  local final = read(LOGS .. "/ExternalBodyFinalUnrealAutomation288Console.log")
  check(not G.has(tests .. final, "Result={Fail}"))
  local main_test_count = G.count(tests, "Test Completed. Result={Success}")
  check(main_test_count == 11 or main_test_count == 12) -- Historical main run preceded SparseTracks.
  check(G.has(tests, "Automation Test Queue Empty " .. main_test_count .. " tests performed"))
  check(G.count(final, "Test Completed. Result={Success}") == 2)
  check(G.has(final, "Result={Success} Name={ExternalBodyDynamics}"))
  check(G.has(final, "Result={Success} Name={ExternalSparseTracks}"))
  check(G.has(final, "Automation Test Queue Empty 2 tests performed"))
  check(G.has(final, "ExternalSparse tracks=20 preserved_missing=1 coordinate_roundtrips=2"))
  local input_test = read(LOGS .. "/ExternalBodySparseUnrealAutomation288Console.log")
  check(not G.has(input_test, "Result={Fail}"))
  check(G.has(input_test, "Result={Success} Name={ExternalSparseTracks}"))
  check(G.has(input_test, "Automation Test Queue Empty 1 tests performed"))
  check(G.has(input_test, "coordinate_roundtrips=2 invalid_clip_rejected=1"))
  local positive = findall([[ExternalBody card=(.+?) frames=180 changed=(\d+) max_delta_cm=([\d.]+) max_rotation_rad=([\d.]+) hip_motion_cm=([\d.]+)]], final)
  check(#positive == 5 and G.set_eq(G.map(positive, function(r) return r[1] end), CARDS))
  check(G.all(positive, function(r) return I(r[2]) > 0 and F(r[3]) > 1e-3 and F(r[4]) > 1e-4 and F(r[5]) > .1 end))
  check(G.has(final, "tracks=21 bones=21 quarter_hip_z=94.000000000"))
  for _, name in ipairs({ "ExternalBodyUnrealBuild288Final.log", "ExternalBodyUnrealBuild288SparseAccepted.log",
    "ExternalBodyUnrealBuild288ReviewAccepted.log", "ExternalBodyUnrealBuild288InputAccepted.log" }) do
    check(G.has(read(LOGS .. "/" .. name), "Result: Succeeded"))
  end

  local log = read(LOGS .. "/ExternalBodyShots288_open.log")
  check(G.has(log, "RequestExitWithStatus(0, 0,") and not G.has(log, "LogTemp: Error:"))
  check(not G.has(log, "Failed to compile Material"))
  local audits = findall([[OK external dynamics display audit passed=(\d) frames=180 joints=(\d+) followers=(\d+) samples=(\d+) motion_cm=([\d.]+) palette_error_cm=([\d.]+)]], log)
  check(#audits == 5)
  check(G.all(audits, function(a) return a[1] == "1" and a[2] == "6" and I(a[3]) > 0 and I(a[4]) == 180 * 6 * I(a[3]) and F(a[5]) > 1e-4 and F(a[6]) == 0 end))
  check(G.count(log, "quarter_hip_z=94.000000000") == 5)
  check(#findall([[OK external animation sequence=ExternalBody288Fixture\w* duration=2.000000 looping=1 source_physics=1]], log) == 5)
  check(G.count(log, "Tracing Screenshot") == 15)
  check(G.has(log:gsub("\\", "/"), ROOT .. "/Saved/DebugShots/ExternalBody288_open"))
  local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], log))
  check(#attachments == 71 and G.max(attachments) == 0)
  local fits = findall([[capture fit view=\w+ .*?ndc_x=([\d.]+) ndc_y=([\d.]+) limit=0.92]], log)
  check(#fits == 15 and G.all(fits, function(f) return math.max(F(f[1]), F(f[2])) <= .920001 end))
  check(G.count(log, "OK gaze display audit passed=1") == 5)
  check(G.count(log, "OK eye highlight display audit passed=1") == 5)

  local root = ROOT .. "/Saved/Tests/ExternalBody288"
  local traces = json.object()
  for _, card in ipairs(CARDS) do
    local p = root .. "/" .. card .. ".display.tsv"
    local rows = ok.csv.parse_dicts(ok.path.read_text(p), { delimiter = "\t" })
    check(#rows == 1080 and G.set_eq(G.map(rows, function(r) return I(r.frame) end), util.range(180)))
    local names = {}
    for j = 1, 3 do for _, side in ipairs({ "L", "R" }) do names[#names + 1] = string.format("cf_j_bust%02d_%s", j, side) end end
    check(G.set_eq(G.map(rows, function(r) return r.bone end), names))
    check(G.all(rows, function(r)
      for _, k in ipairs({ "phase", "x_cm", "y_cm", "z_cm", "qx", "qy", "qz", "qw" }) do if not G.isfinite(F(r[k])) then return false end end
      return true
    end))
    -- max over the set `names` (iteration order does not change the maximum).
    local variation
    for _, bone in ipairs(names) do
      for _, k in ipairs({ "x_cm", "y_cm", "z_cm" }) do
        local vals = {}
        for _, r in ipairs(rows) do if r.bone == bone then vals[#vals + 1] = F(r[k]) end end
        local v = G.max(vals) - G.min(vals)
        if variation == nil or v > variation then variation = v end
      end
    end
    check(variation > 1e-4)
    traces[card] = O("rows", 1080, "variation_cm", variation, "sha256", digest(p))
  end
  local shots = ROOT .. "/Saved/DebugShots/ExternalBody288_open"
  local expected = {}
  for _, card in ipairs(CARDS) do for _, view in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = card .. "_outfit0_" .. view .. ".png" end end
  check(G.set_eq(G.glob_names(shots, "*.png"), expected))
  check(G.all(expected, function(name) return G.size_is(shots .. "/" .. name, 900, 1080) end))
  local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
  for row, card in ipairs(CARDS) do
    sheet:text(8, (row - 1) * 384 + 6, card .. " / external UE fixture + source physics")
    for col, view in ipairs({ "3d", "face", "face_side" }) do
      sheet:paste(shots .. "/" .. card .. "_outfit0_" .. view .. ".png", (col - 1) * 300, (row - 1) * 384 + 24, 300, 360)
    end
  end
  sheet:save(root .. "/ContactSheet.png")
  local sources = { "Tools/ok-extract/src/session.rs", "Tools/ok-extract/src/session_external_tests.rs", "Tools/ok-extract/src/ffi.rs", "Tools/ok-extract/include/ok_extract.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKRustExtract.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKSkeletalBuilder.cpp", "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKExternalBodyFixture.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Tests/KKExternalBody.spec.cpp",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKPreviewActor.h", "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKRustExtract.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Public/KKSkeletalBuilder.h",
    "old_ps1/Tools/GamePoseProbe/run_external_body.ps1", "Tools/GamePoseProbe/verify_external_body.lua" }
  local report = O("cards", 5, "native_frames", 900, "native_bone_samples", 539100, "native_attachment_vertices", 19528380,
    "native_max_error", 0, "invalid_frames_rejected", 30, "unreal_tests", 12,
    "test_runs", main_test_count .. " regression tests on main build; 2 external dynamics/sparse tests rerun on review build; sparse test extended for invalid-track preflight and rerun on input-guard build, with fresh RHI capture (12 distinct tests total).",
    "physics_negative_control_frames", 900, "display_frames", 900, "display_samples", G.sum(audits, function(a) return I(a[4]) end),
    "palette_error_cm", 0, "negative_controls", G.rows(positive), "display_audits", G.rows(audits), "traces", traces, "attachments", 71, "images", 15,
    "image_sha256", G.sorted_object(expected, function(n) return digest(shots .. "/" .. n) end), "source_sha256", G.digests(ROOT, sources),
    "log_sha256", G.digests(LOGS, { "ExternalBodyRust288Parity.log", "ExternalBodyUnrealAutomation288Console.log",
      "ExternalBodyFinalUnrealAutomation288Console.log", "ExternalBodySparseUnrealAutomation288Console.log",
      "ExternalBodyUnrealBuild288ReviewAccepted.log", "ExternalBodyUnrealBuild288InputAccepted.log", "ExternalBodyShots288_open.log" }),
    "source_control_recovery", "BodyDynamics287 source-walk images were regenerated after an output-path overwrite. Original audit/log/contact sheet preserved; regenerated image hashes differ and are verified separately.",
    "limitations", "Humanoid/KKS retarget and external body-local pose bridge. Diagnostic UE fixture is not game vertex equivalence; arbitrary physics plugins, cross-character collisions and full Animator graph remain separate; chin deferred.")
  G.write_json(LOGS .. "/ExternalBody288Audit.json", report)
  print("PASS: five cards; 900 native parity frames, 539100 transforms, 19528380 attachment vertices, 30 invalid frames; 12 UE tests; 900 displayed frames and 15 images")
end

if ok.is_main() then ok.main(main) end
