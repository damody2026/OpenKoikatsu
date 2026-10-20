-- Publish a verified StreetSocial build, keeping a recoverable prior receipt.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local T = util.truthy
local M = {}

local function fail(msg) error(msg, 0) end
function M.read_json(p) return json.decode(path.read_text(p)) end
local read_json = M.read_json
local function sha_file(p) return ok.hash.file(p, "sha256") end
-- Path(p).resolve().as_posix() / str(Path(p)).
local function posix(p) return path.abs(p) end
local function abs_or_cwd(p) if p == nil or p == "" or p == json.null then p = "." end; return path.abs(p):lower() end
local function same_path(a, b) return abs_or_cwd(a) == abs_or_cwd(b) end
local function pystr_path(p)
  p = tostring(p):gsub("/", "\\"):gsub("\\+", "\\")
  return p
end
local function get(t, k, d) local v = t[k]; if v == nil then return d end; return v end
local function isclose(a, b, abs_tol) return math.abs(a - b) <= math.max(1e-9 * math.max(math.abs(a), math.abs(b)), abs_tol) end
local function log_base(log)
  local base = log:match("LogInit: Base Directory: ([^\n]+)")
  return base and util.strip(base) or nil
end
local function uses_executable(log, executable)
  local base = log_base(log)
  return base ~= nil and same_path(path.join(base, "OpenKoikatsu.exe"), executable)
end
local function read_log(p) return path.read_text(p) end
local function contains_all(list, from, to)
  local set = util.set(list or {})
  for i = from, to do if not set[i] then return false end end
  return true
end
M.same_path, M.uses_executable, M.posix = same_path, uses_executable, posix

-- datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ')
function M.stamp()
  local now = ok.fs.time()
  local secs = math.floor(now)
  return os.date("!%Y%m%dT%H%M%S", secs) .. string.format("%06dZ", math.floor((now - secs) * 1e6))
end

function M.verify_movement(evidence, executable)
  local report = read_json(evidence)
  local runtime = read_json(path.join(path.parent(evidence), "runtime.json"))
  local rate = get(report, "female_max_turn_degrees_per_second", 0 / 0)
  if (not T(report.passed) or T(report.errors) or not T(report.movement_only_test) or
      not T(report.movement_checked) or get(report, "floor_finishes_fixed", 0) < 1 or
      get(report, "movement_sweep_checks", 0) < 9 or get(report, "female_turn_samples", 0) < 100 or
      report.runtime_facing_checks ~= 5 or rate ~= rate or rate == math.huge or rate == -math.huge or not (0 < rate and rate <= 45.05) or
      not isclose(get(report, "mouse_yaw_sensitivity", 0), .5, 1.e-5) or
      not isclose(get(report, "mouse_pitch_sensitivity", 0), .2, 1.e-5) or
      runtime.complete ~= 6 or not T(runtime.gpu_ready) or runtime.gpu_failures ~= 0 or
      T(runtime.cooked_character_loaded)) then
    fail("Five runtime APNG actors, floor/wall sweeps and bounded female turning required")
  end
  if not uses_executable(read_log(path.join(path.parent(evidence), "game.log")), executable) then
    fail("Movement evidence must use the exact executable being published")
  end
  return report
end

function M.verify_presentation(evidence, executable)
  local report = read_json(evidence)
  local runtime = read_json(path.join(path.parent(evidence), "runtime.json"))
  if (not T(report.passed) or T(report.errors) or not T(report.presentation_only_test) or
      report.presentation_phase ~= 4 or report.standing_actors_verified ~= 5 or
      report.running_actors_verified ~= 6 or runtime.complete ~= 6 or
      not T(runtime.gpu_ready) or runtime.gpu_failures ~= 0 or T(runtime.cooked_character_loaded)) then
    fail("Five standing, six running and live slider/UI probes required")
  end
  if not uses_executable(read_log(path.join(path.parent(evidence), "game.log")), executable) then
    fail("Presentation evidence must use the exact executable being published")
  end
  local manifest = path.abs("Content/IndependentResources/RuntimeAnimations/locomotion.manifest.json")
  for _, row in ipairs(read_json(manifest).actions) do
    if sha_file(path.join(path.parent(manifest), row.file)) ~= row.sha256 then
      fail("Common locomotion resources changed since verification")
    end
  end
  return manifest
