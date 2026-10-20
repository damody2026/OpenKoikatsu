-- Validated 100-card R4 geometry GPU CrowdRoom play (port of
-- run_crowd_role_r4_development.ps1); cmd\CrowdRoomDevelopmentRoleBudgetR4GPU.cmd.
-- Release receipt: Saved/Crowd/RoleFreshDevelopmentRelease.json.
--   Tools\lua\lua.exe Tools\run_crowd_role_r4_development.lua [--camera-resident N] [--validate-only]
local ok = require "ok"
local C = require "crowd_launch_common"
local development = require "run_crowd_development"
local T, ne, fail = C.truthy, C.ne, C.fail

local M = {}

M.spec = {
  { "SelectionFile", "string", default = "" },
  { "GpuShardIndex", "string", default = "" },
  { "CameraResident", "int", default = 44, range = { 0, 99 } },
  { "ValidateOnly", "switch" },
}

function M.run(input)
  local P = C.bind(M.spec, input, "run_crowd_role_r4_development")
  local root = C.root
  local release = C.read_json(root .. "/Saved/Crowd/RoleFreshDevelopmentRelease.json")
  if not T(release.passed) or not T(release.functional_passed) or ne(release.cards, 100) or ne(release.variant, "GameLowRoleBudgetRigR4") then
    fail("The validated100-card R4 release is not ready")
  end
  if P.SelectionFile == "" then P.SelectionFile = release.selection end
  if P.GpuShardIndex == "" then P.GpuShardIndex = release.gpu_shard_index end
  local binary = C.join(release.stage, "Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe")
  if ne(C.sha256(binary), release.binary_sha256) then fail("Validated Development binary differs") end
  local options = {
    Tag = "RoleFreshPlay_" .. C.stamp("_"),
    Mode = "Play", ValidateOnly = P.ValidateOnly, StageRoot = release.stage,
    ShaderPatchDirectory = release.shader_patch_directory, RoleGeometryPatch = true,
    SelectionFile = P.SelectionFile, IndependentGpuShardIndex = P.GpuShardIndex,
    Variant = release.variant, GroupPrimitives = true, ThirdPerson = true,
    CameraResident = P.CameraResident, AnimationHz = 60, Workers = 8, MaxFPS = 0,
    CacheShadowPolicy = true, LeaderDrivenFollowers = true, OutlineLOD = true,
    IndependentGpuOwnClock = true, IndependentGpuBatchPalettes = true, ShowStats = true,
  }
  C.out("R4 geometry, original face. GPU starts ON. F8 toggles CPU/GPU; F9 shows coverage. WASD moves; hold right mouse to rotate.")
  C.out("120FPS is a performance target; consult the release receipt for measured results.")
  return development.run(options)
end

function M.main()
  return M.run(C.parse_cli(M.spec, "Validated R4 geometry GPU CrowdRoom (CrowdRoomDevelopmentRoleBudgetR4GPU.cmd)."))
end

if ok.is_main() then ok.main(M.main) end
return M
