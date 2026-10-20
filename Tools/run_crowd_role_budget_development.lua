-- 100-card grouped-model (RoleBudget) CrowdRoom play (port of
-- run_crowd_role_budget_development.ps1); cmd\CrowdRoomDevelopmentRoleBudget.cmd.
-- Release receipt: Saved/Crowd/RoleBudgetDevelopmentRelease.json.
--   Tools\lua\lua.exe Tools\run_crowd_role_budget_development.lua [--camera-resident N] [--validate-only]
local ok = require "ok"
local C = require "crowd_launch_common"
local development = require "run_crowd_development"
local T, ne, eq, fail = C.truthy, C.ne, C.eq, C.fail

local M = {}

M.spec = {
  { "SelectionFile", "string", default = "" },
  { "CameraResident", "int", default = 44, range = { 0, 99 } },
  { "ValidateOnly", "switch" },
}

function M.run(input)
  local P = C.bind(M.spec, input, "run_crowd_role_budget_development")
  local root = C.root
  local release = C.read_json(root .. "/Saved/Crowd/RoleBudgetDevelopmentRelease.json")
  local known = eq(release.variant, "GameLowRoleBudgetR1") or eq(release.variant, "GameLowRoleBudgetRigR2")
  if not T(release.passed) or ne(release.cards, 100) or not known then fail("The100-card grouped-model release is not ready") end
  if P.SelectionFile == "" then P.SelectionFile = root .. "/Saved/Crowd/GameLowRandomSelectionR1.json" end
  local tag = "RoleBudgetPlay" .. C.stamp("T")
  return development.run({
    Tag = tag, Mode = "Play", StageRoot = release.stage, ShaderPatchDirectory = release.shader_patch_directory, Variant = release.variant,
    SelectionFile = P.SelectionFile, ThirdPerson = true, CameraResident = P.CameraResident, AnimationHz = 60, Workers = 8,
    CacheShadowPolicy = true, LeaderDrivenFollowers = true, OutlineLOD = true, ShowStats = true, ValidateOnly = P.ValidateOnly,
  })
end

function M.main()
  return M.run(C.parse_cli(M.spec, "RoleBudget CrowdRoom play (CrowdRoomDevelopmentRoleBudget.cmd)."))
end

if ok.is_main() then ok.main(M.main) end
return M
