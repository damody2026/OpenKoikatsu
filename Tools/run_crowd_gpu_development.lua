-- Ten-card independent-GPU CrowdRoom fixture (port of run_crowd_gpu_development.ps1);
-- cmd\CrowdRoomDevelopmentGPUMatrixBridge.cmd. Snapshots the Development host
-- binary into Saved/Crowd/GpuPlay-<hash> beside junctions to the reference
-- stage's cooked content, then starts run_crowd_optimized.lua with the GPU cache.
--   Tools\lua\lua.exe Tools\run_crowd_gpu_development.lua [--gpu-shard-index <index>] [--validate-only]
local ok = require "ok"
local C = require "crowd_launch_common"
local optimized = require "run_crowd_optimized"
local fail = C.fail

local M = {}

M.spec = {
  { "ReferenceStage", "string", default = "D:/OpenKoikatsuCrowd/DevelopmentStage" },
  { "SelectionFile", "string", default = "" },
  { "GpuShardIndex", "string", default = "" },
  { "SectionLODFile", "string", default = "" },
  { "CameraResident", "int", default = 44, range = { 0, 99 } },
  { "MaxFPS", "int", default = 160, range = { 0, 240 } },
  { "ValidateOnly", "switch" },
}

local function junction(link, target)
  local res = ok.proc.run({ "mklink", "/J", link, target }, { capture = true })
  if not res.ok then fail("Junction failed: " .. link .. " -> " .. target .. "\n" .. (res.stdout or "") .. (res.stderr or "")) end
end

function M.run(input)
  local P = C.bind(M.spec, input, "run_crowd_gpu_development")
  local root = C.root
  local binary = C.join(root, "Saved/Crowd/DevelopmentHost/Binaries/Win64/OpenKoikatsu.exe")
  local reference = C.join(C.full(P.ReferenceStage), "Windows")
  local cache = C.join(root, "Saved/Crowd/IndependentGpuPose-RuntimeBoneIdentityRealR18.json.gpuanim")
  local patch = C.join(root, "Saved/Crowd/GpuShaderPatch-ControlR2_P.pak")
  if P.SelectionFile == "" then P.SelectionFile = C.join(root, "Saved/Crowd/GameLowRandomSelectionR1.json") end
  if P.SectionLODFile == "" then P.SectionLODFile = C.join(root, "Saved/Crowd/SectionBoundsHair100R4.json") end
  local inputs = P.GpuShardIndex ~= "" and { C.full(P.GpuShardIndex) } or { cache, cache .. ".manifest" }
  for _, f in ipairs({ binary, patch, P.SelectionFile, P.SectionLODFile, C.join(reference, "Engine"), C.join(reference, "OpenKoikatsu/Content") }) do inputs[#inputs + 1] = f end
  for _, f in ipairs(inputs) do
    if not C.exists(f) then fail("Missing GPU launch input: " .. f) end
  end
  -- Keep a snapshot per binary hash. Never overwrite the stable CPU stage or a
  -- running game. Cooked assets are shared through read-only-use junctions.
  local hash = C.sha256(binary)
  local stage = C.join(root, "Saved/Crowd/GpuPlay-" .. hash:sub(1, 16))
  local bin = C.join(stage, "Windows/OpenKoikatsu/Binaries/Win64")
  if not C.exists(stage) then
    ok.path.mkdir(bin, true)
    junction(C.join(stage, "Windows/Engine"), C.join(reference, "Engine"))
    junction(C.join(stage, "Windows/OpenKoikatsu/Content"), C.join(reference, "OpenKoikatsu/Content"))
    for _, name in ipairs({ "tbb12.dll", "tbbmalloc.dll", "D3D12" }) do
      local src = C.join(reference, "OpenKoikatsu/Binaries/Win64/" .. name)
      if not C.exists(src) then fail("Cannot find path '" .. src .. "' because it does not exist.") end
      if ok.path.is_dir(src) then ok.path.copytree(src, C.join(bin, name)) else ok.path.copy(src, C.join(bin, name)) end
    end
    ok.path.copy(binary, C.join(bin, "OpenKoikatsu.exe"))
    ok.path.mkdir(C.join(stage, "ShaderPatch"))
    ok.path.copy(patch, C.join(stage, "ShaderPatch\\GpuShaderPatch-ControlR2_P.pak"))
  end
  local snapshot = C.join(bin, "OpenKoikatsu.exe")
  if not C.exists(snapshot) or C.sha256(snapshot) ~= hash then fail("GPU stage snapshot incomplete or binary differs; preserve it for inspection") end
  local staged_patch = C.join(stage, "ShaderPatch/GpuShaderPatch-ControlR2_P.pak")
  if not C.exists(staged_patch) then fail("GPU stage shader patch missing") end
  if C.sha256(staged_patch) ~= C.sha256(patch) then fail("GPU stage shader patch differs; preserve the snapshot for inspection") end
  local sharded = P.GpuShardIndex ~= ""
  local options = {
    StageRoot = stage, SelectionFile = P.SelectionFile, SectionLODFile = P.SectionLODFile,
    CameraResident = P.CameraResident, MaxFPS = P.MaxFPS,
    IndependentGpuCache = sharded and "" or cache, IndependentGpuIdentity = sharded and "" or "AEBB329376F3F6329CF90DE234675BD1492B3576", IndependentGpuShardIndex = P.GpuShardIndex,
    ShaderPatchDirectory = C.join(stage, "ShaderPatch"), RuntimePalettes = true, ShowStats = true,
  }
  C.out("GPU starts ON. F8 toggles CPU/GPU in the same scene; F9 shows actual GPU coverage. WASD moves; hold right mouse to rotate the camera.")
  C.out("This is the ten-card independent-GPU fixture; unsupported cards retain CPU, and native CPU pose is still retained.")
  if P.ValidateOnly then options.ValidateOnly = true end
  return optimized.run(options)
end

function M.main()
  return M.run(C.parse_cli(M.spec, "Ten-card independent-GPU CrowdRoom fixture (CrowdRoomDevelopmentGPUMatrixBridge.cmd)."))
end

if ok.is_main() then ok.main(M.main) end
return M
