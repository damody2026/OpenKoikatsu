-- Experimental 100-character independent GPU clock room (port of
-- run_crowd_gpu_clock_development.ps1); cmd\CrowdRoomDevelopmentGPU.cmd and
-- cmd\CrowdRoomDevelopmentGPUClock.cmd. Verifies the experiment receipt
-- Saved/Crowd/GpuClockDevelopmentExperiment.json, then plays it through
-- run_crowd_development.lua.
--   Tools\lua\lua.exe Tools\run_crowd_gpu_clock_development.lua [--legacy-palettes] [--eligibility-pass 0|1] [--validate-only]
local ok = require "ok"
local C = require "crowd_launch_common"
local development = require "run_crowd_development"
local T, ne, fail = C.truthy, C.ne, C.fail

local M = {}

M.spec = {
  { "SelectionFile", "string", default = "" },
  { "CameraResident", "int", default = 44, range = { 0, 99 } },
  { "MaxFPS", "int", default = 0, range = { 0, 240 } },
  { "LegacyPalettes", "switch" },
  { "EligibilityPass", "int", default = -1, set = { -1, 0, 1 } },
  { "ValidateOnly", "switch" },
}

function M.run(input)
  local P = C.bind(M.spec, input, "run_crowd_gpu_clock_development")
  local root = C.root
  local release = C.read_json(root .. "/Saved/Crowd/GpuClockDevelopmentExperiment.json")
  if not T(release.functional_passed) or ne(release.cards, 100) or ne(release.variant, "GameLowRoleBudgetRigR2") then fail("The100-character independent GPU clock experiment is not verified") end
  if ne(C.sha256(release.binary), release.binary_sha256) then fail("GPU experiment executable changed") end
  if ne(C.sha1(release.shard_index), release.shard_index_sha1) then fail("GPU experiment animation index changed") end
  if P.SelectionFile == "" then P.SelectionFile = root .. "/Saved/Crowd/GameLowRandomSelectionR1.json" end
  if P.EligibilityPass < 0 and not C.is_null(release.eligibility_pass) then P.EligibilityPass = math.tointeger(release.eligibility_pass) or ok.util.round(release.eligibility_pass) end
  local tag = "GpuClockPlay" .. C.stamp("T")
  local batch = T(release.batch_palettes) and not P.LegacyPalettes
  C.out("Experimental100-character GPU clock starts ON. F8 switches CPU/GPU; F9 reports actual owned clocks and rendered GPU components. WASD moves, right mouse rotates.")
  C.out("Private skin palette batching and influence bounds are verified;120FPS and original CPU/GPU culling equivalence are not accepted. The CPU RoleBudget launcher remains the default.")
  C.out("Palette batching: " .. (batch and "True" or "False") .. ". Use --legacy-palettes to compare the previous GPU dispatch path.")
  C.out("Eligibility traversal: " .. P.EligibilityPass .. ". Console ok.CrowdGpuEligibilityPass 0/1 switches live when supported.")
  return development.run({
    Tag = tag, Mode = "Play", StageRoot = release.stage, ShaderPatchDirectory = release.shader_patch_directory, Variant = release.variant,
    SelectionFile = P.SelectionFile, ThirdPerson = true, CameraResident = P.CameraResident, AnimationHz = 60, Workers = 8,
    CacheShadowPolicy = true, LeaderDrivenFollowers = true, OutlineLOD = true, ShowStats = true, MaxFPS = P.MaxFPS,
    IndependentGpuShardIndex = release.shard_index, IndependentGpuOwnClock = true, GpuEligibilityPass = P.EligibilityPass,
    IndependentGpuBatchPalettes = batch, ValidateOnly = P.ValidateOnly,
  })
end

function M.main()
  return M.run(C.parse_cli(M.spec, "Experimental 100-character GPU clock CrowdRoom (CrowdRoomDevelopmentGPU.cmd)."))
end

if ok.is_main() then ok.main(M.main) end
return M