end

local function autonomous_check(evidence, prefix, independent_name, natural)
  local autonomous = read_json(evidence)
  local independent = read_json(path.join(path.parent(evidence), independent_name))
  local bad = not T(autonomous.passed) or T(autonomous.errors) or not T(autonomous["auto_" .. prefix .. "_checked"]) or
    autonomous["auto_" .. prefix .. "_verified"] ~= 5 or autonomous["auto_" .. prefix .. "_prepare_cancels"] ~= 5 or
    autonomous["auto_" .. prefix .. "_interrupts"] ~= 5
  if natural and not bad then bad = autonomous["auto_" .. prefix .. "_natural_completions"] ~= 5 end
  if not bad then
    bad = get(autonomous, "auto_" .. prefix .. "_samples", 0) <= 10 or not T(independent.passed) or independent.npc_cases ~= 5 or
      not contains_all(independent.private_actors, 1, 5)
  end
  return autonomous, independent, bad
end

-- publish(executable, patch, catalogue, evidence, receipt, opts) where opts holds the optional evidence paths:
-- invitations_evidence, npc_phone_evidence, npc_drink_evidence, npc_selfie_evidence, npc_groom_evidence,
-- movement_evidence, presentation_evidence, dialogue_context_evidence.
function M.publish(executable, patch, catalogue, evidence, receipt, opts)
  opts = opts or {}
  local evidence_dir = path.parent(evidence)
  local report = read_json(evidence)
  local runtime = read_json(path.join(evidence_dir, "runtime.json"))
  local av = get(report, "all_visible_seconds", 0)
  if (not T(report.passed) or T(report.errors) or
      report.characters ~= 6 or report.female_count ~= 5 or
      report.completed_dialogue_lines ~= 15 or
      report.greetings_completed ~= 5 or report.farewells_completed ~= 5 or
      not T(report.active_speech_cancel_checked) or
      not T(report.face_invalid_request_checked) or
      get(report, "expressions_applied", 0) < 25 or report.expressions_missing ~= 0 or
      not T(report.relationship_events_checked) or
      not (0 < av and av <= 60)) then
    fail("Successful six-character dialogue, expression, cancellation and save evidence required")
  end
  if (runtime.complete ~= 6 or not T(runtime.gpu_ready) or runtime.gpu_failures ~= 0 or T(runtime.cooked_character_loaded)) then
    fail("Runtime APNG GPU evidence required")
  end
  if report.phone_use_checked ~= nil and (not T(report.phone_use_checked) or report.phone_use_actors_verified ~= 5 or report.phone_use_voices ~= 5 or not T(report.phone_use_cancel_checked) or get(report, "phone_use_samples", 0) < 10) then
    fail("Five runtime private phone actions, voice, left-hand prop and cancel probes required")
  end
  local phone
  if T(report.phone_use_checked) then
    phone = read_json(path.join(evidence_dir, "phone-use.independent.validation.json"))
    if not T(phone.passed) or not util.equal(phone.private_actors, { 0, 1, 2, 3, 4, 5 }) or phone.phone_cases ~= 5 then
      fail("Independent phone curve and six private actor admission evidence required")
    end
  end
  if report.contacts_checked ~= nil and (not T(report.contacts_checked) or not T(report.contacts_ui_checked) or report.contacts_routes ~= 5 or report.contacts_private ~= 5 or (not T(report.private_high_model) and report.contacts_voices ~= 5)) then
    fail("Five roster/navigation/private lover probes required")
  end
  if T(report.contacts_checked) and not T(read_json(path.join(evidence_dir, "contacts.independent.validation.json")).passed) then
    fail("Independent stage-two private voice source verification required")
  end
  if T(get(report, "private_any_verified", 0)) and (report.private_any_verified ~= 15 or read_json(path.join(evidence_dir, "contacts.independent.validation.json")).private_any_verified ~= 15) then
    fail("Fifteen source-verified any-relationship private scene cases required")
  end
  if report.minimap_checked ~= nil and not T(report.minimap_checked) then
    fail("Live runtime minimap projection/color/paint/save probe required")
  end
  if report.shared_splash_checked ~= nil and (not T(report.shared_splash_checked) or report.splash_cases_verified ~= 10 or not T(report.splash_cancel_checked) or get(report, "splash_effect_samples", 0) < 16) then
    fail("Ten runtime private splash direction/restore/effect probes required")
  end
  if report.reading_props_checked ~= nil and (not T(report.reading_props_checked) or report.reading_actors_verified ~= 6) then
    fail("Six runtime reading book attachment and walk-hide probes required")
  end
  if report.shared_watermelon_checked ~= nil then
    local dist = get(report, "watermelon_min_distances_cm", {})
    if (not T(report.shared_watermelon_checked) or report.watermelon_cases_verified ~= 10 or not T(report.watermelon_cancel_checked) or get(report, "watermelon_hits", 0) < 10 or #dist ~= 10 or util.any(report.watermelon_min_distances_cm, function(v) return not (0 <= v and v < 12.4) end)) then
      fail("Ten physically contacting private watermelon role/restore probes required")
    end
  end
  if report.shared_meal_checked ~= nil and (not T(report.shared_meal_checked) or report.meal_actors_verified ~= 5 or report.meal_voices_completed ~= 5 or not T(report.meal_cancel_checked) or get(report, "meal_hand_samples", 0) < 10) then
    fail("Five private paired meal/hand-prop and cancellation restore probes required")
  end
  if report.shared_drink_checked ~= nil and (not T(report.shared_drink_checked) or report.drink_actors_verified ~= 5 or report.drink_voices_completed ~= 5 or not T(report.drink_cancel_checked) or get(report, "drink_hand_samples", 0) < 10) then
    fail("Five private paired drink/hand-prop and cancellation restore probes required")
  end
  if report.seated_talk_checked ~= nil and (not T(report.seated_talk_checked) or report.seated_actors_verified ~= 5 or report.seated_voices_completed ~= 5 or not T(report.seated_cancel_checked)) then
    fail("Five seated voice/pose and cancellation restore probes required")
  end
  if report.paired_photo_checked ~= nil and (not T(report.paired_photo_checked) or report.photo_actors_verified ~= 5 or get(report, "photos_saved", 0) < 5 or not T(report.photo_cancel_checked)) then
    fail("Five private paired photo/capture/restore and preparation cancel probes required")
  end
  if report.shared_walk_checked ~= nil and not T(report.shared_walk_checked) then
    fail("Shared walk movement, stop, collision and cancel evidence required")
  end
  if report.facility_prepare_cancels ~= nil and (report.facility_prepare_cancels ~= 3 or report.facility_clock_checks ~= 3) then
    fail("Three delayed facility admission/cancel and independent use-clock probes required")
  end
  if report.facilities_checked ~= nil and (not T(report.facilities_checked) or report.facilities_verified ~= 3) then
    fail("Three runtime facility action/exit probes required")
  end
  local touch_independent
  if report.touch_checked ~= nil then
    touch_independent = read_json(path.join(evidence_dir, "collected-topics.independent.validation.json"))
    if (not T(report.conversation_controls_checked) or not T(report.touch_input_routing_checked) or not T(report.touch_missing_subtitle_checked) or not T(report.touch_missing_recovery_checked) or not T(report.touch_checked) or report.touch_commits ~= 3 or
        not T(report.collected_topics_checked) or report.collected_topic_commits ~= 4 or
        not T(touch_independent.passed) or not util.equal(touch_independent.touch_repeats, { 0, 2, 1 })) then
      fail("Source-verified topic and touch completion/cancellation evidence required")
    end
  end
  for _, p in ipairs({ executable, patch, catalogue }) do
    if not path.exists(p) then fail("Missing verified input: " .. pystr_path(p)) end
  end
  local executable_hash = sha_file(executable)
  if not path.is_file(path.join(evidence_dir, "game.log")) then
    fail("Actual full regression executable log required")
  end
  if not uses_executable(read_log(path.join(evidence_dir, "game.log")), executable) then
    fail("Full regression evidence must use the exact executable being published")
  end
  local updated
  if path.exists(receipt) then
    local old = read_json(receipt)
    updated = json.object()
    for k, v in pairs(old) do updated[k] = v end
  else
    updated = json.object({ { "schema", 1 } })
  end
  for _, kv in ipairs({ { "passed", true }, { "new_map", true }, { "female_count", 5 }, { "full_acceptance", false },
    { "executable", posix(executable) }, { "patch", posix(patch) }, { "catalogue", posix(catalogue) }, { "evidence", posix(evidence) },
    { "runtime_expressions", true }, { "expressions_applied", report.expressions_applied }, { "executable_sha256", executable_hash } }) do
    updated[kv[1]] = kv[2]
  end
  updated.runtime_phone_use = T(report.phone_use_checked)
  if updated.runtime_phone_use then
    local phone_manifest = path.abs("Content/IndependentResources/RuntimeAnimations/phone.manifest.json")
    if sha_file(phone_manifest) ~= phone.shared_manifest_sha256 then fail("Shared phone manifest changed since validation") end
    updated.phone_manifest = phone_manifest
    updated.phone_manifest_sha256 = phone.shared_manifest_sha256
  else
    json.remove(updated, "phone_manifest")
    json.remove(updated, "phone_manifest_sha256")
  end
  updated.runtime_contacts = T(report.contacts_checked)
  updated.runtime_private_any = T(report.contacts_checked) and report.private_any_verified == 15
  updated.private_any_verified = get(report, "private_any_verified", 0)
  updated.runtime_shared_watermelon = T(report.shared_watermelon_checked)
  updated.runtime_shared_meal = T(report.shared_meal_checked)
  updated.runtime_minimap = T(report.minimap_checked)
  updated.runtime_shared_splash = T(report.shared_splash_checked)
  updated.runtime_reading_props = T(report.reading_props_checked)
  updated.runtime_shared_drink = T(report.shared_drink_checked)
  updated.runtime_seated_talk = T(report.seated_talk_checked)
  updated.runtime_paired_photo = T(report.paired_photo_checked)
  if T(report.paired_photo_checked) then updated.photo_actors_verified = report.photo_actors_verified
  else json.remove(updated, "photo_actors_verified") end
  if T(report.shared_walk_checked) then updated.runtime_shared_walk = true end
  if T(report.facilities_checked) then
    updated.runtime_facility_prototypes = true
    updated.facilities_verified = report.facilities_verified
  end
  if report.touch_checked ~= nil then
    for _, kv in ipairs({ { "runtime_collected_topics", true }, { "runtime_touch", true }, { "runtime_touch_missing_recovery", true },
      { "runtime_touch_input_routing", true }, { "runtime_conversation_controls", true }, { "runtime_touch_separate_notice", true },
      { "topic_pickup_count", report.topic_pickup_count }, { "touch_commits", report.touch_commits },
      { "source_voice_hashes", touch_independent.source_voice_hashes } }) do
      if kv[2] == nil then error("KeyError: topic_pickup_count", 0) end
      updated[kv[1]] = kv[2]
    end
  end
  for _, key in ipairs({ "all_visible_seconds", "viewport_width", "viewport_height", "offscreen", "frame_mean_ms", "frame_p95_ms" }) do
    if report[key] == nil then error("KeyError: '" .. key .. "'", 0) end
    updated[key] = report[key]
  end
  if report.face_transition_started ~= nil then
    for _, key in ipairs({ "face_transition_started", "face_transition_intermediate", "face_transition_completed" }) do
      if get(report, key, 0) <= 0 then fail("Successful source facial transition evidence required") end
      updated[key] = report[key]
    end
    updated.runtime_face_transitions = true
  end
  for _, key in ipairs({ "runtime_invitations", "runtime_invitation_ledger", "invitation_evidence", "invitations_catalogue", "invitations_sha256" }) do
    json.remove(updated, key)
  end
  local invitations_evidence = opts.invitations_evidence
  if invitations_evidence ~= nil then
    local layout = read_json(path.join(evidence_dir, "source.layout.validation.json"))
    if not T(layout.passed) or layout.layout_mismatches ~= 0 then
      fail("Corrected shared dialogue/touch source layout validation required")
    end
    local invitation = read_json(invitations_evidence)
    local independent_invitation = read_json(path.join(path.parent(invitations_evidence), "invitations.independent.validation.json"))
    if (not T(invitation.passed) or T(invitation.errors) or
        not T(invitation.invitation_only_test) or invitation.invitation_cases ~= 20 or
        invitation.invitation_voices ~= 20 or invitation.invitation_accepts ~= 10 or
        invitation.invitation_refusals ~= 10 or invitation.invitation_cancels ~= 20 or
        not T(independent_invitation.passed) or invitation.invitation_ledger_checks ~= 20 or independent_invitation.ledger_checks ~= 20) then
      fail("Twenty actual invitation acceptance/refusal/cancel voice cases required")
    end
    local manifest = read_json(path.join(path.parent(invitations_evidence), "selection.json"))
    local invitations_path = manifest.invitation_catalogue
    if sha_file(invitations_path) ~= independent_invitation.catalogue_sha256 then
      fail("Invitation data changed after source verification")
    end
    local game_log = read_log(path.join(path.parent(invitations_evidence), "game.log"))
    if not game_log:find(path.parent(path.abs(executable)) .. "/", 1, true) then
      fail("Invitation evidence must use the executable being published")
    end
    updated.runtime_invitations = true
    updated.runtime_invitation_ledger = true
    updated.invitation_evidence = posix(invitations_evidence)
    updated.invitations_catalogue = posix(invitations_path)
    updated.invitations_sha256 = independent_invitation.catalogue_sha256
  end
  json.remove(updated, "runtime_npc_phone")
  json.remove(updated, "npc_phone_evidence")
  if opts.npc_phone_evidence ~= nil then
    local ev = opts.npc_phone_evidence
    local _, independent, bad = autonomous_check(ev, "phone", "npc-phone.independent.validation.json")
    if bad then fail("Five autonomous runtime APNG phone/cancel/recovery cases required") end
    local game_log = read_log(path.join(path.parent(ev), "game.log"))
    if not game_log:find(path.parent(path.abs(executable)) .. "/", 1, true) or independent.executable_sha256 ~= executable_hash then
      fail("Autonomous phone evidence must use the exact executable being published")
    end
    if not updated.runtime_phone_use or independent.shared_manifest_sha256 ~= updated.phone_manifest_sha256 then
      fail("Autonomous and paired phone must verify the same common resources")
    end
    updated.runtime_npc_phone = true
    updated.npc_phone_evidence = posix(ev)
  end
  for _, spec in ipairs({
    { "drink", opts.npc_drink_evidence, "Five autonomous drink/cancel/recovery cases on the exact executable required", "Common drink resources changed since verification", false },
    { "selfie", opts.npc_selfie_evidence, "Five autonomous selfie/cancel/recovery cases on the exact executable required", "Common selfie resources changed since verification", false },
    { "groom", opts.npc_groom_evidence, "Five autonomous groom/cancel/recovery cases on the exact executable required", "Common groom resources changed since verification", true },
  }) do
    local name, ev, msg, changed, natural = spec[1], spec[2], spec[3], spec[4], spec[5]
    json.remove(updated, "runtime_npc_" .. name)
    json.remove(updated, "npc_" .. name .. "_evidence")
    json.remove(updated, name .. "_manifest")
    json.remove(updated, name .. "_manifest_sha256")
    if ev ~= nil then
      local _, independent, bad = autonomous_check(ev, name, "npc-" .. name .. ".independent.validation.json", natural)
      if bad or independent.executable_sha256 ~= executable_hash then fail(msg) end
      local manifest = path.abs("Content/IndependentResources/RuntimeAnimations/" .. name .. ".manifest.json")
      if sha_file(manifest) ~= independent.shared_manifest_sha256 then fail(changed) end
      updated["runtime_npc_" .. name] = true
      updated["npc_" .. name .. "_evidence"] = posix(ev)
      updated[name .. "_manifest"] = manifest
      updated[name .. "_manifest_sha256"] = independent.shared_manifest_sha256
    end
  end
  for _, key in ipairs({ "runtime_movement_fix", "movement_evidence", "female_turn_limit_degrees_per_second", "mouse_yaw_sensitivity", "mouse_pitch_sensitivity" }) do
    json.remove(updated, key)
  end
  if opts.movement_evidence ~= nil then
    local movement = M.verify_movement(opts.movement_evidence, executable)
    updated.runtime_movement_fix = true
    updated.movement_evidence = posix(opts.movement_evidence)
    updated.female_turn_limit_degrees_per_second = 45
    updated.mouse_yaw_sensitivity = movement.mouse_yaw_sensitivity
    updated.mouse_pitch_sensitivity = movement.mouse_pitch_sensitivity
  end
  for _, key in ipairs({ "runtime_presentation", "presentation_evidence", "locomotion_manifest", "locomotion_manifest_sha256" }) do
    json.remove(updated, key)
  end
  if opts.presentation_evidence ~= nil then
    local locomotion = M.verify_presentation(opts.presentation_evidence, executable)
    updated.runtime_presentation = true
    updated.presentation_evidence = posix(opts.presentation_evidence)
    updated.locomotion_manifest = locomotion
    updated.locomotion_manifest_sha256 = sha_file(locomotion)
  end
  for _, key in ipairs({ "runtime_dialogue_context", "dialogue_context_evidence", "relationships_catalogue", "relationships_sha256", "runtime_confession_outcomes" }) do
    json.remove(updated, key)
  end
  if opts.dialogue_context_evidence ~= nil then
    local ev = opts.dialogue_context_evidence
    require("verify_street_social_dialogue_context").verify(path.parent(ev))
    local proof = read_json(path.join(path.parent(ev), "dialogue-context.independent.validation.json"))
    local selection = read_json(path.join(path.parent(ev), "selection.json"))
    if proof.executable_sha256 ~= executable_hash then
      fail("Dialogue context evidence must use the exact executable being published")
    end
    local relationships = selection.source_relationship_catalogue
    if not same_path(relationships, path.with_name(path.abs(catalogue), "relationships.json")) then
      fail("Dialogue context evidence must use the published source data")
    end
    updated.runtime_dialogue_context = true
    updated.runtime_confession_outcomes = proof.source_refusals == 5
    updated.dialogue_context_evidence = posix(ev)
    updated.relationships_catalogue = posix(relationships)
    updated.relationships_sha256 = proof.relationships_sha256
  end
  path.mkdir(path.parent(receipt))
  local stamp = M.stamp()
  if path.exists(receipt) then
    path.write_bytes(path.with_name(receipt, "receipt.before-" .. stamp .. ".json"), path.read_bytes(receipt))
  end
  local temporary = path.with_name(receipt, "receipt.pending-" .. stamp .. ".json")
  path.write_text(temporary, json.encode(updated, { ensure_ascii = false, indent = 2 }) .. "\n")
  path.replace(temporary, receipt)
  return updated
end

if ok.is_main() then
  ok.main(function()
    local p = ok.args.parser("Publish a verified StreetSocial build, keeping a recoverable prior receipt.")
    for _, name in ipairs({ "executable", "patch", "catalogue", "evidence" }) do p:opt("--" .. name, { required = true }) end
    p:opt("--receipt", { default = "Saved/StreetSocial/Build/receipt.json" })
    for _, name in ipairs({ "invitations-evidence", "npc-phone-evidence", "npc-drink-evidence", "npc-selfie-evidence",
      "npc-groom-evidence", "movement-evidence", "presentation-evidence", "dialogue-context-evidence" }) do
      p:opt("--" .. name)
    end
    local a = p:parse(arg)
    local result = M.publish(a.executable, a.patch, a.catalogue, a.evidence, a.receipt, {
      invitations_evidence = a.invitations_evidence, npc_phone_evidence = a.npc_phone_evidence,
      npc_drink_evidence = a.npc_drink_evidence, npc_selfie_evidence = a.npc_selfie_evidence,
      npc_groom_evidence = a.npc_groom_evidence, movement_evidence = a.movement_evidence,
      presentation_evidence = a.presentation_evidence, dialogue_context_evidence = a.dialogue_context_evidence,
    })
    print(json.encode(json.object({ { "receipt", (pystr_path(a.receipt):gsub("\\", "/")) }, { "executable", result.executable },
      { "evidence", result.evidence } }), { ensure_ascii = false }))
  end)
end

return M
