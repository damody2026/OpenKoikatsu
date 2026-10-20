-- Run persisted retarget replay/rejection checks with isolated native game saves.
local ok = require "ok"
local json, path, util, proc = ok.json, ok.path, ok.util, ok.proc
local T = util.truthy

local function read(p) return json.decode(path.read_text(p)) end
local function write(p, value) path.write_text(p, json.encode(value, { ensure_ascii = false, indent = 2 }) .. "\n") end
local function win(p) return (path.norm(p):gsub("/", "\\")) end
local function signature(p)
  if not path.exists(p) then return nil end
  return ok.hash.file(p, "sha256") .. "|" .. string.format("%.7f", path.mtime(p))
end
local function assertion(msg) error("AssertionError: " .. msg, 0) end

local function run(args)
  local root = ok.root
  if not args.tag:match("^[A-Za-z0-9_%-]+$") then error("ValueError: Fresh alphanumeric test tag required", 0) end
  local evidence = path.abs(args.evidence)
  if not T(read(path.join(evidence, "social.validation.json")).passed) then
    error("ValueError: Successful isolated retarget evidence required", 0)
  end
  local source = read(path.join(evidence, "selection.json"))
  local store = read(path.join(evidence, "fixture.relationships.json"))
  if #(store.identity_retargets or {}) ~= 1 or #store.relations ~= 1 then
    error("ValueError: Expected one persisted retarget fixture", 0)
  end
  local target = path.join(root, "Saved/StreetSocial", "RetargetPersistence-" .. args.tag)
  if path.exists(target) then error("FileExistsError: " .. win(target), 0) end
  path.mkdir(target, false)
  local user_save = path.join(root, "Saved/StreetSocial/relationships.json")
  local user_before = signature(user_save)
  local requests = source.identity_retargets
  local results = {}
  local okrun, err = pcall(function()
    for _, case in ipairs({ { "replay", "" }, { "request-mismatch", "request_id_mismatch" }, { "destination-collision", "destination_collision" } }) do
      local name, reason = case[1], case[2]
      local folder = path.join(target, name)
      path.mkdir(folder, false)
      local fixture = path.join(folder, "fixture.relationships.json")
      local manifest = util.deepcopy(source)
      local candidate = util.deepcopy(store)
      -- Represents a subsequent save: replay must not reset or rewrite it.
      candidate.relations[1].favor = 13.5
      if name == "request-mismatch" then
        manifest.identity_retargets[1].new_id = manifest.characters[3].id
      elseif name == "destination-collision" then
        json.remove(candidate, "identity_retargets")
        local old_relation = util.deepcopy(candidate.relations[1])
        old_relation.id = requests[1].old_id
        table.insert(candidate.relations, old_relation)
      end
      write(fixture, candidate)
      local before = signature(fixture)
      manifest.save_path = fixture
      manifest.social_report = path.join(folder, "social.validation.json")
      manifest.report = path.join(folder, "runtime.json")
      local selection = path.join(folder, "selection.json")
      write(selection, manifest)
      local command = { win(path.abs(args.executable)), "/Game/Scenes/StreetSocial/StreetSocial",
        "-D3D12", "-SM6", "-windowed", "-ResX=1920", "-ResY=1080",
        "-StreetSocialRetargetSelfTest", "-RenderOffscreen", "-unattended",
        "-StreetSocialManifest=" .. win(selection),
        "-pakdir=" .. win(path.abs(args.patch)), "-abslog=" .. win(path.join(folder, "game.log")),
        "-RuntimeGpuCacheMiB=4096" }
      if args.cache_directory then command[#command + 1] = "-RuntimeGpuCacheDir=" .. win(path.abs(args.cache_directory)) end
      local completed = proc.run(command, { cwd = root, env = { OK_BUILTIN_RESOURCES = win(path.join(root, "Content/IndependentResources/v1")) },
        stdout_file = path.join(folder, "launcher.log"), merge = true, timeout = 540, hide = true })
      if completed.error == "timeout" then error("TimeoutExpired: " .. proc.cmdline(command) .. " timed out after 540 seconds", 0) end
      if completed.code == -1 and completed.error then error("cannot run " .. command[1] .. ": " .. tostring(completed.error), 0) end
      local report = read(path.join(folder, "social.validation.json"))
      if signature(fixture) ~= before then assertion(name .. " modified its persisted fixture") end
      if reason ~= "" then
        -- UE may return zero when BeginPlay requests exit during engine
        -- initialization. The native failure report and no admission
        -- are authoritative; a crash is still rejected.
        if (completed.code ~= 0 and completed.code ~= 1) or T(report.passed) or report.identity_retarget_rejected ~= 1 then
          assertion(name .. " was not refused by native startup")
        end
        if not util.any(report.errors or {}, function(e) return util.str(e):find(reason, 1, true) ~= nil end) then
          assertion(name .. " rejection reason differs")
        end
        if path.exists(path.join(folder, "runtime.json")) then
          assertion(name .. " admitted runtime actors before rejection")
        end
      else
        local runtime = read(path.join(folder, "runtime.json"))
        local av = report.all_visible_seconds or 0
        if (completed.code ~= 0 or not T(report.passed) or T(report.errors)
            or report.identity_retarget_applied ~= 0
            or report.identity_retarget_replayed ~= 1
            or report.completed_dialogue_lines ~= 0
            or not (0 < av and av <= 60)
            or runtime.complete ~= 6 or not T(runtime.gpu_ready)
            or runtime.gpu_failures ~= 0 or T(runtime.cooked_character_loaded)) then
          assertion("Persisted replay/runtime assertions failed")
        end
      end
      results[#results + 1] = json.object({ { "case", name }, { "passed", true }, { "exit_code", completed.code },
        { "save_unchanged", true }, { "evidence", folder } })
      print(name .. ": passed")
      io.stdout:flush()
    end
  end)
  if signature(user_save) ~= user_before then assertion("Native test changed the user relationship save") end
  if not okrun then error(err, 0) end
  local report = json.object({ { "passed", true }, { "full_acceptance", false }, { "cases", results }, { "user_save_unchanged", true } })
  write(path.join(target, "validation.json"), report)
  print(win(target))
  io.stdout:flush()
end

ok.main(function()
  local p = ok.args.parser("Run persisted retarget replay/rejection checks with isolated native game saves.")
  p:opt("--tag", { required = true })
  for _, key in ipairs({ "evidence", "executable", "patch" }) do p:opt("--" .. key, { required = true }) end
  p:opt("--cache-directory")
  run(p:parse(arg))
end)
