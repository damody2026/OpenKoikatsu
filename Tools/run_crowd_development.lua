-- Cooked Development CrowdRoom launcher and evidence checker (port of
-- run_crowd_development.ps1). Play starts the game and returns; Benchmark waits
-- for the owned game, records priority/affinity/background CPU evidence and
-- validates the JSON report against every requested option.
--   Tools\lua\lua.exe Tools\run_crowd_development.lua --tag <fresh> [options]
-- Other Lua launchers call M.run{Tag=..., Mode=..., ...} with the PowerShell
-- parameter names (the former `& run_crowd_development.ps1 @taskOptions`).
local ok = require "ok"
local C = require "crowd_launch_common"
local json, util = ok.json, ok.util
local T, eq, ne, lt, le, gt, ge, count = C.truthy, C.eq, C.ne, C.lt, C.le, C.gt, C.ge, C.count
local fail = C.fail

local M = {}

local VARIANTS = { "GameLowRoleBudgetRigR4", "GameLowRoleBudgetR3", "GameLowRoleBudgetRigR2", "GameLowRoleBudgetR1", "GameLowHairSafeR9", "GameLowSurfaceSafeR8", "GameLowBindingSafeR7", "GameLowBodySafeR6", "GameLow30kR1", "GameLow10kR1", "GameLow30kR2", "GameLow30kMaskR1", "GameLowSRTR1" }

M.spec = {
  { "Tag", "string", pattern = "^[A-Za-z0-9_-]+$" },
  { "Mode", "string", default = "Benchmark", set = { "Play", "Benchmark" } },
  { "PilotCard", "int", default = -1, range = { -1, 99 } },
  { "Seconds", "int", default = 60, range = { 30, 600 } },
  { "SelectionFile", "string", default = "" },
  { "Workers", "int", default = 8, range = { 2, 29 } },
  { "ForegroundWorkers", "int", default = 0, range = { 0, 16 } },
  { "Affinity", "string", default = "Default", set = { "Default", "LargestL3" } },
  { "AnimationHz", "int", default = 60, set = { 0, 60 } },
  { "MaxFPS", "int", default = 0, range = { 0, 240 } },
  { "ProcessPriority", "string", default = "Normal", set = { "Normal", "AboveNormal" } },
  { "CullRenderUploads", "switch" },
  { "DisableOcclusion", "switch" },
  { "StaggerPose", "switch" },
  { "BalancedPose", "switch" },
  { "FrameSeries", "switch" },
  { "FocusedHighOnly", "switch" },
  { "FocusOnlyShadows", "switch" },
  { "CacheShadowPolicy", "switch" },
  { "SuppressPeriodicReports", "switch" },
  { "BatchSubmissions", "switch" },
  { "PresentOnSubmission", "switch" },
  { "ShadowHysteresisCm", "int", default = 0, range = { 0, 200 } },
  { "FocusResident", "int", default = 44, range = { -1, 99 } },
  { "GatherTasks", "int", default = 4, set = { 4, 8, 16 } },
  { "VisibilitySchedule", "int", default = 1, set = { 0, 1 } },
  { "ShadowRadius", "int", default = 1200, range = { 300, 1200 } },
  { "OutlineRadius", "int", default = 1600, range = { 0, 1600 } },
  { "UploadNearRadius", "int", default = 1800, range = { 1200, 1800 } },
  { "BoundsOnly", "switch" },
  { "RHIWidth", "int", default = 8, set = { 8, 16 } },
  { "MinDraws", "int", default = 64, set = { 64, 128, 256, 512 } },
  { "TransientAllocator", "int", default = 1, set = { 0, 1 } },
  { "TransientTextureCache", "int", default = 64, set = { 64, 256 } },
  { "Loose", "switch" },
  { "StageRoot", "string", default = "" },
  { "Require120", "switch" },
  { "DetailSwitchTest", "switch" },
  { "MovementTest", "switch" },
  { "DisableFaceLOD", "switch" },
  { "FaceLOD", "switch" },
  { "FaceLODSwitchTest", "switch" },
  { "FullOutlines", "switch" },
  { "OutlineLOD", "switch" },
  { "Profile", "switch" },
  { "ControlFile", "string", default = "" },
  { "ParallelBonePacking", "switch" },
  { "VerifyBonePacking", "switch" },
  { "AuditPoseBones", "switch" },
  { "WeightedPoseSeed", "string", default = "" },
  { "VerifyWeightedPoseSeed", "switch" },
  { "ConstantScaleCache", "switch", default = true },
  { "VerifyConstantScaleCache", "switch" },
  { "KeepDebugPose", "switch" },
  { "TraceSeconds", "int", default = 20, range = { 20, 90 } },
  { "LoadProfile", "switch" },
  { "LeaderDrivenFollowers", "switch" },
  { "HideSlots", "string", default = "", pattern = "^$|^[0-9]+(,[0-9]+)*$" },
  { "VisualPoseSamples", "switch" },
  { "FilmColorComparison", "switch" },
  { "AntiAliasing", "string", default = "TAA", set = { "FXAA", "TAA", "TSR" } },
  { "WindowWidth", "int", default = 1600, range = { 640, 7680 } },
  { "WindowHeight", "int", default = 1000, range = { 480, 4320 } },
  { "PilotViewYaw", "double", default = 0.0, range = { -180, 180 } },
  { "PilotTargetHeight", "double", default = 145.0, range = { 0, 300 } },
  { "FrontCamera", "switch" },
  { "FaceDistance", "double", default = 180.0, range = { -2000, 2000 } },
  { "ThirdPerson", "switch", negatable = true },
  { "PlayerControl", "switch" },
  { "Behaviors", "switch" },
  { "BehaviorProfile", "string", default = "", pattern = "^$|^[A-Za-z0-9_]+$" },
  { "CameraResident", "int", default = 44, range = { 0, 99 } },
  { "SectionLODFile", "string", default = "" },
  { "SectionLODPixels", "double", default = 2.0, range = { 0.1, 8 } },
  { "SectionLODSwitchTest", "switch" },
  { "BoneBuffers", "string", default = "Scene", set = { "Scene", "VertexFactory" } },
  { "Variant", "string", default = "GameLowHairSafeR9", set = VARIANTS },
  { "IndependentGpuCache", "string", default = "" },
  { "IndependentGpuShardIndex", "string", default = "" },
  { "IndependentGpuIdentity", "string", default = "", pattern = "^$|^[A-Fa-f0-9]{40}$" },
  { "ShaderPatchDirectory", "string", default = "" },
  { "GpuClockVisualCompare", "switch" },
  { "VerifyIndependentGpu", "switch" },
  { "IndependentGpuOwnClock", "switch" },
  { "IndependentGpuBatchPalettes", "switch" },
  { "RuntimePalettes", "switch" },
  { "GroupPrimitives", "switch" },
  { "RoleGeometryPatch", "switch" },
  { "GpuEligibilityPass", "int", default = -1, set = { -1, 0, 1 } },
  { "FullRefMatrixCache", "int", default = -1, set = { -1, 0, 1 } },
  { "ShowStats", "switch" },
  { "ValidateOnly", "switch" },
}

