-- Submit one controlled measurement case to an already running CrowdRoom
-- (-ControlFile) and wait for its report (port of measure_crowd_case.ps1).
-- Writes Saved/Crowd/<Tag>.request.json and the control file, then validates
-- the case report Saved/Crowd/<Tag>.json against the requested modes.
--   Tools\lua\lua.exe Tools\measure_crowd_case.lua --tag <fresh> --revision <n> [--end-frame-mode 1] [--seconds 60] [--require-120]
-- Default control file: <repo>/Saved/Crowd/PerformanceControlR1.json.
local ok = require "ok"
local C = require "crowd_launch_common"
local json = ok.json
local T, ne, lt, le, fail = C.truthy, C.ne, C.lt, C.le, C.fail

local M = {}

M.spec = {
  { "Tag", "string", mandatory = true, pattern = "^[A-Za-z0-9_-]+$" },
  { "Revision", "int", mandatory = true, range = { 1, 100000 } },
  { "ControlFile", "string", default = C.join(C.root, "Saved/Crowd/PerformanceControlR1.json") },
  { "Seconds", "int", default = 60, range = { 30, 600 } },
  { "EndFrameMode", "int", default = 1, range = { 0, 2 } },
  { "RhiWidth", "int", default = 8, range = { 1, 32 } },
  { "ParallelShadows", "switch" },
  { "Trace", "switch" },
  { "BonePackingMode", "int", default = -1, range = { -1, 5 } },
  { "MinDraws", "int", default = -1, set = { -1, 64, 128, 256, 512 } },
  { "CopyDebugPose", "int", default = -1, range = { -1, 1 } },
  { "ComponentMatrixCache", "int", default = -1, range = { -1, 2 } },
  { "VerifyComponentMatrices", "switch" },
  { "WeightedPoseSeedMode", "int", default = -1, range = { -1, 1 } },
  { "ConstantKeyISPC", "int", default = -1, range = { -1, 1 } },
  { "ConstantScaleCache", "int", default = -1, range = { -1, 1 } },
  { "VerifyConstantScales", "switch" },
  { "ResetLayout", "switch" },
  { "Require120", "switch" },
}

M.columns = { "control_tag", "task_workers", "end_frame_mode", "parallel_shadow_draw", "detailed_profile", "samples", "frame_mean_ms", "frame_p95_ms", "game_thread_mean_ms", "render_thread_mean_ms", "rhi_thread_mean_ms", "gpu_mean_ms", "bone_packing_mode", "high_face_residents", "face_shadow_missing_components", "target_met" }

