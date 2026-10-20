-- Editable-mesh pose-bone audit in an isolated editor host (port of
-- run_crowd_editable_bone_audit.ps1). Builds Saved/Crowd/EditableBoneAuditHost
-- from Tools/PoseBoneAudit + KKImportedAssets, runs the OKEditablePoseAudit
-- commandlet with -NullRHI and checks Saved/Crowd/EditableBones-<Tag>.json.
--   Tools\lua\lua.exe Tools\run_crowd_editable_bone_audit.lua --tag <fresh> [--input-file <cards.json>] [--card-count 100]
--       [--audit-animation] [--compare-constant-key-decoder] [--skip-build] [--engine <UE root>]
local ok = require "ok"
local C = require "crowd_launch_common"
local json = ok.json
local T, ne, fail = C.truthy, C.ne, C.fail

local M = {}

M.spec = {
  { "Tag", "string", mandatory = true, pattern = "^[A-Za-z0-9_-]+$" },
  { "Engine", "string" },
  { "InputFile", "string", default = "" },
  { "SelectionFile", "string", default = "" },
  { "CardCount", "int", default = 100, range = { 1, 100 } },
  { "AuditAnimation", "switch" },
  { "CompareConstantKeyDecoder", "switch" },
  { "SkipBuild", "switch" },
}

local function copy_into(src, dest_dir)
  local target = C.join(dest_dir, ok.path.name(src))
  if ok.path.is_dir(src) then ok.path.copytree(src, target) else ok.path.copy(src, target) end
end

function M.run(input)
  local P = C.bind(M.spec, input, "run_crowd_editable_bone_audit")
  if P.Engine == nil then P.Engine = ok.engine_root() end
  if P.CompareConstantKeyDecoder and not P.AuditAnimation then fail("Decoder comparison requires animation audit") end
  local root = C.root
  local host = C.join(root, "Saved/Crowd/EditableBoneAuditHost")
  local base = C.join(root, "Saved/Crowd/EditableBones-" .. P.Tag)
  if C.exists(base .. ".json") then fail("Fresh audit tag required") end
  if P.InputFile == "" then
    if P.SelectionFile == "" then P.SelectionFile = C.join(root, "Saved/Crowd/GameLowRandomSelectionR1.json") end
    local selection = C.read_json(P.SelectionFile)
    if C.count(selection.characters) < P.CardCount then fail("Insufficient selected characters") end
    local cards = {}
    for i = 1, P.CardCount do
      local card_path = tostring(selection.characters[i].character)
      local last = card_path:match("^.*()/")
      if card_path:sub(1, 6) ~= "/Game/" or (last or 0) - 1 < 6 then fail("Invalid game character path") end
      local parent = card_path:sub(1, last - 1)
      cards[#cards + 1] = json.object({ { "low", parent .. "/GameLowHairSafeR9/Character.Character" }, { "near", parent .. "/NearDetailR6/Character.Character" } })
    end
    P.InputFile = base .. ".input.json"
    C.write_all_json(P.InputFile, json.object({ { "cards", json.array(cards) } }))
  end
  P.InputFile = C.full(P.InputFile)
  if not P.SkipBuild then
    ok.path.mkdir(C.join(host, "Plugins/KKImportedAssets"), true)
    copy_into(C.join(C.tools, "PoseBoneAudit/OpenKoikatsuPoseAudit.uproject"), host)
    copy_into(C.join(C.tools, "PoseBoneAudit/Source"), host)
    copy_into(C.join(root, "Plugins/KKImportedAssets/KKImportedAssets.uplugin"), C.join(host, "Plugins/KKImportedAssets"))
    copy_into(C.join(root, "Plugins/KKImportedAssets/Source"), C.join(host, "Plugins/KKImportedAssets"))
    local res = ok.proc.run({ C.join(P.Engine, "Engine/Build/BatchFiles/Build.bat"), "OpenKoikatsuPoseAuditEditor", "Win64", "Development",
      "-Project=" .. C.join(host, "OpenKoikatsuPoseAudit.uproject"), "-NoEngineChanges", "-NoHotReloadFromIDE", "-UsePrecompiled" },
      { stdout_file = base .. ".build.log", merge = true })
    if res.code ~= 0 then fail("Isolated audit build failed: " .. base .. ".build.log") end
  end
  local args = { '"' .. C.join(host, "OpenKoikatsuPoseAudit.uproject") .. '"', "-run=OKEditablePoseAudit", "-NullRHI", "-unattended", "-nosound", "-NoSplash",
    '-AssetRoot="' .. C.join(root, "Content") .. '"', '-Input="' .. P.InputFile .. '"', '-Output="' .. base .. '.json"', '-abslog="' .. base .. '.log"' }
  if P.AuditAnimation then args[#args + 1] = "-AuditAnimation" end
  if P.CompareConstantKeyDecoder then args[#args + 1] = "-CompareConstantKeyDecoder" end
  local owned = C.start_process(C.join(P.Engine, "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), args, { hide = true })
  C.set_content(base .. ".pid", owned:pid())
  local code = owned:wait()
  if code ~= 0 then fail("Editable audit failed: " .. base .. ".log, exit=" .. code) end
  local result = C.read_json(base .. ".json")
  if ne(result.failures, 0) or ne(result.assets_modified, false) or not T(C.count(result.cards)) then fail("Editable audit receipt failed") end
  C.print_select(result, { "leader_bones", "low_weight_parent_closure", "low_near_weight_parent_closure", "failures", "assets_modified", "safe_pruning_proven" })
  return 0
end

function M.main()
  return M.run(C.parse_cli(M.spec, "Editable-mesh pose-bone audit (OKEditablePoseAudit commandlet)."))
end

if ok.is_main() then ok.main(M.main) end
return M
