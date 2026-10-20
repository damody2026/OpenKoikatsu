-- Publish common rendering changes with exact-build functional and visual evidence.
local ok = require "ok"
local receipt_mod = require "publish_street_social_receipt"
local json, path, util = ok.json, ok.path, ok.util
local T = util.truthy
local publish, read_json = receipt_mod.publish, receipt_mod.read_json
local same_path, uses_executable, posix = receipt_mod.same_path, receipt_mod.uses_executable, receipt_mod.posix
local M = {}

local function fail(msg) error(msg, 0) end
function M.sha256(p) return ok.hash.file(p, "sha256") end
local sha256 = M.sha256
local function read_log(p) return path.read_text(p) end

-- UTF-16 (with BOM) to UTF-8, like bytes.decode('utf-16').
local function utf16_to_utf8(raw)
  local le = raw:sub(1, 2) == "\255\254"
  local fmt = le and "<I2" or ">I2"
  local out, i, n = {}, 3, #raw
  while i + 1 <= n do
    local u = string.unpack(fmt, raw, i); i = i + 2
    if u >= 0xD800 and u <= 0xDBFF and i + 1 <= n then
      local lo = string.unpack(fmt, raw, i)
      if lo >= 0xDC00 and lo <= 0xDFFF then u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00); i = i + 2 end
    end
    out[#out + 1] = utf8.char(u)
  end
  return table.concat(out)
end
M.utf16_to_utf8 = utf16_to_utf8

-- Engine's default material warning falls back to the same default material;
-- it is not a missing cooked character shader. Keep all other warnings/errors.
function M.shader_fallback(log)
  local kept = {}
  for _, line in ipairs(util.splitlines(log)) do
    if not line:find("Material /Engine/EngineMaterials/DefaultMaterial.DefaultMaterial missing usage flag Nanite!", 1, true) then kept[#kept + 1] = line end
  end
  local text = table.concat(kept, "\n")
  for _, needle in ipairs({ "Missing shader resource", "uncooked shader map", "Default Material will be used", "Invalid shader map ID" }) do
    if text:find(needle, 1, true) then return needle end
  end
  return nil
end
local shader_fallback = M.shader_fallback

function M.verify_independent(args, optional)
  -- Recompute source proofs rather than trusting leftover independent JSON files.
  local folder = path.parent(path.abs(args.evidence))
  require("verify_street_social_topics").verify(ok.root, folder)
  require("verify_street_social_phone").verify(folder)
  require("verify_street_social_contacts").verify(folder)
  require("verify_street_social_source_layout").verify(args.catalogue, path.join(folder, "source.layout.validation.json"))
  local modules = {
    { "invitations_evidence", "verify_street_social_invitations" },
    { "npc_phone_evidence", "verify_street_social_npc_phone" },
    { "npc_drink_evidence", "verify_street_social_npc_drink" },
    { "npc_selfie_evidence", "verify_street_social_npc_selfie" },
    { "npc_groom_evidence", "verify_street_social_npc_groom" },
  }
  for _, m in ipairs(modules) do
    if optional[m[1]] ~= nil then require(m[2]).verify(path.parent(path.abs(optional[m[1]]))) end
  end
end

-- re.findall(r'Runtime source outlines .*?: materials=(\d+) sections=(\d+)', log)
local function outline_counts(log)
  local found = {}
  for _, line in ipairs(util.splitlines(log)) do
    local pos = 1
    while true do
      local a = line:find("Runtime source outlines ", pos, true)
      if not a then break end
      local s, e, m, sec = line:find(": materials=(%d+) sections=(%d+)", a + 24)
      if not s then break end
      found[#found + 1] = { tonumber(m), tonumber(sec) }
      pos = e + 1
    end
  end
  return found
end

function M.verify_visual(folder, executable, material_root)
  local proof = read_json(path.join(folder, "visual.comparison.json"))
  local scene = read_json(path.join(folder, "scene.json"))
  local selection = read_json(path.join(folder, "selection.json"))
  local log = read_log(path.join(folder, "scene.log"))
  if log:find("OKRuntimeDisplayDebug=[1-9]") then
    fail("Material input diagnostics are not appearance evidence: " .. folder)
  end
  if shader_fallback(log) then
    fail("All cooked source shaders must load without fallback: " .. folder)
  end
  local material_inputs = path.glob(folder, "scene.json.*.materials.json")
  if #material_inputs == 0 then
    fail("Per-part source outline evidence required: " .. folder)
  end
  for _, inputs in ipairs(material_inputs) do
    -- UE SaveStringToFile uses UTF-16 for non-ASCII material names.
    local raw = path.read_bytes(inputs)
    local text
    if raw:sub(1, 2) == "\255\254" or raw:sub(1, 2) == "\254\255" then text = utf16_to_utf8(raw) else text = raw end
    local decoded = json.decode(text)
    for _, part in ipairs(decoded.parts) do
      if (part.shader_kind == 1 or part.shader_kind == 10) and part.outline_enabled ~= true then
        fail("Hair's unconditional outline pass must be enabled: " .. util.str(part.part))
      end
    end
  end
  local outlines = outline_counts(log)
  local views_ok = false
  if T(proof.passed) and T(proof.renderer_palette_verified) then
    local names, count = {}, 0
    for _, v in ipairs(proof.views or {}) do
      if not names[v.view] then names[v.view] = true; count = count + 1 end
    end
    views_ok = count == 3 and names.front and names.side and names.back
      and not util.any(proof.views, function(v) return not T(v.passed) or v.max_error ~= 0 end)
  end
  if (not views_ok or
      not T(scene.gpu_ready) or scene.gpu_failures ~= 0 or
      scene.scene_skinning_matrix_mismatches ~= 0 or
      not T(scene.characters) or
      util.any(scene.characters, function(c) return not T(c.source_geometry_preserved) end) or
      selection.material_root ~= material_root or
      #outlines == 0 or util.any(outlines, function(o) return o[1] <= 0 or o[2] <= 0 end) or
      not uses_executable(log, executable)) then
    fail("Exact executable, source geometry, common outlines and three matching views required: " .. folder)
  end
  return json.object({
    { "comparison", posix(path.join(folder, "visual.comparison.json")) },
    { "sha256", sha256(path.join(folder, "visual.comparison.json")) },
    { "cards", util.map(scene.characters, function(c) return c.apng end) },
  })
end
local verify_visual = M.verify_visual

local function starts_with_any(k, prefixes)
  for _, p in ipairs(prefixes) do if k:sub(1, #p) == p then return true end end
  return false
end

function M.release(args)
  local report = read_json(args.evidence)
  if not T(report.relationship_auto_commit_checked) or report.relationship_auto_commits_verified ~= 1 then
    fail("Automatic relationship commit, cancellation and persisted reload evidence required")
  end
  local materials = read_json(args.material_manifest)
  local lut = path.join(ok.root, "Content/KKPreview/KKSDefaultSaturationLUT16.png")
  if not path.is_file(lut) then fail("Shared CardReader display LUT required") end
  for _, evidence in ipairs({ args.evidence, args.presentation_evidence, args.dialogue_context_evidence }) do
    local r = read_json(evidence)
    local selection = read_json(path.join(path.parent(evidence), "selection.json"))
    if not T(r.camera_display_lut_applied) or not same_path(selection.display_lut or "", lut) then
      fail("Actual gameplay camera and selection must use the shared CardReader LUT")
    end
    if shader_fallback(read_log(path.join(path.parent(evidence), "game.log"))) then
      fail("Gameplay acceptance cannot contain material shader fallback")
    end
  end
  local assets = materials.assets or {}
  if (not T(materials.passed) or not T(materials.source_mesh_shader_permutations_verified) or
      materials.per_card_input_used ~= false or materials.runtime_compilation_required ~= false or
      not util.any(assets, function(asset) return util.str(asset):find("_Outline.", 1, true) ~= nil end)) then
    fail("Precompiled common source outline materials required")
  end
  local visuals = {}
  for _, folder in ipairs(args.visual) do visuals[#visuals + 1] = verify_visual(folder, args.executable, materials.root) end
  for _, folder in ipairs(args.visual) do
    local scene = read_json(path.join(folder, "scene.json"))
    if not T(scene.display_lut_applied) or not same_path(scene.display_lut or "", lut) then
      fail("Source appearance evidence must include the final CardReader color profile")
    end
  end
  local cards, ncards = {}, 0
  for _, proof in ipairs(visuals) do
    for _, card in ipairs(proof.cards) do if not cards[card] then cards[card] = true; ncards = ncards + 1 end end
  end
  if ncards < 3 then fail("At least three distinct source cards required") end
  local optional_names = { "invitations_evidence", "npc_phone_evidence", "npc_drink_evidence", "npc_selfie_evidence", "npc_groom_evidence", "movement_evidence" }
  local optional = {}
  for _, name in ipairs(optional_names) do optional[name] = args[name] end
  local acceptance = { args.evidence, args.presentation_evidence, args.dialogue_context_evidence }
  for _, name in ipairs(optional_names) do if optional[name] ~= nil then acceptance[#acceptance + 1] = optional[name] end end
  for _, evidence in ipairs(acceptance) do
    if read_json(path.join(path.parent(evidence), "selection.json")).material_root ~= materials.root then
      fail("All current acceptance runs must use the published material root")
    end
  end
  local patches = util.sorted(path.glob(args.patch, "*.pak"), function(p) return p:lower() end)
  if #patches == 0 then fail("Runtime shader/audio/map patch files required") end
  if #path.glob(args.patch, "RuntimeMaterialPatch-*.pak") ~= 1 then
    fail("One common RuntimeMaterialBake shader archive required; compose a fresh patch with prepare_street_social_render_patch.lua")
  end
  M.verify_independent(args, optional)
  local touch_evidence = args.touch_reaction_evidence
  if touch_evidence ~= nil then
    if read_json(path.join(path.parent(touch_evidence), "selection.json")).material_root ~= materials.root then
      fail("Touch reaction evidence must use the published common material root")
    end
    require("verify_street_social_touch_reaction").verify(path.parent(path.abs(touch_evidence)))
    if not uses_executable(read_log(path.join(path.parent(touch_evidence), "game.log")), args.executable) then
      fail("Touch reaction evidence belongs to another executable")
    end
  end
  local old = path.exists(args.receipt) and read_json(args.receipt) or json.object()
  if (T(old.runtime_interaction_watergun) or T(old.runtime_interaction_full_catalogue)) and args.interaction_features_evidence == nil then
    fail("Previously verified interaction watergun/catalogue requires current feature evidence")
  end
  local quality = args.antialiasing_evidence
  if quality ~= nil then
    local q = read_json(quality)
    local qlog = read_log(path.join(path.parent(quality), "game.log"))
    if (not T(q.passed) or not T(q.presentation_only_test) or T(q.errors) or
        not T(q.camera_display_lut_applied) or
        read_json(path.join(path.parent(quality), "selection.json")).material_root ~= materials.root or
        not uses_executable(qlog, args.executable) or
        not qlog:find('r.AntiAliasingMethod = "1"', 1, true) or
        not qlog:find('r.ScreenPercentage = "150"', 1, true) or
        shader_fallback(qlog)) then
      fail("Exact-build FXAA 150% gameplay presentation evidence required")
    end
  end
  -- Run the existing full/independent validators before touching the live receipt.
  local temporary = path.temp("-street-release")
  path.mkdir(temporary)
  local okp, current = pcall(publish, args.executable, args.patch, args.catalogue, args.evidence, path.join(temporary, "receipt.json"), {
    presentation_evidence = args.presentation_evidence, dialogue_context_evidence = args.dialogue_context_evidence,
    invitations_evidence = optional.invitations_evidence, npc_phone_evidence = optional.npc_phone_evidence,
    npc_drink_evidence = optional.npc_drink_evidence, npc_selfie_evidence = optional.npc_selfie_evidence,
    npc_groom_evidence = optional.npc_groom_evidence, movement_evidence = optional.movement_evidence,
  })
  path.rmtree(temporary)
  if not okp then error(current, 0) end
  local presentation = read_json(args.presentation_evidence)
  if presentation.relationship_controls_verified ~= 15 or not T(presentation.relationship_controls_sync) then
    fail("Fifteen mutually exclusive live relationship controls required")
  end
  local stamp = receipt_mod.stamp()
  local backup = path.with_name(args.receipt, "receipt.before-" .. stamp .. ".json")
  if path.exists(args.receipt) then path.write_bytes(backup, path.read_bytes(args.receipt)) end
  -- Optional regressions keep their original evidence paths and explicit provenance.
  local retained_prefixes = { "runtime_npc_", "npc_", "runtime_invitation", "invitation", "drink_manifest", "selfie_manifest",
    "groom_manifest", "runtime_movement_fix", "movement_evidence", "female_turn_limit_", "mouse_yaw_", "mouse_pitch_" }
  local updated, retained_keys = json.object(), {}
  for k, v in pairs(old) do
    if starts_with_any(k, retained_prefixes) and current[k] == nil then updated[k] = v; retained_keys[#retained_keys + 1] = k end
  end
  for k, v in pairs(current) do updated[k] = v end
  local has_old = T(old)
  local patch_hashes = json.object()
  for _, p in ipairs(patches) do patch_hashes[path.name(p)] = sha256(p) end
  for _, kv in ipairs({
    { "runtime_display_profile", json.object({ { "lut", posix(lut) }, { "sha256", sha256(lut) }, { "intensity", 1 }, { "tone_curve", 0 }, { "name", "CardReaderStudio" } }) },
    { "validation_scope", "Exact-build StreetSocial functional, presentation, confession and common rendering acceptance" },
    { "full_functional_acceptance", true },
    { "material_manifest", posix(args.material_manifest) },
    { "material_manifest_sha256", sha256(args.material_manifest) },
    { "patch_file_sha256", patch_hashes },
    { "runtime_source_outlines", true }, { "runtime_speech_follows_instance", true },
    { "runtime_appearance_fix", true }, { "runtime_texture_max_dimension", 1024 },
    { "appearance_original_game_parity", false }, { "appearance_visual_evidence", visuals },
    { "appearance_evidence", visuals[1].comparison },
    { "runtime_relationship_auto_commit", true },
    { "relationship_auto_commit_evidence", posix(args.evidence) },
    { "runtime_relationship_controls", true }, { "relationship_controls_verified", 15 },
    { "relationship_controls_sync", true },
    { "relationship_controls_evidence", posix(args.presentation_evidence) },
    { "contacts_evidence", posix(args.evidence) },
    { "private_any_evidence", posix(args.evidence) },
    { "regression_baseline_receipt", has_old and posix(backup) or json.null },
    { "regression_baseline_receipt_sha256", has_old and sha256(backup) or json.null },
    { "retained_baseline_fields", util.sorted(retained_keys) },
    { "retained_baseline_note", "Optional feature evidence retains its original executable provenance; current full acceptance does not relabel those runs." },
  }) do updated[kv[1]] = kv[2] end
  if T(read_json(args.evidence).private_high_model) then
    local high_evidence = args.high_interaction_evidence or args.evidence
    require("verify_street_social_contacts").verify(path.parent(path.abs(high_evidence)))
    local high_selection = read_json(path.join(path.parent(high_evidence), "selection.json"))
    local high_log = read_log(path.join(path.parent(high_evidence), "game.log"))
    if (high_selection.interaction_mode ~= "in_process" or high_selection.material_root ~= materials.root or
        not uses_executable(high_log, args.executable)) then
      fail("Exact-build in-process fifteen-case high interaction evidence required")
    end
    local reader_evidence = args.reader_interaction_evidence
    if reader_evidence == nil then fail("Shared reader mode restoration/playback evidence required") end
    local reader = read_json(reader_evidence)
    local cases = reader.cases or {}
    local names = {}
    for _, case in ipairs(cases) do if T(case.passed) then names[#names + 1] = case.case end end
    local function count(name) local n = 0; for _, v in ipairs(names) do if v == name then n = n + 1 end end; return n end
    if (not T(reader.passed) or T(reader.error) or
        count("reader_high_pair_roundtrip_preserves_unsaved_source") < 3 or
        count("pair_pause_replay_speed_shared_clock") < 3 or
        count("cancel_during_loading_restores_source") == 0 or
        count("missing_resource_preserves_source") == 0) then
      fail("Three reader mode roundtrips, playback controls, cancellation and missing-resource proof required")
    end
    for _, kv in ipairs({
      { "runtime_private_high_model", true }, { "runtime_interaction_mode", "in_process" },
      { "contacts_evidence", posix(high_evidence) }, { "private_any_evidence", posix(high_evidence) },
      { "reader_interaction_evidence", posix(reader_evidence) }, { "reader_interaction_evidence_sha256", sha256(reader_evidence) },
      { "high_interaction_validation_note", "Both entry points share one in-world mode, independent high-model sessions and the paired playback controller. No external interaction host or IPC control." },
    }) do updated[kv[1]] = kv[2] end
  end
  if touch_evidence ~= nil then
    local touch_manifest = path.join(ok.root, "Content/IndependentResources/RuntimeAnimations/touch.manifest.json")
    updated.runtime_touch_body_reaction = true
    updated.touch_reaction_verified = 20
    updated.touch_reaction_evidence = posix(touch_evidence)
    updated.touch_manifest = touch_manifest
    updated.touch_manifest_sha256 = sha256(touch_manifest)
  end
  if quality ~= nil then
    updated.runtime_render_quality = json.object({ { "antialiasing_method", 1 }, { "screen_percentage", 150 },
      { "name", "FXAA_Supersampling" }, { "evidence", posix(quality) }, { "evidence_sha256", sha256(quality) } })
  end
  local feature_evidence = args.interaction_features_evidence
  if feature_evidence ~= nil then
    local proof = require("verify_interaction_features").verify(path.abs(feature_evidence), args.executable)
    local reader_evidence = args.reader_interaction_evidence
    if reader_evidence == nil or not util.any(read_json(reader_evidence).cases or {}, function(c) return c.case == "solo_catalogue_demand_playback" and T(c.passed) end) then
      fail("Reader single-motion catalogue and demand playback proof required")
    end
    updated.runtime_interaction_watergun = true
    updated.runtime_interaction_full_catalogue = true
    updated.interaction_features_evidence = proof
    if T(proof.clothing_visibility_preserves_high_model) then updated.runtime_instant_clothing_visibility = true end
  end
  local pending = path.with_name(args.receipt, "receipt.pending-" .. stamp .. ".json")
  path.write_text(pending, json.encode(updated, { ensure_ascii = false, indent = 2 }) .. "\n")
  path.replace(pending, args.receipt)
  return updated
end

if ok.is_main() then
  ok.main(function()
    local p = ok.args.parser("Publish common rendering changes with exact-build functional and visual evidence.")
    for _, field in ipairs({ "executable", "patch", "catalogue", "evidence", "material-manifest", "presentation-evidence", "dialogue-context-evidence" }) do
      p:opt("--" .. field, { required = true })
    end
    p:opt("--visual", { multiple = true, required = true })
    for _, field in ipairs({ "invitations-evidence", "npc-phone-evidence", "npc-drink-evidence", "npc-selfie-evidence", "npc-groom-evidence", "movement-evidence" }) do
      p:opt("--" .. field)
    end
    p:opt("--receipt", { default = "Saved/StreetSocial/Build/receipt.json" })
    for _, field in ipairs({ "touch-reaction-evidence", "antialiasing-evidence", "high-interaction-evidence", "reader-interaction-evidence", "interaction-features-evidence" }) do
      p:opt("--" .. field)
    end
    local args = p:parse(arg)
    if #args.visual == 0 then p:fail("the following arguments are required: --visual") end
    local result = M.release(args)
    print(json.encode(json.object({ { "executable", result.executable }, { "executable_sha256", result.executable_sha256 },
      { "full_functional_acceptance", result.full_functional_acceptance } })))
  end)
end

return M
