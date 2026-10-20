-- Stage286: source shader identities, native three-stream controls and UE captures.
local ok = require "ok"
local G = require "gpp"
local json, util = ok.json, ok.util
local O, F, I, check, findall = G.O, G.F, G.I, G.check, G.findall
local EC = require "verify_eye_clamp"
local ROOT, LOGS, CARDS, read = EC.ROOT, EC.LOGS, EC.CARDS, EC.read

local digest = G.digest

local function main()
  local native = ROOT .. "/Saved/Tests/EyeShader286"
  local source = ROOT .. "/Saved/Tests/MouthMaterial278"
  local fixtures = json.object()
  local transforms = 0
  for _, card in ipairs(CARDS) do
    local stem = G.sanitize(card)
    local completion = native .. "/" .. stem .. "EyeShader286.json"
    check(json.decode(read(completion))["error"] == "")
    local tbl = completion .. ".eye-shader.tsv"
    local rows = {}
    for _, line in ipairs(util.splitlines(read(tbl))) do rows[#rows + 1] = util.split(line, "\t") end
    local shaders, values = {}, {}
    for _, r in ipairs(rows) do if r[1] == "shader" then shaders[r[2]] = r[3] end end
    for _, v in pairs(shaders) do values[#values + 1] = v end
    check(#values == 2)
    check(G.all(values, function(v) return v == "Koikano/main_eye" or v == "Shader Forge/toon_eye_lod0" end))
    local st = G.filter(rows, function(r) return r[1] == "st" end)
    check(#st == 150 and G.set_eq(G.map(st, function(r) return r[4] end), { "_MainTex", "_overtex1", "_overtex2" }))
    transforms = transforms + #st
    check(#G.filter(rows, function(r) return r[1] == "uv" end) == 1074)
    local selected = source .. "/" .. stem .. "_png_selected_head.unity3d"
    local material
    for _, o in ipairs(ok.kit.unity_objects(selected, { type = "Material" })) do
      if o.name == "cf_m_hitomi_00" then material = o.tree; break end
    end
    if material == nil then error("StopIteration") end
    local shader = ok.kit.unity_objects(selected, { path_id = material.m_Shader.m_PathID })[1].tree
    local runtime_name = shader.m_ParsedForm.m_Name
    check(G.set_eq(values, { runtime_name }))
    fixtures[card] = O("shader", runtime_name, "asset_name", shader.m_Name,
      "completion_sha256", digest(completion), "trace_sha256", digest(tbl),
      "selected_head_sha256", digest(selected))
  end

  local rust = read(LOGS .. "/EyeShaderRust286TexturesAccepted.log")
  check(G.has(rust, "1 passed; 0 failed"))
  check(G.has(rust, "EYE_DRAW_NATIVE cards=5 eyes=10 transforms=750 raw_uv_vertices=5370"))
  local texture_errors = G.floats(findall([[mean_error_255=([\d.eE+-]+)]], rust))
  check(#texture_errors == 20 and G.max(texture_errors) < 2)
  local cache = read(LOGS .. "/EyeShaderCacheRust286.log")
  check(G.has(cache, "1 passed; 0 failed"))
  check(G.has(read(LOGS .. "/EyeShaderUnrealBuild286FinalAccepted.log"), "Result: Succeeded"))
  local tests = read(LOGS .. "/EyeShaderUnrealAutomation286Console.log")
  for _, name in ipairs({ "HandShapes", "ImportedData", "Playback", "NativeWeights", "SharedDeformation",
    "NativeFaceControls", "NativeFaceTime", "NativeEyeGaze", "EyeLookPlayback" }) do
    check(G.has(tests, "Result={Success} Name={" .. name .. "}"))
  end
  check(G.has(tests, "Automation Test Queue Empty 9 tests performed"))
  check(not G.has(tests, "Result={Fail}") and G.has(tests, "RequestExitWithStatus(1, 0"))

  local captures = json.object()
  for _, pose in ipairs({ "Walk", "Tpose" }) do
    local log = read(LOGS .. "/EyeShaderShots286_" .. pose .. "_open.log")
    check(G.count(log, "Tracing Screenshot") == 15)
    check(G.has(log, "RequestExitWithStatus(0, 0,"))
    check(not G.has(log, "Failed to compile Material") and not G.has(log, "LogMaterial: Error"))
    check(not G.has(log, "LogTemp: Error:"))
    if pose == "Walk" then
      check(G.count(log, "ok.ShotPreview imported Studio row=") == 5)
    else
      check(not G.has(log, "ok.ShotPreview imported Studio row="))
    end
    local highlights = findall([[OK eye highlight display audit passed=(\d) changed_uv1=(\d+) changed_uv2=(\d+) restore_all_uv=([\d.]+)]], log)
    check(#highlights == 5 and G.all(highlights, function(h) return h[1] == "1" and I(h[2]) == 358 and I(h[3]) == 358 and F(h[4]) == 0 end))
    check(G.count(log, "OK gaze display audit passed=1") == 5)
    check(G.count(log, "OK eye look shot states=6") == 5)
    local attachments = G.floats(findall([[max_vertex_error_cm=([\d.]+)]], log))
    check(#attachments == 71 and G.max(attachments) <= 1e-6)
    local fits = findall([[capture fit view=(\w+) points=(\d+) distance=([\d.]+) ndc_x=([\d.]+) ndc_y=([\d.]+)]], log)
    check(#fits == 15 and G.all(fits, function(f) return F(f[4]) <= .92001 and F(f[5]) <= .92001 end))
    local folder = ROOT .. "/Saved/DebugShots/EyeShader286_" .. pose .. "_open"
    local expected = {}
    for _, card in ipairs(CARDS) do for _, view in ipairs({ "3d", "face", "face_side" }) do expected[#expected + 1] = card .. "_outfit0_" .. view .. ".png" end end
    check(G.set_eq(G.glob_names(folder, "*.png"), expected))
    check(G.all(expected, function(p) return G.size_is(folder .. "/" .. p, 900, 1080) end))
    local sheet = G.sheet(900, 1920, { 0x30, 0x30, 0x30 })
    for row, card in ipairs(CARDS) do
      sheet:text(8, (row - 1) * 384 + 6, card .. " / " .. pose)
      for col, view in ipairs({ "3d", "face", "face_side" }) do
        sheet:paste(folder .. "/" .. card .. "_outfit0_" .. view .. ".png", (col - 1) * 300, (row - 1) * 384 + 24, 300, 360)
      end
    end
    local contact = native .. "/ContactSheet_" .. pose .. ".png"
    sheet:save(contact)
    captures[pose] = O("images", 15, "attachment_audits", 71, "attachment_max_cm", G.max(attachments),
      "projection_fits", 15, "uv_stream_restore_error", 0,
      "contact_sheet", G.winpath(contact), "sha256", G.sorted_object(expected, function(p) return digest(folder .. "/" .. p) end))
  end

  check(not G.exists(ok.game_root(true) .. "/BepInEx/plugins/OpenKoikatsu.GamePoseProbe.dll"))
  local production = { "Tools/ok-extract/src/eye_gaze.rs", "Tools/ok-extract/src/lib.rs",
    "Tools/ok-extract/src/session.rs", "Tools/ok-extract/src/ffi.rs",
    "Tools/ok-extract/src/card.rs", "Tools/ok-extract/src/unity.rs",
    "Tools/ok-extract/src/sqlcache.rs", "Tools/ok-extract/include/ok_extract.h",
    "Plugins/KKUnityBundle/Source/KKUnityBundle/Private/KKPreviewActor.cpp" }
  local report = O("pipeline", 140, "decoded_cache", 90, "native_transforms", transforms,
    "native_raw_uv_vertices", 5370, "native_eyes", 10, "native_textures", 20,
    "texture_mean_error_max_255", G.max(texture_errors), "unreal_tests", 9,
    "sources", fixtures, "captures", captures,
    "source_sha256", G.digests(ROOT, production),
    "limitations", "HighLight draw/rotation/ST scope; expression, overtex3, full lighting, iris create shader and GPU head remain separate. Chin deferred.")
  G.write_json(LOGS .. "/EyeShader286Audit.json", report)
  print("PASS: five cards, two shader families, 750 transforms, 5370 raw UV vertices, 20 source textures, 9 UE tests, 30 images")
end

if ok.is_main() then ok.main(main) end
