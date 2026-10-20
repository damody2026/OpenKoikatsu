-- Same-process A/B of crowd pose providers (port of test_crowd_bone_packing.ps1).
-- Starts one controlled Benchmark game through run_crowd_development.lua in a
-- background lua.exe (the former Start-Job), waits for the initial low-only
-- gate, then submits one measure_crowd_case.lua case per --modes entry and
-- writes Saved/Crowd/<Tag>.comparison.json.
--   Tools\lua\lua.exe Tools\test_crowd_bone_packing.lua --tag <fresh> [--modes 0,1,2,0] [--seconds 30] [...cases]
local ok = require "ok"
local C = require "crowd_launch_common"
local development = require "run_crowd_development"
local measure = require "measure_crowd_case"
local json, util = ok.json, ok.util
local T, eq, fail = C.truthy, C.eq, C.fail

local M = {}

M.spec = {
  { "Tag", "string", mandatory = true, pattern = "^[A-Za-z0-9_-]+$" },
  { "StageRoot", "string", default = "D:/OpenKoikatsuCrowd/DevelopmentStage" },
  { "SelectionFile", "string", default = "" },
  { "SectionLODFile", "string", default = "" },
  { "Workers", "int", default = 8, set = { 8, 16 } },
  { "Seconds", "int", default = 30, range = { 30, 120 } },
  { "ForegroundWorkers", "int", default = 0, range = { 0, 16 } },
  { "Modes", "int[]", default = { 0, 1, 2, 0 }, range = { 0, 5 } },
  { "MinDrawsCases", "int[]", default = {}, set = { 64, 128, 256, 512 } },
  { "CopyDebugPoseCases", "int[]", default = {}, set = { 0, 1 } },
  { "ComponentMatrixCacheCases", "int[]", default = {}, set = { 0, 1, 2 } },
  { "VerifyComponentMatrices", "switch" },
  { "WeightedPoseSeed", "string", default = "" },
  { "WeightedPoseSeedCases", "int[]", default = {}, set = { 0, 1 } },
  { "ConstantKeyISPCCases", "int[]", default = {}, set = { 0, 1 } },
  { "ConstantScaleCacheCases", "int[]", default = {}, set = { 0, 1 } },
  { "VerifyConstantScales", "switch" },
  { "Trace", "switch" },
}

local function case_value(list, i) if #list > 0 then return list[i] end return -1 end

