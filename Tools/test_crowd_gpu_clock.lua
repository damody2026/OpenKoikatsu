-- Owned CPU -> GPU -> CPU toggle test of the independent GPU clock in one
-- controlled game (port of test_crowd_gpu_clock.ps1). Builds the launch with
-- run_crowd_development.lua --validate-only, starts it hidden, drives three
-- control revisions and writes Saved/Crowd/GpuClockControl-<Tag>.json.
--   Tools\lua\lua.exe Tools\test_crowd_gpu_clock.lua --tag <fresh> --stage-root <stage> --shader-patch-directory <dir>
--       --selection-file <selection.json> [--gpu-shard-index <index> | --gpu-cache <cache> --gpu-identity <sha1>] [--benchmark] [...]
local ok = require "ok"
local C = require "crowd_launch_common"
local development = require "run_crowd_development"
local json = ok.json
local T, ne, fail = C.truthy, C.ne, C.fail

local M = {}

M.spec = {
  { "Tag", "string", mandatory = true, pattern = "^[A-Za-z0-9_-]+$" },
  { "StageRoot", "string", mandatory = true },
  { "ShaderPatchDirectory", "string", mandatory = true },
  { "SelectionFile", "string", mandatory = true },
  { "GpuShardIndex", "string", default = "" },
  { "GpuCache", "string", default = "" },
  { "GpuIdentity", "string", default = "" },
  { "PilotCard", "int", default = -1, range = { -1, 99 } },
  { "Variant", "string", default = "GameLowRoleBudgetRigR2", set = { "GameLowRoleBudgetRigR2", "GameLowRoleBudgetRigR4" } },
  { "RoleGeometryPatch", "switch" },
  { "Workers", "int", default = 8, range = { 2, 29 } },
  { "Benchmark", "switch" },
  { "Profile", "switch" },
  { "Behaviors", "switch" },
  { "BatchPalettes", "switch" },
  { "GroupPrimitives", "switch" },
  { "EligibilityComparison", "switch" },
  { "FullRefMatrixComparison", "switch" },
  { "VerifyFullRefMatrices", "switch" },
}

local function f3(v) if type(v) == "number" then return string.format("%.3f", v) end return "" end
local function show(v) return C.show(v) end