local AA = { FXAA = 1, TAA = 2, TSR = 4 }
local function b01(v) return v and 1 or 0 end
local function inv(v) return C.num(v) end

-- Rewrite every -ExecCmds argument with fn.
local function map_exec(args, fn, exact_prefix)
  for i, a in ipairs(args) do
    local hit
    if exact_prefix then hit = a:sub(1, 11) == '-ExecCmds="' else hit = a:lower():sub(1, 10) == "-execcmds=" end
    if hit then args[i] = fn(a) end
  end
end
local function insert_before_async(args, text)
  map_exec(args, function(a) return util.replace(a, "AllowAsyncRenderThreadUpdates 1", text .. ",AllowAsyncRenderThreadUpdates 1") end)
end

-- Run with options keyed by the PowerShell parameter names. quiet drops the
-- --validate-only listing (callers that piped it to Out-Null).
function M.run(input, quiet)
  local P, bound = C.bind(M.spec, input, "run_crowd_development")
  if P.ControlFile ~= "" and P.Mode == "Play" then fail("Controlled measurement requires Benchmark mode for process monitoring") end
  if not P.Tag or P.Tag == "" then fail("Fresh tag required") end
  if P.LoadProfile then fail("Loading trace is disabled: this UE runtime crashes at exit when recording asset loads. Per-card loading counters are always written to the report; use -Profile for a verified warm CPU/GPU trace.") end
  if P.Profile and P.Seconds < 45 then fail("Warm trace requires at least 45 seconds") end
  if P.Require120 and P.PilotCard >= 0 then fail("A pilot cannot validate 100-person performance") end
  if P.Require120 and (P.GpuClockVisualCompare or P.VerifyIndependentGpu) then fail("GPU diagnostics cannot validate FPS") end
  if P.Require120 and P.VerifyConstantScaleCache then fail("Scale equality diagnostics cannot validate FPS") end
  local root = C.root
  local stage = P.Loose and "StagedLoose" or "StagedBuilds"
  local binary = C.join(root, "Saved/Crowd/DevelopmentHost/Saved/" .. stage .. "/Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe")
  if P.StageRoot ~= "" then binary = C.join(C.full(P.StageRoot), "Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe") end
  if not C.exists(binary) then fail("Stage the Development game first") end
  local base = C.join(root, "Saved/Crowd/Development" .. P.Tag)
  if C.exists(base .. ".log") then fail("Fresh evidence tag required") end
  local args = { "/Game/Scenes/CrowdRoom/CrowdRoom", "-windowed",
    "-ini:Engine:[/Script/Engine.UserInterfaceSettings]:bAllowHighDPIInGameMode=True,[/Script/Engine.UserInterfaceSettings]:bAllowHighDpiWhenUnattended=True",
    "-ResX=" .. P.WindowWidth, "-ResY=" .. P.WindowHeight, "-nosound", "-NoSplash",
    "-corelimit=" .. (P.Workers + (P.Workers >= 16 and 2 or 1)), "-oversubscriptionratio=1",
    "-CrowdQualityVariant=" .. P.Variant, '-abslog="' .. base .. '.log"', '-CrowdReport="' .. base .. '.json"',
    '-ExecCmds="EnableHighDPIAwareness,r.VSync 0,t.MaxFPS ' .. P.MaxFPS .. ",r.RHICmdWidth " .. P.RHIWidth .. ",r.RHICmdMinDrawsPerParallelCmdList " .. P.MinDraws .. ",AllowAsyncRenderThreadUpdates 1,r.Visibility.DynamicMeshElements.NumMainViewTasks " .. P.GatherTasks .. ",r.Visibility.TaskSchedule " .. P.VisibilitySchedule .. '"' }
  local function add(...) for _, a in ipairs({ ... }) do args[#args + 1] = a end end
  if P.GpuEligibilityPass >= 0 then
    map_exec(args, function(a) return a:sub(1, -2) .. ",ok.CrowdGpuEligibilityPass " .. P.GpuEligibilityPass .. '"' end, true)
  end
  if P.FullRefMatrixCache >= 0 then
    map_exec(args, function(a) return a:sub(1, -2) .. ",r.SkeletalMesh.ReuseRefToLocalMatrices " .. P.FullRefMatrixCache .. '"' end, true)
  end
  if P.GroupPrimitives then add("-CrowdGroupPrimitives"); P.RuntimePalettes = true end
  local has_cache, has_shard, has_identity = P.IndependentGpuCache ~= "", P.IndependentGpuShardIndex ~= "", P.IndependentGpuIdentity ~= ""
  if has_shard and (has_cache or has_identity) then fail("Choose GPU cache or shard index") end
  if has_shard then
    if P.ParallelBonePacking then fail("Shard index conflicts with bone packing experiment") end
    local index = C.full(P.IndependentGpuShardIndex)
    if not C.exists(index) then fail("GPU shard index missing") end
    add("-CrowdIndependentGpuAnimation", '-CrowdIndependentGpuShardIndex="' .. index .. '"', "-CrowdIndependentGpuShardIndexIdentity=" .. C.sha1(index))
  end
  if has_cache ~= has_identity then fail("GPU cache and trusted identity must be supplied together") end
  if has_cache then
    local gpu_cache = C.full(P.IndependentGpuCache)
    for _, f in ipairs({ gpu_cache, gpu_cache .. ".manifest" }) do if not C.exists(f) then fail("Missing GPU input: " .. f) end end
    if P.ParallelBonePacking then fail("Independent GPU animation cannot use the separate bone-packing experiment") end
    add("-CrowdIndependentGpuAnimation", '-CrowdIndependentGpuCache="' .. gpu_cache .. '"', "-CrowdIndependentGpuIdentity=" .. P.IndependentGpuIdentity)
  end
  if P.GpuClockVisualCompare then
    if P.PilotCard < 0 or P.VerifyIndependentGpu then fail("Whole-character comparison requires one pilot and no readback verification") end
    add("-CrowdGpuClockVisualCompare", '-CrowdVisualOutput="' .. base .. '.comparison"')
  end
  if P.VerifyIndependentGpu then
    if P.IndependentGpuBatchPalettes then add("-CrowdIndependentGpuBatchPalettes") end
    if P.IndependentGpuOwnClock then fail("Matrix verification requires the CPU oracle; disable clock ownership") end
    add("-CrowdIndependentGpuVerify")
  end
  if P.IndependentGpuBatchPalettes then add("-CrowdIndependentGpuBatchPalettes") end
  if P.IndependentGpuOwnClock then
    if not (has_cache or has_shard) then fail("GPU clock ownership requires an independent animation cache") end
    add("-CrowdIndependentGpuOwnClock")
  end
  if P.RuntimePalettes then
    if not (has_cache or has_shard) then fail("Runtime palettes require a GPU cache") end
    add("-CrowdIndependentGpuRuntimePalettes")
  end
  if P.RoleGeometryPatch then
    if P.ShaderPatchDirectory == "" then fail("Geometry shader library requires a cooked patch directory") end
    add("-CrowdRoleGeometryPatch")
  end
  if P.ShaderPatchDirectory ~= "" then
    if not ok.path.is_dir(P.ShaderPatchDirectory) then fail("Shader patch directory missing") end
    add('-pakdir="' .. C.full(P.ShaderPatchDirectory) .. '"')
  end
  if P.ShowStats then map_exec(args, function(a) return (a:gsub('"$', ' ,stat unit"')) end) end
  insert_before_async(args, "r.RDG.TransientAllocator " .. P.TransientAllocator)
  insert_before_async(args, "r.RHICmd.ParallelTranslate.BatchSubmissions " .. b01(P.BatchSubmissions))
  insert_before_async(args, "r.D3D12.PresentOnSubmissionThread " .. b01(P.PresentOnSubmission))
  -- The heap cache latches its initializer before first rendering. ExecCmds is
  -- too late for this setting; device-profile command-line overrides precede RHIInit.
  if P.TransientTextureCache ~= 64 then add("-dpcvars=RHI.TransientAllocator.TextureCacheSize=" .. P.TransientTextureCache) end
  if P.DisableOcclusion then
    map_exec(args, function(a) return util.replace(a, "AllowAsyncRenderThreadUpdates 1", "AllowAsyncRenderThreadUpdates 1,r.AllowOcclusionQueries 0") end)
  end
  add("-CrowdAnimationHz=" .. P.AnimationHz, "-CrowdShadowRadius=" .. P.ShadowRadius, "-CrowdOutlineRadius=" .. P.OutlineRadius, "-CrowdUploadNearRadius=" .. P.UploadNearRadius)
  if P.FocusedHighOnly then add("-CrowdFocusedHighOnly", "-CrowdFocusResident=" .. P.FocusResident) end
  if P.ShadowHysteresisCm > 0 and not P.CacheShadowPolicy then fail("Shadow hysteresis requires the cache policy") end
  if P.ShadowHysteresisCm > P.ShadowRadius / 3 then fail("Shadow hysteresis must not exceed one third of radius") end
  add("-CrowdShadowHysteresisCm=" .. P.ShadowHysteresisCm)
  if P.CacheShadowPolicy then add("-CrowdCacheShadowPolicy") end
  if P.SuppressPeriodicReports then add("-CrowdSuppressPeriodicReports") end
  add("-CrowdTraceSeconds=" .. P.TraceSeconds)
  if P.FocusOnlyShadows then add("-CrowdFocusOnlyShadows") end
  if P.BalancedPose then add("-CrowdBalancedPose") end
  if P.FrameSeries then
    if P.Mode ~= "Benchmark" then fail("Frame series requires a bounded benchmark") end
    add('-CrowdFrameSeries="' .. base .. '.frames.csv"')
  end
  if P.StaggerPose then add("-CrowdStaggerPose") end
  if P.CullRenderUploads then
    if P.SectionLODFile == "" then fail("Upload culling needs conservative bounds metadata") end
    add("-CrowdCullRenderUploads")
  end
  if P.BoundsOnly then add("-CrowdBoundsOnly") end
  if not bound.ThirdPerson and P.PilotCard < 0 then P.ThirdPerson = true end
  if P.DetailSwitchTest then
    add("-CrowdDisableFaceLOD")
    if P.PilotCard < 0 then fail("Detail switch validation currently requires a pilot card") end
    add("-CrowdDetailSwitchTest")
  end
  if P.ThirdPerson then
    if P.FrontCamera or P.DetailSwitchTest or P.SectionLODSwitchTest then fail("Third-person tests need an independent camera path") end
    add("-CrowdThirdPerson", "-CrowdCameraResident=" .. P.CameraResident)
  end
  if P.ThirdPerson and (P.Mode == "Play" or P.ControlFile ~= "" or P.PlayerControl) then add("-CrowdPlayerControl") end
  if P.Behaviors then
    if P.PilotCard >= 0 or P.GpuClockVisualCompare or P.Require120 then fail("Behavior demonstration is separate from fixed-pose and walking-only FPS acceptance") end
    add("-CrowdBehaviors")
    if P.BehaviorProfile ~= "" then add("-CrowdBehaviorProfile=" .. P.BehaviorProfile) end
  end
  if P.BehaviorProfile ~= "" and not P.Behaviors then fail("BehaviorProfile requires Behaviors") end
  if P.MovementTest then
    if not P.ThirdPerson or P.Mode ~= "Benchmark" or P.Require120 then fail("Movement test requires third-person bounded functional mode without FPS acceptance") end
    add("-CrowdMovementTest")
  end
  if P.FrontCamera then
    if P.PilotCard < 0 or P.DetailSwitchTest then fail("Front-camera visual comparison requires a single low-only pilot") end
    add("-CrowdPilotFrontCamera", "-CrowdDisableHighDetail", "-CrowdPilotFaceDistance=" .. inv(P.FaceDistance), "-CrowdPilotViewYaw=" .. inv(P.PilotViewYaw), "-CrowdPilotTargetHeight=" .. inv(P.PilotTargetHeight))
  end
  if P.SectionLODFile ~= "" then
    local section = C.full(P.SectionLODFile)
    if not C.exists(section) then fail("Section LOD metadata missing") end
    add('-CrowdSectionLODFile="' .. section .. '"', "-CrowdSectionLODPixels=" .. inv(P.SectionLODPixels))
  end
  if P.SectionLODSwitchTest then
    if P.PilotCard < 0 or P.SectionLODFile == "" or P.FrontCamera or P.DetailSwitchTest then fail("Section LOD restoration requires an isolated low pilot") end
    add("-CrowdSectionLODSwitchTest", "-CrowdDisableHighDetail")
  end
  if P.BoneBuffers == "VertexFactory" then add("-CrowdVertexFactoryBoneBuffers") end
  if P.LeaderDrivenFollowers then add("-CrowdLeaderDrivenFollowers") end
  if P.HideSlots ~= "" then
    if P.PilotCard < 0 then fail("Material isolation is pilot-only") end
    add("-CrowdPilotHideSlots=" .. P.HideSlots)
  end
  add("-CrowdAntiAliasing=" .. AA[P.AntiAliasing])
  if P.FilmColorComparison then add("-CrowdFilmColorComparison") end
  if P.VisualPoseSamples then
    if P.PilotCard < 0 then fail("Visual pose samples require a pilot") end
    add("-CrowdVisualPoseSamples")
  end
  if P.DisableFaceLOD then
    if P.FaceLOD or P.FaceLODSwitchTest then fail("Face LOD diagnostics cannot be disabled") end
    add("-CrowdDisableFaceLOD")
  end
  if P.FaceLOD then add("-CrowdFaceLOD") end
  if P.FaceLODSwitchTest then add("-CrowdFaceLODSwitchTest") end
  if P.FullOutlines then add("-CrowdFullOutlines") end
  if P.OutlineLOD then add("-CrowdOutlineLOD") end
  if P.Profile then add("-statnamedevents", '-CrowdTrace="' .. base .. '.utrace"') end
  if P.LoadProfile then add("-CrowdLoadProfile", "-statnamedevents", '-CrowdLoadingTrace="' .. base .. '.utrace"') end
  if P.PilotCard >= 0 then add("-CrowdPilotIndex=" .. P.PilotCard) end
  if P.SelectionFile ~= "" then
    local selection = C.full(P.SelectionFile)
    if not C.exists(selection) then fail("Selection file missing") end
    add('-CrowdSelection="' .. selection .. '"')
  end
  if P.Mode == "Benchmark" then add("-unattended", "-CrowdRunSeconds=" .. P.Seconds, '-CrowdScreenshot="' .. base .. '.png"') end
  if P.VerifyBonePacking and not P.ParallelBonePacking then fail("Verification requires parallel bone packing") end
  if P.ControlFile ~= "" then
    local control = C.full(P.ControlFile)
    local control_root = C.full(C.join(root, "Saved/Crowd"))
    if control:lower():sub(1, #control_root + 1) ~= (control_root .. "\\"):lower() then fail("Control file must be inside Saved/Crowd") end
    add('-CrowdControl="' .. control .. '"')
  end
  if P.ParallelBonePacking then add("-CrowdParallelBonePacking") end
  if P.VerifyBonePacking then add("-CrowdVerifyBonePacking") end
  if P.AuditPoseBones then
    if P.Require120 then fail("Pose-weight diagnostic scans are not FPS acceptance") end
    add("-CrowdAuditPoseBones")
  end
  if P.VerifyWeightedPoseSeed and P.WeightedPoseSeed == "" then fail("Verification requires a weighted-pose manifest") end
  if P.WeightedPoseSeed ~= "" then
    if not C.exists(P.WeightedPoseSeed) then fail("Missing weighted-pose manifest") end
    add('-CrowdWeightedPoseSeed="' .. C.full(P.WeightedPoseSeed) .. '"')
  end
  if P.VerifyWeightedPoseSeed then add("-CrowdVerifyWeightedPoseSeed") end
  if P.VerifyConstantScaleCache and not P.ConstantScaleCache then fail("Scale verification requires cache registration") end
  if P.ConstantScaleCache then add("-CrowdConstantScaleCache") else add("-CrowdDisableConstantScaleCache") end
  if P.VerifyConstantScaleCache then add("-CrowdVerifyConstantScaleCache") end
  if P.KeepDebugPose then add("-CrowdKeepDebugPose") end
  if P.ForegroundWorkers > 0 then
    if P.ForegroundWorkers >= P.Workers then fail("Reserve at least one background worker") end
    add("-foregroundworkers=" .. P.ForegroundWorkers)
  end
  local cache = nil
  if P.Affinity == "LargestL3" then
    local caches = require("get_crowd_cpu_cache").read()
    local other_group = false
    for _, c in ipairs(caches) do if c.Group ~= 0 then other_group = true end end
    if #caches == 0 or other_group then fail("LargestL3 trial requires a single Windows processor group") end
    cache = util.sorted(caches, function(c) return c.CacheBytes end, true)[1]
    if not T(cache.Mask) or math.type(cache.Mask) ~= "integer" or cache.Mask < 0 then fail("Cache affinity mask unsupported") end
    if cache.LogicalProcessors <= 32 and cache.Mask == (1 << cache.LogicalProcessors) - 1 then add("-processaffinity=" .. cache.LogicalProcessors) end
  end
  local background_start, background_started = {}, C.clock()
  if P.Mode == "Benchmark" then background_start = C.process_cpu() end
  add("-CrowdLaunchUnixMs=" .. C.unix_ms())
  local cache_json = json.null
  if cache then cache_json = json.object({ { "CacheBytes", cache.CacheBytes }, { "Mask", cache.Mask }, { "Group", cache.Group }, { "LogicalProcessors", cache.LogicalProcessors } }) end
  C.write_json(base .. ".launch.json", json.object({
    { "configuration", "Development" }, { "cooked", true }, { "pilot", P.PilotCard }, { "variant", P.Variant },
    { "binary", binary }, { "arguments", json.array(args) }, { "affinity", P.Affinity }, { "cache", cache_json },
    { "max_fps", P.MaxFPS }, { "process_priority", P.ProcessPriority } }), true)
  if P.ValidateOnly then
    if not quiet then
      C.out("Validated launch only: " .. binary)
      for _, a in ipairs(args) do C.out(a) end
    end
    return 0
  end
  local owned = C.start_process(binary, args, { hide = P.Mode ~= "Play" })
  local pid = owned:pid()
  if cache then
    local actual = C.affinity(pid, cache.Mask)
    C.write_json(base .. ".affinity.json", json.object({ { "pid", pid }, { "requested_mask", cache.Mask }, { "actual_mask", actual }, { "cache_bytes", cache.CacheBytes } }))
    if actual ~= cache.Mask then fail("Owned game affinity was not applied") end
  end
  C.priority(pid, P.ProcessPriority)
  C.set_content(base .. ".pid", pid)
  if P.Mode == "Play" then C.out("Development game PID=" .. pid); return 0 end
  local priority_changes, late_priority = 0, false
  local repins, late_affinity = 0, false
  local stable_at = C.clock() + 10
  local exit_code
  while true do
    exit_code = owned:wait(1)
    if exit_code then break end
    if P.ProcessPriority == "AboveNormal" then
      local okp, err = pcall(function()
        if C.priority(pid) ~= "AboveNormal" then
          priority_changes = priority_changes + 1
          if C.clock() > stable_at then late_priority = true end
          C.priority(pid, "AboveNormal")
        end
      end)
      if not okp and not owned:poll() then error(err, 0) end
    end
    if cache then
      local oka, err = pcall(function()
        if C.affinity(pid) ~= cache.Mask then
          repins = repins + 1
          if C.clock() > stable_at then late_affinity = true end
          C.affinity(pid, cache.Mask)
        end
      end)
      if not oka and not owned:poll() then error(err, 0) end
    end
  end
  if cache then
    C.write_json(base .. ".affinity.final.json", json.object({ { "pid", pid }, { "requested_mask", cache.Mask }, { "cache_bytes", cache.CacheBytes }, { "repins", repins }, { "late_change", late_affinity } }))
  end
  C.write_json(base .. ".priority.json", json.object({ { "requested", P.ProcessPriority }, { "startup_changes", priority_changes }, { "late_change", late_priority } }))
  local duration = C.clock() - background_started
  local rows = util.sorted(C.background_cpu(background_start), function(r) return r.cpu end, true)
  local samples = {}
  for _, r in ipairs(rows) do
    samples[#samples + 1] = json.object({ { "pid", r.pid }, { "name", r.name }, { "cpu_seconds", r.cpu }, { "equivalent_cores", r.cpu / duration } })
  end
  C.write_json(base .. ".background-cpu.json", json.object({ { "window_seconds", duration },
    { "note", "Whole launch-to-exit window; surviving pre-existing processes only, excludes game. CPU cores are average work, not proof of per-frame contention." },
    { "processes", json.array(samples) } }))
  C.set_content(base .. ".exit.txt", exit_code)
  if exit_code ~= 0 then fail("Development game failed: " .. base .. ".log") end
  if P.ControlFile ~= "" then C.out("Controlled session exited; use the individual controlled case reports for acceptance"); return 0 end
  local r = C.read_json(base .. ".json")
  if not T(r.complete) or not T(r.initial_low_only_met) or not T(r.run_finished) or not T(r.all_warm_frames_retained) or lt(r.samples, 10) then fail("Cooked game did not pass animation and initial low-only checks") end
  if ne(r.bone_transform_allocation_mode, P.BoneBuffers == "Scene" and 1 or 0) then fail("Requested bone-buffer allocation mode was not applied") end
  if P.LeaderDrivenFollowers and (not T(r.leader_driven_followers) or lt(r.leader_driven_follower_parts, 1) or T(r.animation_rate_lod)) then fail("Follower optimization did not preserve the configured update path") end
  if P.FaceLODSwitchTest and (not T(r.face_round_trip_met) or not T(r.body_stays_low)) then fail("Face LOD round trip failed") end
  if P.DetailSwitchTest and not T(r.detail_switch_test_met) then fail("High/low round-trip or animation phase validation failed") end
  if P.PilotCard < 0 and (ne(r.loaded, 100) or ne(r.distinct_source_hashes, 100)) then fail("100 distinct cards required") end
  if P.AuditPoseBones then
    local audit = r.pose_bone_audit
    if not T(audit) or ne(count(audit.leaders), r.pose_evaluators) or ne(audit.render_bones_not_required, 0) or ne(audit.unmapped_palette_entries, 0) then fail("Pose bone audit missing leaders or detected required/mapping gaps") end
    if ne(audit.fps_acceptance_eligible, false) then fail("Diagnostic weight scan must not qualify as FPS acceptance") end
  end
  C.print_select(r, { "loaded", "animated", "initial_low_only_met", "load_process_seconds", "frame_mean_ms", "frame_p95_ms", "target_met" })
  if P.SectionLODSwitchTest and not T(r.section_lod_switch_test_met) then fail("Section LOD near restoration failed") end
  if P.SectionLODFile ~= "" and not P.BoundsOnly and (lt(r.section_lod_matched_meshes, 1) or T(r.animation_rate_lod)) then fail("Section LOD metadata did not match animated low meshes") end
  if P.MovementTest and (not T(r.player_control) or not T(r.movement_test_met)) then fail("WASD movement, idle stability or diagonal speed validation failed") end
  if P.ThirdPerson and P.Mode == "Play" and not T(r.player_control) then fail("Third-person player control missing") end
  if P.ThirdPerson and ne(r.camera_mode, "third_person_follow") then fail("Requested third-person camera not applied") end
  if ne(r.task_workers, P.Workers) then fail("Requested worker count differs from actual: " .. C.show(r.task_workers)) end
  if P.ParallelBonePacking and (not T(r.parallel_bone_packing_installed) or lt(r.parallel_bone_packing_passes, 1)) then fail("Parallel bone packing did not run") end
  if P.VerifyBonePacking and (not T(r.parallel_bone_packing_verified) or ne(r.parallel_bone_packing_mismatches, 0)) then fail("Bone packing byte equality failed") end
  if P.WeightedPoseSeed ~= "" and (ne(r.weighted_pose_seed_failed, 0) or lt(r.loaded, 1) or lt(r.weighted_pose_seed_applied, r.loaded)) then fail("Weighted seed setup failed") end
  if P.VerifyWeightedPoseSeed and lt(r.weighted_pose_seed_verified, r.loaded) then fail("Weighted seed retained-bone matrix verification failed") end
  if P.ConstantScaleCache and (lt(r.constant_scale_cache_prepared, r.loaded) or le(r.constant_scale_cache_hits, 0) or T(r.constant_scale_cache_failed) or ne(r.constant_scale_cache_mismatches, 0)) then fail("Constant scale cache failed setup or equality") end
  if P.VerifyConstantScaleCache and le(r.constant_scale_cache_verified, 0) then fail("Constant scale verification did not run") end
  if ge(r.skeletal_debug_pose_copy, 0) and (ne(r.skeletal_debug_pose_copy, b01(P.KeepDebugPose)) or ne(r.skeletal_debug_pose_copy_effective, P.KeepDebugPose)) then fail("Gameplay debug copy policy was not applied") end
  if ne(r.frame_rate_limit, P.MaxFPS) then fail("Requested frame limit was not applied") end
  if ne(r.anti_aliasing_method, AA[P.AntiAliasing]) or ne(r.screen_percentage, 100) or ne(r.secondary_screen_percentage, 100) or ne(r.dynamic_resolution_mode, 0) or lt(r.viewport_width, 1) or lt(r.viewport_height, 1) then fail("Native viewport resolution / anti-aliasing configuration was not applied") end
  if ne(r.animation_pose_hz, P.AnimationHz) or ne(r.rhi_command_width, P.RHIWidth) then fail("Animation or RHI width configuration did not apply") end
  if ne(r.parallel_draw_min_draws, P.MinDraws) then fail("Parallel draw batch configuration did not apply") end
  if not C.file_contains(base .. ".log", 'r.RDG.TransientAllocator = "' .. P.TransientAllocator .. '"') then fail("Transient allocator configuration was not acknowledged") end
  if ne(r.rdg_transient_allocator, P.TransientAllocator) then fail("Reported transient allocator differs from requested mode") end
  if ne(r.present_on_submission_thread, b01(P.PresentOnSubmission)) then fail("Present submission-thread configuration did not apply") end
  if ne(r.rhi_batch_submissions, b01(P.BatchSubmissions)) then fail("RHI submission batching configuration did not apply") end
  if ne(r.rhi_transient_texture_cache, P.TransientTextureCache) then fail("Transient texture cache configuration did not apply") end
  if ne(r.periodic_reports_suppressed, P.SuppressPeriodicReports) then fail("Periodic report configuration did not apply") end
  if ne(r.trace_duration_seconds, P.TraceSeconds) then fail("Trace duration configuration did not apply") end
  if P.DisableOcclusion and T(r.hardware_occlusion_queries) then fail("Hardware occlusion was not disabled") end
  if P.CullRenderUploads and (not T(r.render_upload_culling) or lt(r.render_upload_bound_skins, 1)) then fail("Upload policy did not bind low meshes") end
  if P.StaggerPose and not T(r.stagger_pose_evaluation) then fail("Pose staggering was not applied") end
  if ne(r.gather_main_view_tasks, P.GatherTasks) or ne(r.outline_radius_cm, P.OutlineRadius) or ne(r.shadow_radius_cm, P.ShadowRadius) or ne(r.upload_near_radius_cm, P.UploadNearRadius) then fail("Render configuration did not apply") end
  if P.FocusedHighOnly and (not T(r.focused_high_only) or ne(r.high_detail_focus, P.FocusResident) or gt(r.high_detail_residents, 1)) then fail("Focused high-detail configuration did not apply") end
  if late_priority then fail("Owned game priority changed during measurement; reject evidence") end
  if late_affinity then fail("Cache affinity changed during the measurement; reject this evidence") end
  if P.BalancedPose and (not T(r.balanced_pose_evaluation) or le(r.pose_balance_min_bones, 0)) then fail("Pose work balancing was not applied") end
  if P.FrameSeries and not T(r.frame_series_written) then fail("Frame time series missing") end
  if P.Require120 and not T(r.target_met) then fail("Cooked Development has not met 120 FPS") end
  if gt(r.high_face_residents, 0) and (not T(r.face_shadow_policy_met) or ne(r.face_shadow_missing_components, 0)) then fail("Visible near-detail residents are missing current-mesh shadows") end
  if P.FocusOnlyShadows and not T(r.focus_only_shadows) then fail("Focus shadow policy was not applied") end
  if ne(r.shadow_policy_cached, P.CacheShadowPolicy) or ne(r.shadow_hysteresis_cm, P.ShadowHysteresisCm) then fail("Shadow policy configuration did not apply") end
  return 0
end

function M.main()
  return M.run(C.parse_cli(M.spec, "Cooked Development CrowdRoom launcher (Play) / benchmark with report validation."))
end

if ok.is_main() then ok.main(M.main) end
return M