function M.run(input)
  local P = C.bind(M.spec, input, "test_crowd_bone_packing")
  local saved = C.join(C.root, "Saved/Crowd")
  local control = C.join(saved, P.Tag .. ".control.json")
  local summary = C.join(saved, P.Tag .. ".comparison.json")
  local n = #P.Modes
  if C.exists(control) or C.exists(summary) then fail("Fresh comparison tag required") end
  if #P.MinDrawsCases > 0 and #P.MinDrawsCases ~= n then fail("One command-list batch size is required per case") end
  if #P.CopyDebugPoseCases > 0 and #P.CopyDebugPoseCases ~= n then fail("One debug-pose copy mode is required per case") end
  if #P.ComponentMatrixCacheCases > 0 and #P.ComponentMatrixCacheCases ~= n then fail("One component-matrix cache mode is required per case") end
  if #P.WeightedPoseSeedCases > 0 and (P.WeightedPoseSeed == "" or #P.WeightedPoseSeedCases ~= n) then fail("Weighted seed cases require a manifest and one mode per case") end
  if P.Trace and P.Seconds < 45 then fail("Detailed trace cases require at least 45 seconds") end
  if #P.ConstantKeyISPCCases > 0 and #P.ConstantKeyISPCCases ~= n then fail("One constant-key decoder mode is required per case") end
  if #P.ConstantScaleCacheCases > 0 and #P.ConstantScaleCacheCases ~= n then fail("One constant-scale cache mode is required per case") end
  if P.VerifyConstantScales and (#P.ConstantScaleCacheCases == 0 or util.any(P.ConstantScaleCacheCases, function(v) return v ~= 1 end)) then fail("Verification cases must explicitly enable the scale cache") end
  if P.SelectionFile == "" then P.SelectionFile = C.join(saved, "GameLowRandomSelectionR1.json") end
  if P.SectionLODFile == "" then P.SectionLODFile = C.join(saved, "SectionBoundsHair100R4.json") end
  local launch_tag = P.Tag .. "Session"
  local launch = { Tag = launch_tag, StageRoot = P.StageRoot, SelectionFile = P.SelectionFile, SectionLODFile = P.SectionLODFile, Mode = "Benchmark", ThirdPerson = true, LeaderDrivenFollowers = true, CullRenderUploads = true, BoundsOnly = true, BalancedPose = true, StaggerPose = true, DisableOcclusion = true, FocusedHighOnly = true, FocusResident = 44, CameraResident = 44, ShadowRadius = 600, UploadNearRadius = 1200, TransientAllocator = 0, Affinity = "Default", Workers = P.Workers, ForegroundWorkers = P.ForegroundWorkers, RHIWidth = 8, MaxFPS = 0, Seconds = 600, ParallelBonePacking = true, VerifyBonePacking = true, ControlFile = control }
  -- Isolate historical provider/debug/matrix experiments from the new game default.
  launch.ConstantScaleCache = #P.ConstantScaleCacheCases > 0
  launch.KeepDebugPose = #P.CopyDebugPoseCases == 0
  if P.WeightedPoseSeed ~= "" then launch.WeightedPoseSeed = C.full(P.WeightedPoseSeed) end
  -- Background launcher (Start-Job): its output is collected and printed like Receive-Job.
  local job_log = ok.path.temp(".log")
  local job_args = { ok.lua_exe, ok.root .. "/Tools/run_crowd_development.lua" }
  for _, a in ipairs(C.to_cli(development.spec, launch)) do job_args[#job_args + 1] = a end
  local job = ok.proc.popen(job_args, { stdout = job_log, stderr = "stdout" })
  local function receive()
    if job_log and ok.path.exists(job_log) then
      local text = ok.path.read_bytes(job_log)
      if text ~= "" then io.write(text); io.stdout:flush() end
      ok.path.remove(job_log, true)
    end
    job_log = nil
  end
  local function running() return job:poll() == nil end
  local rows, game_id = {}, nil
  local okb, err = pcall(function()
    local pid_file = C.join(saved, "Development" .. launch_tag .. ".pid")
    local deadline = C.clock() + 120
    while not C.exists(pid_file) do
      if not running() then receive(); fail("Controlled launch failed") end
      if C.clock() > deadline then fail("Controlled launch timed out") end
      C.sleep(0.5)
    end
    game_id = math.tointeger(tonumber(util.strip(ok.path.read_text(pid_file))))
    local initial_report = C.join(saved, "Development" .. launch_tag .. ".json")
    while true do
      if not running() then receive(); fail("Game exited during initial warm-up") end
      local ready = nil
      if C.exists(initial_report) then
        local okr, v = pcall(C.read_json, initial_report)
        if okr then ready = v end
      end
      if ready and T(ready.complete) and T(ready.initial_low_only_met) then break end
      if C.clock() > deadline then fail("Initial animation/load gate timed out") end
      C.sleep(0.5)
    end
    -- Near-detail components load serially after the initial low-only gate.
    -- Settle those caches before comparing providers in this resident scene.
    C.sleep(20)
    for revision, mode in ipairs(P.Modes) do
      local case = P.Tag .. "Case" .. revision
      measure.run({
        Tag = case, Revision = revision, ControlFile = control, Seconds = P.Seconds, BonePackingMode = mode,
        MinDraws = case_value(P.MinDrawsCases, revision), CopyDebugPose = case_value(P.CopyDebugPoseCases, revision),
        ComponentMatrixCache = case_value(P.ComponentMatrixCacheCases, revision), VerifyComponentMatrices = P.VerifyComponentMatrices,
        WeightedPoseSeedMode = case_value(P.WeightedPoseSeedCases, revision), ConstantKeyISPC = case_value(P.ConstantKeyISPCCases, revision),
        ConstantScaleCache = case_value(P.ConstantScaleCacheCases, revision), VerifyConstantScales = P.VerifyConstantScales,
        ResetLayout = true, Trace = P.Trace,
      })
      local result = C.read_json(C.join(saved, case .. ".json"))
      if not T(result.player_control) or C.ne(result.face_shadow_missing_components, 0) or C.ne(result.parallel_bone_packing_mismatches, 0) then fail("Camera/shadow/bone equality gate failed") end
      rows[#rows + 1] = result
    end
    local anchor, camera_stable = rows[1], true
    for _, row in ipairs(rows) do
      if C.ne(row.high_face_residents, anchor.high_face_residents) then camera_stable = false end
      for axis = 1, 3 do
        local a = type(row.camera_position_cm) == "table" and tonumber(row.camera_position_cm[axis]) or 0
        local b = type(anchor.camera_position_cm) == "table" and tonumber(anchor.camera_position_cm[axis]) or 0
        if math.abs(a - b) > 2 then camera_stable = false end
      end
    end
    local baseline, drift = rows[#rows], nil
    local same = true
    for _, k in ipairs({ "constant_scale_cache", "constant_scale_cache_verification", "constant_key_lerp_ispc", "weighted_pose_seed_enabled", "component_matrix_cache", "component_matrix_cache_verification", "skeletal_debug_pose_copy", "bone_packing_mode", "parallel_draw_min_draws", "end_frame_mode", "parallel_shadow_draw", "rhi_command_width" }) do
      if not eq(baseline[k], anchor[k]) then same = false end
    end
    if #rows > 1 and same and C.gt(anchor.frame_mean_ms, 0) then
      drift = 100 * math.abs((tonumber(baseline.frame_mean_ms) or 0) - anchor.frame_mean_ms) / anchor.frame_mean_ms
    end
    -- A small repeated-baseline drift is only a screening check, not proof that
    -- the host was uncontended or that an intermediate candidate caused a gain.
    local timing_rejected = drift ~= nil and drift > 10
    C.write_json(summary, json.object({
      { "note", "One process; 100 unique animated characters; reset layout/animation; 10s warm-up per case. Background contention and animated end-frame bounds may still change. Match fixed camera and near-detail population; frustum count remains diagnostic. Repeated baseline drift above 10% rejects timing comparison; a lower drift alone does not prove a benefit. No automatic adoption." },
      { "camera_and_near_population_matched", camera_stable }, { "baseline_frame_drift_percent", drift == nil and json.null or drift },
      { "timing_comparison_rejected", timing_rejected }, { "cases", json.array(rows) } }), true)
    if timing_rejected then io.stderr:write(("WARNING: Repeated baseline drift %.1f%%; timing results are diagnostic only\n"):format(drift)) end
    if not camera_stable then fail("Camera/population mismatch; keep results as rejected comparison evidence") end
  end)
  local okf, ferr = pcall(function()
    ok.path.write_bytes(control, '{"revision":100000,"exit":true}')
    -- Only stop this owned test game if its normal controlled exit fails.
    if game_id then
      local game = C.wait_pid(game_id, 15000)
      if game.exists and not game.exited then
        local expected = C.full(C.join(P.StageRoot, "Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe"))
        if C.is_null(game.path) or C.full(game.path):lower() ~= expected:lower() then fail("Owned test PID path changed; refusing termination") end
        C.kill_pid(game_id)
      end
    end
    if running() then job:wait(10) end
    if running() then job:kill() end
    receive()
  end)
  if not okf then error(ferr, 0) end
  if not okb then error(err, 0) end
  return 0
end

function M.main()
  return M.run(C.parse_cli(M.spec, "Same-process A/B of crowd pose providers in one controlled game."))
end

if ok.is_main() then ok.main(M.main) end
return M