function M.run(input)
  local P = C.bind(M.spec, input, "measure_crowd_case")
  local saved = C.full(C.join(C.root, "Saved/Crowd"))
  local control = C.full(P.ControlFile)
  if control:lower():sub(1, #saved + 1) ~= (saved .. "\\"):lower() then fail("Control file must be inside this project Saved/Crowd") end
  local report = C.join(saved, P.Tag .. ".json")
  if C.exists(report) then fail("Use a fresh case tag") end
  local receipt = C.join(saved, P.Tag .. ".request.json")
  if C.exists(receipt) then fail("Use a fresh case tag; a request already exists") end
  if P.Trace and P.Seconds < 45 then fail("Detailed tracing requires at least 45 seconds") end
  if P.Trace and P.Require120 then fail("120 FPS acceptance requires a case without detailed tracing") end
  if P.VerifyConstantScales and (P.ConstantScaleCache ~= 1 or P.Require120) then fail("Scale equality diagnostics require cache mode 1 and cannot validate FPS") end
  if C.exists(control) then
    local previous = C.read_json(control)
    if le(P.Revision, previous.revision) then fail("Revision must increase") end
  end
  local request = json.object({
    { "revision", P.Revision }, { "tag", P.Tag }, { "seconds", P.Seconds }, { "end_frame_mode", P.EndFrameMode }, { "rhi_width", P.RhiWidth },
    { "parallel_shadows", P.ParallelShadows }, { "trace", P.Trace }, { "bone_packing_mode", P.BonePackingMode }, { "min_draws", P.MinDraws },
    { "copy_debug_pose", P.CopyDebugPose }, { "component_matrix_cache", P.ComponentMatrixCache }, { "verify_component_matrices", P.VerifyComponentMatrices },
    { "weighted_pose_seed", P.WeightedPoseSeedMode }, { "reset_layout", P.ResetLayout },
  })
  request.constant_key_lerp_ispc = P.ConstantKeyISPC
  request.constant_scale_cache = P.ConstantScaleCache; request.verify_constant_scales = P.VerifyConstantScales
  C.write_all_json(receipt, json.object({ { "submitted_utc", C.utc_o() }, { "control_file", control }, { "request", request } }))
  C.write_all_json(control, request)
  local deadline = C.clock() + 25 * 60
  while C.clock() < deadline do
    if C.exists(report) then
      local okr, r = pcall(C.read_json, report)
      if not okr then C.sleep(1); goto continue end
      if T(r.run_finished) then
        if not T(r.complete) or lt(r.samples, 120) then fail("Case did not animate all 100 residents with enough warm samples") end
        if P.BonePackingMode >= 0 and ne(r.bone_packing_mode, P.BonePackingMode) then fail("Requested bone packing mode was not applied") end
        if P.MinDraws >= 0 and ne(r.parallel_draw_min_draws, P.MinDraws) then fail("Requested command-list batch size was not applied") end
        if P.CopyDebugPose >= 0 and (not C.has(r, "skeletal_debug_pose_copy") or ne(r.skeletal_debug_pose_copy, P.CopyDebugPose) or ne(r.skeletal_debug_pose_copy_effective, P.CopyDebugPose ~= 0)) then fail("Requested game debug-pose copy mode was not applied") end
        if P.ComponentMatrixCache >= 0 and (not C.has(r, "component_matrix_cache") or ne(r.component_matrix_cache, P.ComponentMatrixCache) or ne(r.component_matrix_cache_verification, P.VerifyComponentMatrices) or T(r.component_matrix_cache_failed) or ne(r.component_matrix_cache_mismatches, 0)) then fail("Matrix cache mode/equality gate failed") end
        if P.ComponentMatrixCache >= 1 and (le(r.component_matrix_cache_hits, 0) or (P.VerifyComponentMatrices and le(r.component_matrix_cache_verified, 0))) then fail("Matrix fast path did not convert or verify any matrices") end
        if P.WeightedPoseSeedMode >= 0 and (ne(r.weighted_pose_seed_enabled, P.WeightedPoseSeedMode ~= 0) or ne(r.weighted_pose_seed_failed, 0) or (P.WeightedPoseSeedMode == 1 and lt(r.weighted_pose_seed_applied, 100))) then fail("Weighted pose seed control gate failed") end
        if P.ConstantKeyISPC >= 0 and (not C.has(r, "constant_key_lerp_ispc") or ne(r.constant_key_lerp_ispc, P.ConstantKeyISPC)) then fail("Constant-key decoder mode was not applied") end
        if P.ConstantScaleCache >= 0 and (not C.has(r, "constant_scale_cache") or ne(r.constant_scale_cache, P.ConstantScaleCache) or ne(r.constant_scale_cache_verification, P.VerifyConstantScales) or T(r.constant_scale_cache_failed) or ne(r.constant_scale_cache_mismatches, 0)) then fail("Constant scale cache mode/equality gate failed") end
        if P.ConstantScaleCache == 1 and (lt(r.constant_scale_cache_prepared, 100) or le(r.constant_scale_cache_hits, 0) or (P.VerifyConstantScales and le(r.constant_scale_cache_verified, 0))) then fail("Constant scale cache did not execute or verify") end
        if P.BonePackingMode == 5 and (le(r.gpu_bone_packing_passes, 0) or le(r.gpu_bone_packing_verified_proxies, 0) or ne(r.gpu_bone_packing_fallbacks, 0) or ne(r.parallel_bone_packing_mismatches, 0)) then fail("GPU matrix path did not execute and verify without fallback") end
        C.print_select(r, M.columns)
        if P.Require120 and not T(r.target_met) then fail("Measured mean/P95 did not meet 120 FPS") end
        return 0
      end
    end
    C.sleep(1)
    ::continue::
  end
  fail("Timed out waiting for the controlled game case")
end


function M.main()
  return M.run(C.parse_cli(M.spec, "Submit one controlled CrowdRoom measurement case and wait for its report."))
end

if ok.is_main() then ok.main(M.main) end
return M
