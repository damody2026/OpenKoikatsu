-- Native APNG CrowdRoom with the StreetSocial look (port of
-- run_crowd_runtime_apng_development.ps1); CrowdRoom100.cmd, CrowdRoom10.cmd.
-- Verifies the StreetSocial build receipt (runtime, common toon materials,
-- display LUT, render quality), the CrowdRoom environment display patch and the
-- selected APNG cards, writes Saved/Crowd/RuntimeApngPlay<stamp>/selection.json
-- and starts the room. --self-test runs it unattended and checks the report,
-- log and CPU/GPU captures.
--   Tools\lua\lua.exe Tools\run_crowd_runtime_apng_development.lua [--low-poly] [--view-light]
--       [--executable <room receipt.json>] [--selection-file <selection.json>] [--self-test]
local ok = require "ok"
local C = require "crowd_launch_common"
local json = ok.json
local T, ne, fail = C.truthy, C.ne, C.fail

local M = {}

M.spec = {
  { "SelectionFile", "string", default = "" },
  { "Executable", "string", default = "" },
  { "LowPoly", "switch" },
  { "ViewLight", "switch" },
  { "LegwearInflateCm", "double", default = 0.5, range = { 0, 5 } },
  { "ValidateOnly", "switch" },
  { "NoSpeechActions", "switch" },
  { "CacheMiB", "int", default = 4096, range = { 0, 65536 }, cli = "--cache-mib" },
  { "CacheDirectory", "string", default = "" },
  { "RenderScale", "int", default = 150, range = { 100, 200 } },
  { "SelfTest", "switch" },
}

local function sha256(p) return C.sha256(p) end
local function same_hash(a, b) return type(a) == "string" and type(b) == "string" and a:lower() == b:lower() end
local function str(v) if C.is_null(v) then return "" end return tostring(v) end

