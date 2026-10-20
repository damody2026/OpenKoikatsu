-- Optimized CrowdRoom Development defaults (port of run_crowd_optimized.ps1);
-- cmd\CrowdRoomDevelopment.cmd. Play by default; Benchmark adds the frame series
-- and the 120 FPS gate. Everything else goes to run_crowd_development.lua.
--   Tools\lua\lua.exe Tools\run_crowd_optimized.lua [--mode Play|Benchmark] [options]
local ok = require "ok"
local C = require "crowd_launch_common"
local development = require "run_crowd_development"

local M = {}

M.spec = {
  { "Mode", "string", default = "Play", set = { "Play", "Benchmark" } },
  { "SelectionFile", "string", default = "" },
  { "SectionLODFile", "string", default = "" },
  { "StageRoot", "string", default = "D:/OpenKoikatsuCrowd/DevelopmentStage" },
  { "FocusResident", "int", default = 44, range = { -1, 99 } },
  { "CameraResident", "int", default = 44, range = { 0, 99 } },
  { "FocusOnlyShadows", "switch" },
  { "LegacyFollowerTicks", "switch" },
  { "CacheShadowPolicy", "switch" },
  -- Diagnostic option; keep the existing default until uncontended validation.
  { "SuppressPeriodicReports", "switch" },
  { "Profile", "switch" },
  { "ParallelBonePacking", "switch" },
  { "VerifyBonePacking", "switch" },
  { "ForegroundWorkers", "int", default = 0, range = { 0, 16 } },
  { "BatchSubmissions", "switch" },
  { "PresentOnSubmission", "switch" },
  { "TraceSeconds", "int", default = 20, range = { 20, 90 } },
  { "ShadowHysteresisCm", "int", default = 0, range = { 0, 200 } },
  { "Seconds", "int", default = 60, range = { 30, 600 } },
  { "MaxFPS", "int", default = 160, range = { 0, 240 } },
  { "MinDraws", "int", default = 64, set = { 64, 128, 256, 512 } },
  { "TransientAllocator", "int", default = 0, set = { 0, 1 } },
  { "TransientTextureCache", "int", default = 64, set = { 64, 256 } },
  { "Workers", "int", default = 8, set = { 8, 16 } },
  { "RHIWidth", "int", default = 8, set = { 8, 16 } },
  { "Tag", "string", default = "" },
  { "IndependentGpuCache", "string", default = "" },
  { "IndependentGpuShardIndex", "string", default = "" },
  { "IndependentGpuIdentity", "string", default = "", pattern = "^$|^[A-Fa-f0-9]{40}$" },
  { "ShaderPatchDirectory", "string", default = "" },
  { "RuntimePalettes", "switch" },
  { "ShowStats", "switch" },
  { "ValidateOnly", "switch" },
}

function M.run(input)
  local P = C.bind(M.spec, input, "run_crowd_optimized")
  local root = C.root
  if P.SelectionFile == "" then P.SelectionFile = C.join(root, "Saved/Crowd/GameLowRandomSelectionR1.json") end
  if P.SectionLODFile == "" then P.SectionLODFile = C.join(root, "Saved/Crowd/SectionBoundsHair100R4.json") end
  if P.Tag == "" then P.Tag = "Optimized" .. C.stamp("T") end
  -- Both files can be replaced for a different prepared player selection. Bounds
  -- are matched by source hash; unknown meshes keep conservative render updates.
  local options = {
    Tag = P.Tag, Mode = P.Mode, Seconds = P.Seconds, StageRoot = P.StageRoot,
    IndependentGpuCache = P.IndependentGpuCache, IndependentGpuShardIndex = P.IndependentGpuShardIndex, IndependentGpuIdentity = P.IndependentGpuIdentity, ShaderPatchDirectory = P.ShaderPatchDirectory, RuntimePalettes = P.RuntimePalettes, ShowStats = P.ShowStats, ValidateOnly = P.ValidateOnly,
    SelectionFile = P.SelectionFile, SectionLODFile = P.SectionLODFile,
    Workers = P.Workers, ForegroundWorkers = P.ForegroundWorkers, RHIWidth = P.RHIWidth, Affinity = "Default", MaxFPS = P.MaxFPS,
    ParallelBonePacking = P.ParallelBonePacking, VerifyBonePacking = P.VerifyBonePacking,
    MinDraws = P.MinDraws,
    TransientAllocator = P.TransientAllocator,
    TransientTextureCache = P.TransientTextureCache,
    AnimationHz = 60, ShadowRadius = 600, OutlineRadius = 1600, UploadNearRadius = 1200,
    Variant = "GameLowHairSafeR9", FaceLOD = true,
    ThirdPerson = true, CameraResident = P.CameraResident,
    FocusedHighOnly = true, FocusResident = P.FocusResident, FocusOnlyShadows = P.FocusOnlyShadows,
    CullRenderUploads = true, BoundsOnly = true, DisableOcclusion = true,
    StaggerPose = true, BalancedPose = true, LeaderDrivenFollowers = not P.LegacyFollowerTicks,
    CacheShadowPolicy = P.CacheShadowPolicy, ShadowHysteresisCm = P.ShadowHysteresisCm,
    SuppressPeriodicReports = P.SuppressPeriodicReports,
    Profile = P.Profile, TraceSeconds = P.TraceSeconds,
    BatchSubmissions = P.BatchSubmissions,
    PresentOnSubmission = P.PresentOnSubmission,
  }
  if P.Mode == "Benchmark" then options.FrameSeries = true; options.Require120 = true end
  return development.run(options)
end

function M.main()
  return M.run(C.parse_cli(M.spec, "Optimized CrowdRoom Development launcher (CrowdRoomDevelopment.cmd)."))
end

if ok.is_main() then ok.main(M.main) end
return M