function M.run(input)
  local P = C.bind(M.spec, input, "test_crowd_gpu_clock")
  if P.Profile and not P.Benchmark then fail("Profile requires uncapped benchmark") end
  if P.EligibilityComparison and P.FullRefMatrixComparison then fail("Choose one isolated comparison") end
  if P.VerifyFullRefMatrices and not P.FullRefMatrixComparison then fail("Matrix verification requires matrix comparison") end
  local gpu_comparison = P.EligibilityComparison or P.FullRefMatrixComparison
  local root = C.root
  local base = C.join(root, "Saved/Crowd/GpuClockControl-" .. P.Tag)
  local control = base .. ".control.json"
  if C.exists(control) then fail("Fresh evidence tag required") end
  local selection = C.read_json(P.SelectionFile)
  local count = P.PilotCard >= 0 and 1 or C.count(selection.characters)
  if count < 1 or count > 100 then fail("Expected 1 to100 selected characters") end
  local function write_control(revision, enabled, exit)
    exit = exit or false
    local request = json.object({ { "revision", revision }, { "tag", "GpuClockControl-" .. P.Tag .. "-" .. revision }, { "seconds", P.Benchmark and 45 or 30 }, { "trace", P.Profile }, { "reset_layout", true }, { "independent_gpu_animation", enabled }, { "exit", exit } })
    if P.EligibilityComparison and not exit then request.gpu_clock_eligibility_pass = revision == 2 and 1 or 0 end
    if P.FullRefMatrixComparison and not exit then
      request.full_ref_matrix_cache = revision == 2 and 1 or 0; request.verify_full_ref_matrices = P.VerifyFullRefMatrices; request.gpu_clock_eligibility_pass = 0
    end
    local temp = control .. ".tmp"
    C.write_all_json(temp, request)
    if C.exists(control) then ok.path.copy(control, control .. ".previous") end
    ok.path.replace(temp, control)
  end
  write_control(1, gpu_comparison)
  local options = { Tag = "GpuClockControl-" .. P.Tag, StageRoot = P.StageRoot, ShaderPatchDirectory = P.ShaderPatchDirectory, SelectionFile = P.SelectionFile, PilotCard = P.PilotCard, Variant = P.Variant, RoleGeometryPatch = P.RoleGeometryPatch, GroupPrimitives = P.GroupPrimitives, ThirdPerson = true, Behaviors = P.Behaviors, AnimationHz = 60, Workers = P.Workers, CacheShadowPolicy = true, LeaderDrivenFollowers = true, OutlineLOD = true, ControlFile = control, IndependentGpuOwnClock = true, IndependentGpuBatchPalettes = P.BatchPalettes, IndependentGpuShardIndex = P.GpuShardIndex, IndependentGpuCache = P.GpuCache, IndependentGpuIdentity = P.GpuIdentity, MaxFPS = P.Benchmark and 0 or 60, ValidateOnly = true }
  -- The validate-only listing goes to Out-Null in the original (quiet = true).
  development.run(options, true)
  local launch = C.read_json(root .. "/Saved/Crowd/DevelopmentGpuClockControl-" .. P.Tag .. ".launch.json")
  local background = C.process_cpu()
  local start = C.clock()
  local owned = C.start_process(launch.binary, launch.arguments, { hide = true })
  C.set_content(base .. ".pid", owned:pid())
  local phases = {}
  local deadline = C.clock() + 480
  local okb, err = pcall(function()
    for revision = 1, 3 do
      local enabled = gpu_comparison or revision == 2
      if revision > 1 then write_control(revision, enabled) end
      local report = base .. "-" .. revision .. ".json"
      local r = nil
      repeat
        if owned:poll() then fail("Owned game exited before finishing the toggle sequence") end
        if C.clock() > deadline then fail("Owned GPU clock test timed out") end
        r = nil
        if C.exists(report) then
          local okr, v = pcall(C.read_json, report)
          if okr then r = v end
        end
        if not r or not T(r.run_finished) then C.sleep(1) end
      until r and T(r.run_finished)
      local expected_animated = C.is_null(r.animation_expected_active) and count or math.tointeger(r.animation_expected_active) or ok.util.round(r.animation_expected_active)
      if not T(r.complete) or ne(r.loaded, count) or ne(r.animated, expected_animated) or not T(r.initial_low_only_met) then fail("Selected scene failed functional checks") end
      if ne(r.independent_gpu_animation_requested, enabled) then fail("CPU/GPU toggle did not apply") end
      if enabled then
        if ne(r.independent_gpu_clock_owned_actors, count) or ne(r.independent_gpu_clock_cpu_evaluations, 0) or ne(r.independent_gpu_animation_render_fallbacks, 0) or C.le(r.independent_gpu_animation_render_passes, 0) then fail("GPU clock did not own every selected actor without CPU pose evaluation/fallback") end
      elseif ne(r.independent_gpu_clock_owned_actors, 0) then fail("CPU toggle failed to restore animation ownership") end
      if P.EligibilityComparison and ne(r.gpu_clock_eligibility_pass, revision == 2 and 1 or 0) then fail("Eligibility path switch did not apply") end
      if P.FullRefMatrixComparison then
        if ne(r.full_ref_matrix_cache, revision == 2 and 1 or 0) or T(r.full_ref_matrix_failed) or ne(r.full_ref_matrix_mismatches, 0) then fail("Full matrix cache mode or equivalence failed") end
        if revision == 2 and (C.le(r.full_ref_matrix_hits, 0) or (P.VerifyFullRefMatrices and ne(r.full_ref_matrix_verified, r.full_ref_matrix_hits))) then fail("No independently verified full-palette hits") end
      end
      local function v(x) if x == nil then return json.null end return x end
      phases[#phases + 1] = json.object({
        { "full_ref_matrix_cache", v(r.full_ref_matrix_cache) }, { "matrix_hits", v(r.full_ref_matrix_hits) }, { "matrix_misses", v(r.full_ref_matrix_misses) },
        { "matrix_verified", v(r.full_ref_matrix_verified) }, { "matrix_mismatches", v(r.full_ref_matrix_mismatches) }, { "eligibility_pass", v(r.gpu_clock_eligibility_pass) },
        { "enabled", enabled }, { "report", report }, { "frame_mean_ms", v(r.frame_mean_ms) }, { "frame_p95_ms", v(r.frame_p95_ms) },
        { "game_ms", v(r.game_thread_mean_ms) }, { "render_ms", v(r.render_thread_mean_ms) }, { "rhi_ms", v(r.rhi_thread_mean_ms) }, { "gpu_ms", v(r.gpu_mean_ms) },
        { "owned", v(r.independent_gpu_clock_owned_actors) }, { "cpu_evaluations", v(r.independent_gpu_clock_cpu_evaluations) } })
      C.out(("GPU=%s owned=%s; Frame=%s P95=%s GT=%s RT=%s RHI=%s GPU=%s ms"):format(enabled and "True" or "False", show(r.independent_gpu_clock_owned_actors), f3(r.frame_mean_ms), f3(r.frame_p95_ms), f3(r.game_thread_mean_ms), f3(r.render_thread_mean_ms), f3(r.rhi_thread_mean_ms), f3(r.gpu_mean_ms)))
    end
    write_control(4, false, true)
    local code = owned:wait(15)
    if not code then fail("Owned game did not exit after final control") end
    if code ~= 0 then fail("Owned game returned failure") end
    local duration = C.clock() - start
    local processes = {}
    for _, p in ipairs(C.background_cpu(background)) do
      processes[#processes + 1] = json.object({ { "name", p.name }, { "pid", p.pid }, { "cpu_seconds", p.cpu }, { "equivalent_cores", p.cpu / duration } })
    end
    local first, last = tonumber(phases[1].frame_mean_ms) or 0, tonumber(phases[3].frame_mean_ms) or 0
    local drift = 100 * math.abs(last - first) / first
    C.write_json(base .. ".json", json.object({
      { "passed", true }, { "workers", P.Workers }, { "variant", P.Variant }, { "role_geometry_patch", P.RoleGeometryPatch }, { "group_primitives", P.GroupPrimitives },
      { "batch_palettes", P.BatchPalettes }, { "cards", count }, { "benchmark", P.Benchmark }, { "profile", P.Profile }, { "binary", launch.binary },
      { "binary_sha256", C.sha256(launch.binary) }, { "cpu_control_drift_percent", gpu_comparison and json.null or drift }, { "baseline_control_drift_percent", drift },
      { "eligibility_comparison", P.EligibilityComparison }, { "full_ref_matrix_comparison", P.FullRefMatrixComparison }, { "verify_full_ref_matrices", P.VerifyFullRefMatrices },
      { "control_drift_gate_passed", drift <= 5 }, { "phases", json.array(phases) }, { "background_window_seconds", duration },
      { "background_processes", json.array(processes) }, { "final_120fps_acceptance", false } }), true)
  end)
  if not owned:poll() then owned:kill() end
  if not okb then error(err, 0) end
  return 0
end

function M.main()
  return M.run(C.parse_cli(M.spec, "Owned CPU/GPU/CPU toggle test of the independent GPU clock."))
end

if ok.is_main() then ok.main(M.main) end
return M