function M.run(input)
  local P = C.bind(M.spec, input, "run_crowd_runtime_apng_development")
  local root = C.root
  -- Resolve the exact same runtime, common toon materials and output LUT as StreetSocial.
  -- The old R1147 binary predates the display profile and cannot apply this look.
  local build = C.read_json(C.join(root, "Saved/StreetSocial/Build/receipt.json"))
  if not T(build.passed) or not T(build.runtime_display_profile) or not T(build.runtime_render_quality) then fail("Verified StreetSocial display and render quality profiles required") end
  local exe = str(build.executable)
  local exe_hash = build.executable_sha256
  -- CrowdRoom100: a room-only Development host staged on the same cooked content.
  -- Its receipt pins the exe; materials, LUT and patches still come from StreetSocial.
  if P.Executable ~= "" then
    local own = C.read_json(P.Executable)
    if not T(own.executable) or not T(own.executable_sha256) then fail("Room executable receipt incomplete") end
    exe = str(own.executable); exe_hash = own.executable_sha256
  end
  local materials = str(build.material_manifest)
  -- The room needs its matching cooked environment shader library.
  local room = C.read_json(C.join(root, "Saved/Crowd/Display-StreetStyle01.json"))
  if not T(room.passed) or not T(room.common_only) or T(room.per_card_assets) or not T(room.patch_file_sha256) then fail("Verified CrowdRoom environment display patch required") end
  local patch = str(room.patch)
  local display = build.runtime_display_profile
  local quality = build.runtime_render_quality
  for _, item in ipairs({
    { exe, exe_hash },
    { materials, build.material_manifest_sha256 },
    { display.lut, display.sha256 },
    { quality.evidence, quality.evidence_sha256 },
  }) do
    local p, h = item[1], item[2]
    if not T(p) or not T(h) or not ok.path.is_file(p) or not same_hash(sha256(p), h) then fail("StreetSocial display input missing or changed: " .. str(p)) end
  end
  if ne(display.intensity, 1) or ne(display.tone_curve, 0) or ne(quality.antialiasing_method, 1) or ne(quality.screen_percentage, 150) then fail("Unexpected StreetSocial display profile") end
  if not T(build.patch_file_sha256) then fail("Verified StreetSocial common shader patch hashes required") end
  local expected, actual = {}, {}
  for _, name in ipairs(C.props(room.patch_file_sha256)) do expected[#expected + 1] = name:lower() end
  for _, name in ipairs(ok.path.list(patch)) do
    if name:lower():match("%.pak$") and ok.path.is_file(C.join(patch, name)) then actual[#actual + 1] = name:lower() end
  end
  table.sort(expected); table.sort(actual)
  if table.concat(expected, "\n") ~= table.concat(actual, "\n") then fail("CrowdRoom display patch set changed") end
  for _, name in ipairs(C.props(room.patch_file_sha256)) do
    if name:find("[\\/]") or not same_hash(sha256(C.join(patch, name)), room.patch_file_sha256[name]) then fail("CrowdRoom display patch identity changed") end
  end
  for _, name in ipairs(C.props(build.patch_file_sha256)) do
    if ok.path.fnmatch(name, "RuntimeMaterialPatch-*") or ok.path.fnmatch(name, "RuntimeAudioPatch-*") then
      if ne(C.prop(room.patch_file_sha256, name), build.patch_file_sha256[name]) then fail("CrowdRoom common materials/audio must match current StreetSocial build") end
    end
  end
  if P.SelectionFile == "" then P.SelectionFile = C.join(root, "Content/CharacterCards/CrowdAPNG/selection.json") end
  local source = C.read_json(P.SelectionFile)
  local cards = {}
  if type(source.cards) == "table" and not json.is_object(source.cards) then
    for _, c in ipairs(source.cards) do cards[#cards + 1] = c end
  elseif not C.is_null(source.cards) then cards[1] = source.cards end
  if #cards < 1 or #cards > 100 then fail("Selection needs 1..100 APNG paths") end
  for _, card in ipairs(cards) do if not ok.path.is_file(card) then fail("Selected APNG missing: " .. str(card)) end end
  -- Low models come from ok-lowpoly / APNG export: <card>.lowpoly.okmap beside each card.
  local low_maps = 0
  for _, card in ipairs(cards) do
    local s = tostring(card)
    local dot = s:match("^.*()%.[^.\\/]*$")
    local map = (dot and s:sub(1, dot - 1) or s) .. ".lowpoly.okmap"
    if ok.path.is_file(map) then low_maps = low_maps + 1 end
  end
  if P.LowPoly then C.out("Low-poly models: " .. low_maps .. "/" .. #cards .. " cards have .lowpoly.okmap; others keep their source mesh.") end
  local material = C.read_json(materials)
  if not T(material.runtime_reveal_supported) or not T(material.passed) or not T(material.source_mesh_shader_permutations_verified) then fail("Common material receipt failed") end
  C.out("Verified native APNG Development GPU room. F9 switches CPU/GPU; WASD moves; hold right mouse to rotate the camera.")
  local actions, voices = {}, {}
  if not P.NoSpeechActions then
    local action_manifest = C.read_json(C.join(root, "Content/IndependentResources/RuntimeAnimations/actions.manifest.json"))
    local voice_manifest = C.read_json(C.join(root, "Content/IndependentResources/RuntimeVoices/manifest.json"))
    for _, action in ipairs(action_manifest.actions or {}) do
      local file = C.join(root, "Content/IndependentResources/RuntimeAnimations/" .. str(action.file))
      if not same_hash(sha256(file), action.sha256) then fail("Common gesture identity changed") end
      actions[#actions + 1] = json.object({ { "curve", file }, { "loop", T(action.loop) } })
    end
    for _, voice in ipairs(voice_manifest.voices or {}) do
      local file = C.join(root, "Content/IndependentResources/RuntimeVoices/" .. str(voice.file))
      if not same_hash(sha256(file), voice.sha256) then fail("Common voice identity changed") end
      voices[#voices + 1] = file
    end
    C.out("1-4 actions; T action+speech; Y nearby NPC action+speech; Backspace resumes behavior. Shared demonstration voice; NPC gestures prepare on demand, controlled actor preloads all four.")
  end
  C.out("Progressive room entry: WASD works during loading; nearby preparation jobs have priority. Each ready NPC uploads its own private GPU bank and appears while the others load. The completed scene then uses the aggregate GPU provider. Upload batches are limited to4 MiB.")
  C.out("StreetSocial look: shared CardReaderStudio LUT, common toon materials, camera-relative character lighting, FXAA and 150% supersampling by default. Characters retain their 3-second GPU fade-in.")
  C.out("Persistent GPU pose disk cache limit: " .. P.CacheMiB .. " MiB; oldest unused entries are evicted. Default: %LOCALAPPDATA%/OpenKoikatsu/RuntimeGpuCache/v1")
  if P.ValidateOnly then C.out(exe); return 0 end
  local output = C.join(root, "Saved/Crowd/RuntimeApngPlay" .. C.stamp("T"))
  if ok.path.exists(output) then fail("An item with the specified name " .. output .. " already exists.") end
  ok.path.mkdir(output, true)
  local manifest = json.object({
    { "cards", json.array(cards) },
    { "curves", json.array({ C.join(root, "Content/IndependentResources/RuntimeAnimations/idle_02.okanim"), C.join(root, "Content/IndependentResources/RuntimeAnimations/walk_01.okanim") }) },
    { "material_root", material.root == nil and json.null or material.root }, { "material_library", "RuntimeMaterialBake" },
    { "report", C.join(output, "scene.json") }, { "exit_after_report", P.SelfTest }, { "profile_seconds", 0 },
    { "display_lut", str(display.lut) }, { "depth_prepass_enabled", true }, { "background_prepare_concurrency", 16 },
    { "diagnostic_load_deadline_seconds", 180 },
  })
  manifest.actions = json.array(actions); manifest.voices = json.array(voices); manifest.npc_action_preload_count = 0
  manifest.auto_action_streaming = true; manifest.progressive_loading = true; manifest.parallel_loading_poses = true
  manifest.loading_cpu_preview = false; manifest.auto_blink = true
  if P.SelfTest then manifest.visual_prefix = C.join(output, "visual"); manifest.diagnostic_load_deadline_seconds = 300 end
  local selection = C.join(output, "selection.json")
  C.write_json(selection, manifest, true)
  local before = ok.fs.getenv("OK_BUILTIN_RESOURCES")
  local okr, err = pcall(function()
    ok.fs.setenv("OK_BUILTIN_RESOURCES", C.join(root, "Content/IndependentResources/v1"))
    local args = '/Game/Scenes/CrowdRoom/CrowdRoom -D3D12 -SM6 -windowed -ResX=1920 -ResY=1080 "-CrowdRuntimeScene=' .. selection .. '" "-pakdir=' .. patch .. '" "-abslog=' .. C.join(output, "scene.log") .. '" -ExecCmds="r.AntiAliasingMethod 1,r.ScreenPercentage ' .. P.RenderScale .. '"'
    args = args .. " -RuntimeGpuCacheMiB=" .. P.CacheMiB
    if P.LowPoly then args = args .. " -RuntimeLowPoly -CrowdLowPolyRules" end
    -- Socks and stockings pushed out along their normal so the leg cannot show through.
    if P.LowPoly and P.LegwearInflateCm > 0 then args = args .. " -CrowdLegwearInflate=" .. C.num(P.LegwearInflateCm) end
    -- Every character material lit from the camera view, not only clothes and skin.
    if P.ViewLight then args = args .. " -CrowdViewLightAll" end
    if P.CacheDirectory ~= "" then
      local cache_path = C.full(P.CacheDirectory)
      if cache_path:find('"', 1, true) then fail("Cache directory must not contain quotes") end
      args = args .. ' "-RuntimeGpuCacheDir=' .. cache_path .. '"'
    end
    if P.SelfTest then args = args .. " -unattended -RenderOffscreen" end
    local game = C.start_process(exe, args, { cwd = root, hide = P.SelfTest })
    C.set_content(C.join(output, "scene.pid"), game:pid())
    C.out(output)
    if P.SelfTest then
      local deadline = C.clock() + 300
      local code
      while true do
        code = game:wait(1)
        if code then break end
        if C.clock() > deadline then game:kill(); fail("Owned CrowdRoom display self-test timed out") end
      end
      local result = C.read_json(C.join(output, "scene.json"))
      if code ~= 0 or not T(result.gpu_ready) or ne(result.gpu_failures, 0) or ne(result.complete, #cards) or not T(result.display_lut_applied) or not T(result.same_phase_captures_written) or T(result.capture_error) or ne(result.scene_skinning_matrix_mismatches, 0) then fail("CrowdRoom runtime/display/capture self-test failed") end
      local log = C.read_text(C.join(output, "scene.log")):lower()
      for _, needle in ipairs({ "missing shader resource", "uncooked shader map", "default material will be used", "invalid shader map id" }) do
        if log:find(needle, 1, true) then fail("CrowdRoom display shader regression") end
      end
      local res = ok.proc.lua(ok.root .. "/Tools/compare_crowd_gpu_images.lua", { C.join(output, "visual") })
      if res.code ~= 0 then fail("CrowdRoom CPU/GPU visual comparison failed") end
      C.out("CrowdRoom StreetSocial display self-test passed.")
    end
  end)
  ok.fs.setenv("OK_BUILTIN_RESOURCES", before or nil)
  if not okr then error(err, 0) end
  return 0
end

function M.main()
  return M.run(C.parse_cli(M.spec, "Native APNG CrowdRoom with the StreetSocial look (CrowdRoom100.cmd / CrowdRoom10.cmd)."))
end

if ok.is_main() then ok.main(M.main) end
return M
