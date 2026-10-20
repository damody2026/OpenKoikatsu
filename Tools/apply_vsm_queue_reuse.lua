-- Apply/restore a reviewed VSM shader patch, refusing unknown engine revisions.
local ok = require "ok"
local json, P = ok.json, ok.path

local RELATIVE_SHADER = "Engine/Shaders/Private/VirtualShadowMaps/VirtualShadowMapBuildPerPageDrawCommands.usf"

local function digest(data) return ok.hash.sha256(data):lower() end
local function win(p) return (P.norm(p):gsub("/", "\\")) end
local function fail(msg) error("ValueError: " .. msg, 0) end
local function create_exclusive(p, data)
  if P.exists(p) then error(("FileExistsError: [Errno 17] File exists: '%s'"):format(win(p)), 0) end
  P.write_bytes(p, data)
end

local function operate(engine, candidate, metadata, backup, mode)
  local info = json.decode(P.read_text(metadata))
  local original_hash = info.original_sha256:lower()
  local candidate_hash = info.candidate_sha256:lower()
  local replacement = P.read_bytes(candidate)
  if digest(replacement) ~= candidate_hash then fail("Candidate differs from reviewed metadata") end
  local target = P.join(P.abs(engine), RELATIVE_SHADER)
  local current = P.read_bytes(target)
  local current_hash = digest(current)
  if current_hash ~= original_hash and current_hash ~= candidate_hash then
    fail("Engine shader changed; refusing to overwrite unrelated edits")
  end
  if mode == "verify" then
    return json.object({ { "installed", current_hash == candidate_hash }, { "sha256", current_hash } })
  end
  if mode == "restore" then
    replacement = P.read_bytes(backup)
    if digest(replacement) ~= original_hash then fail("Original backup does not match reviewed engine revision") end
  elseif current_hash == original_hash then
    P.mkdir(P.parent(backup))
    if P.exists(backup) then
      if digest(P.read_bytes(backup)) ~= original_hash then fail("Existing backup belongs to a different engine revision") end
    else
      create_exclusive(backup, current)
    end
  elseif not P.exists(backup) or digest(P.read_bytes(backup)) ~= original_hash then
    fail("Installed patch requires a verified recoverable original backup")
  end
  -- Atomic sibling replacement prevents the compiler from reading a partial file.
  local temporary = target .. ".ok-queue-reuse.tmp"
  create_exclusive(temporary, replacement)
  P.replace(temporary, target)
  return json.object({ { "installed", mode == "apply" }, { "sha256", digest(P.read_bytes(target)) }, { "backup", win(P.abs(backup)) } })
end

local p = ok.args.parser("Apply/restore a reviewed VSM shader patch, refusing unknown engine revisions.")
p:opt("--engine", { default = os.getenv("OK_ENGINE_ROOT") or "E:/PhysX/UE5.8" })
p:opt("--candidate", { required = true })
p:opt("--metadata", { required = true })
p:opt("--backup", { default = "Saved/EnginePatches/VsmQueueReuse/VirtualShadowMapBuildPerPageDrawCommands.original.usf" })
p:opt("--mode", { choices = { "apply", "restore", "verify" }, required = true })
local a = p:parse(arg)
print(json.encode(operate(a.engine, a.candidate, a.metadata, a.backup, a.mode)))
