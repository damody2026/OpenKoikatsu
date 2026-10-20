-- Grouped-primitive GPU CrowdRoom play (port of run_crowd_grouped_development.ps1);
-- cmd\CrowdRoomDevelopmentGPUGrouped.cmd. --baseline keeps ungrouped primitives.
--   Tools\lua\lua.exe Tools\run_crowd_grouped_development.lua [--baseline] [--validate-only]
local ok = require "ok"
local C = require "crowd_launch_common"
local development = require "run_crowd_development"

local M = {}

M.spec = {
  { "Baseline", "switch" },
  { "ValidateOnly", "switch" },
  { "SelectionFile", "string", default = "" },
  { "GpuShardIndex", "string", default = "" },
}

function M.run(input)
  local P = C.bind(M.spec, input, "run_crowd_grouped_development")
  local root = C.root
  if P.SelectionFile == "" then P.SelectionFile = C.join(root, "Saved/Crowd/GameLowRandomSelectionR1.json") end
  if P.GpuShardIndex == "" then P.GpuShardIndex = C.join(root, "Saved/Crowd/RoleRigGpuShardsR151/selection-index.json") end
  return development.run({
    Tag = "GroupedPlay_" .. (P.Baseline and "Baseline_" or "Grouped_") .. C.stamp("_"),
    Mode = "Play", ValidateOnly = P.ValidateOnly, StageRoot = C.join(root, "Saved/Crowd/GroupSafetyR290.stage"),
    ShaderPatchDirectory = C.join(root, "Saved/Crowd/PaletteBatchR182.patch"),
    SelectionFile = P.SelectionFile, IndependentGpuShardIndex = P.GpuShardIndex,
    Variant = "GameLowRoleBudgetRigR2", GroupPrimitives = not P.Baseline, ThirdPerson = true,
    AnimationHz = 60, Workers = 8, MaxFPS = 0, CacheShadowPolicy = true,
    LeaderDrivenFollowers = true, OutlineLOD = true, IndependentGpuOwnClock = true,
    IndependentGpuBatchPalettes = true, ShowStats = true,
  })
end

function M.main()
  return M.run(C.parse_cli(M.spec, "Grouped-primitive GPU CrowdRoom play (CrowdRoomDevelopmentGPUGrouped.cmd)."))
end

if ok.is_main() then ok.main(M.main) end
return M
