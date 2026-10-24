-- StreetSocial development launcher (StreetSocial.cmd). Runs the newest staged build (Saved/StreetSocial/Build/*.stage)
-- unless -Executable is given; CrowdRoom*.stage lives there too but is the crowd room's build. Integrity (SHA256) checks
-- against receipt.json only run with -Strict: several agents rebuild shared data and stages at the same time, so the
-- default just needs the files to exist.
--
--   Tools\lua\lua.exe Tools\run_street_social_development.lua [-Control [-ControlPort N]] [-Clock HH:MM] [-SelfTest]
--     [-ValidateOnly] [-Strict] [-Executable EXE] [-SelectionFile JSON] [-MaleCard PNG] [-RetargetFile JSON]
--     [-RenderScale 100..200] [-Window WxH]  (window size, default 1920x1080; a smaller one leaves room for other work)
--     [-Editor]  (development: runs the map in the built editor with -game instead of a staged build; no cook needed)
--     [-Sandbox NAME [-PortBase N] [-RulesDll DLL]]
-- -Sandbox runs a second game beside one already playing from this checkout without touching it: its own ports
-- (-PortBase, default 48060: TTS N, ASR N+1, control N+10), its own save, voice settings and free-talk logs in
-- Saved/StreetSocial/Sandbox/NAME/ (copied from the main save the first time), voice servers started for it only (the
-- other game's are not stopped), and its own copies of the rules / mind DLLs (no cargo build of Tools/ok-rules, which
-- the other game hot-reloads). Identity migration of the shared saves is skipped.
-- Option names are case-insensitive and may also be written --control, --control-port, ... (StreetSocial.cmd passes
-- its arguments here unchanged).
--
-- Ports and voice servers follow this checkout's local-instance.json (Tools/ok_instance.lua): another clone on the same
-- machine runs its own game on its own ports, and only servers started from this checkout's Build/ are stopped.
local ok = require "ok"
local json, path, proc, hash, util = ok.json, ok.path, ok.proc, ok.hash, ok.util
local fs = require "okfs"
local instance = require "ok_instance"

local SWITCHES = { validateonly = true, selftest = true, strict = true, control = true, editor = true }
local VALUES = { selectionfile = true, malecard = true, retargetfile = true, renderscale = true, executable = true,
  controlport = true, clock = true, window = true, sandbox = true, portbase = true, rulesdll = true, exec = true, trace = true, gpucachedir = true }

local function parse(argv)
  local o, i = {}, 1
  while i <= #argv do
    local raw = argv[i]
    local name, inline = raw:match("^%-%-?([%w%-]+):(.*)$")
    if not name then name = raw:match("^%-%-?([%w%-]+)$") end
    if not name then error("unexpected argument: " .. raw, 0) end
    local key = name:gsub("%-", ""):lower()
    if SWITCHES[key] then o[key] = true
    elseif VALUES[key] then
      local v = inline
      if v == nil then i = i + 1; v = argv[i] end
      if v == nil then error("missing value for " .. raw, 0) end
      o[key] = v
    else error("unknown option: " .. raw, 0) end
    i = i + 1
  end
  return o
end

local function say(s) io.write(s, "\n"); io.flush() end
local function sha(p) return hash.sha256_file(p):lower() end
local function exists(p) return p ~= nil and p ~= "" and path.exists(p) end
local function read_json(p) return json.decode(path.read_text(p)) end
local function tool(script, args) return proc.lua(path.join(ok.root, "Tools", script), args) end

-- Identity retargets (formerly an inline Lua block of the PowerShell launcher): check a request file, or inject it.
local function retarget(mode, request_path, manifest_path)
  local function quit(msg) error("Identity retarget file was rejected: " .. msg, 0) end
  local payload = json.decode(path.read_text(request_path))
  local rows
  if json.is_array(payload) then rows = payload
  elseif json.is_object(payload) and #json.keys(payload) == 1 and json.keys(payload)[1] == "identity_retargets" and json.is_array(payload.identity_retargets) then
    rows = payload.identity_retargets
  else quit("Identity retarget file must be a request array or an object with only identity_retargets") end
  if #rows < 1 or #rows > 10 then quit("Identity retarget file must contain 1 to 10 requests") end
  local function identity(v) return type(v) == "string" and #v == 69 and v:match("^apng%-[0-9a-f]+$") ~= nil end
  local clean, seen = {}, {}
  for _, row in ipairs(rows) do
    if not json.is_object(row) or #json.keys(row) ~= 3 or row.request_id == nil or row.old_id == nil or row.new_id == nil then
      quit("Identity retarget request must have only request_id, old_id, and new_id")
    end
    local request_id, old_id, new_id = row.request_id, row.old_id, row.new_id
    if type(request_id) ~= "string" or #request_id > 64 or not request_id:match("^[A-Za-z0-9_%-]+$") or request_id:sub(1, 8) == "session-" or seen[request_id] then
      quit("Identity retarget request_id is invalid or duplicated")
    end
    if not identity(old_id) or not identity(new_id) or old_id == new_id then quit("Identity retarget ids must be distinct apng- sha256 values") end
    seen[request_id] = true
    clean[#clean + 1] = json.object({ { "request_id", request_id }, { "old_id", old_id }, { "new_id", new_id } })
  end
  if mode == "check" then return end
  local manifest = json.decode(path.read_text(manifest_path))
  local roster = {}
  for _, item in ipairs(manifest.characters or {}) do roster[#roster + 1] = util.str(item.id == nil and "" or item.id) end
  local females = util.slice(roster, 2)
  for _, row in ipairs(clean) do
    if not util.contains_value(females, row.new_id) then quit("Identity retarget new_id is not a selected female") end
    if util.contains_value(roster, row.old_id) then quit("Identity retarget old_id still belongs to a selected actor") end
  end
  manifest.identity_retargets = clean
  path.write_text(manifest_path, json.encode(manifest, { ensure_ascii = false, indent = 2 }) .. "\n", { newline = "\n" })
end

-- -Strict: everything the receipt verified must still hash the same.
local function strict_checks(build)
  local function same(p, digest) return exists(p) and sha(p) == digest end
  local q = build.runtime_render_quality
  if q and (q.antialiasing_method ~= 1 or q.screen_percentage ~= 150 or not same(q.evidence, q.evidence_sha256)) then error("Verified anti-aliasing quality profile changed", 0) end
  if not build.passed or not build.new_map or build.female_count ~= 5 then error("StreetSocial verified receipt is required", 0) end
  if build.material_manifest and not same(build.material_manifest, build.material_manifest_sha256) then error("Verified common material manifest changed", 0) end
  if build.runtime_display_profile and not same(build.runtime_display_profile.lut, build.runtime_display_profile.sha256) then error("Verified CardReader display LUT changed", 0) end
  if build.runtime_private_high_model and build.runtime_interaction_mode ~= "in_process" then
    if not same(build.high_interaction_host, build.high_interaction_host_sha256) then error("Verified CardReader interaction host changed", 0) end
    if not same(build.high_interaction_module, build.high_interaction_module_sha256) then error("Verified CardReader interaction module changed", 0) end
    for _, d in ipairs(read_json(build.high_interaction_host).dependencies or {}) do
      if not same(d.path, d.sha256) then error("Verified frozen CardReader dependency changed", 0) end
    end
  end
  if build.runtime_interaction_watergun or build.runtime_interaction_full_catalogue then
    local f = build.interaction_features_evidence
    if not f.passed or not same(f.evidence, f.sha256) then error("Verified interaction watergun/catalogue evidence changed", 0) end
  end
  if build.runtime_interaction_mode == "in_process" and not same(build.reader_interaction_evidence, build.reader_interaction_evidence_sha256) then
    error("Verified shared interaction acceptance changed", 0)
  end
end

local function strict_inputs(build)
  local function same(p, digest) return exists(p) and sha(p) == digest end
  if build.executable_sha256 and sha(build.executable) ~= build.executable_sha256 then error("Verified StreetSocial executable changed", 0) end
  if build.patch_file_sha256 then
    local want, have = json.keys(build.patch_file_sha256), {}
    for _, n in ipairs(path.list(build.patch)) do if n:lower():match("%.pak$") then have[#have + 1] = n end end
    table.sort(want); table.sort(have)
    if #want ~= #have then error("Verified runtime patch set changed", 0) end
    for i, n in ipairs(want) do
      if n ~= have[i] then error("Verified runtime patch set changed", 0) end
      if path.name(n) ~= n then error("Invalid runtime patch filename", 0) end
      if sha(path.join(build.patch, n)) ~= build.patch_file_sha256[n] then error("Verified runtime patch changed", 0) end
    end
  end
  if build.runtime_invitations and not same(build.invitations_catalogue, build.invitations_sha256) then error("Shared invitation catalogue changed since validation", 0) end
  if build.runtime_dialogue_context then
    if not same(build.relationships_catalogue, build.relationships_sha256) then error("Shared relationship dialogue changed since validation", 0) end
    local pack = read_json(build.relationships_catalogue)
    local base = path.parent(path.parent(build.relationships_catalogue))
    for _, name in ipairs(json.keys(pack.source_voice_sha256 or json.object({}))) do
      if not same(path.join(base, name), pack.source_voice_sha256[name]) then error("Shared relationship voice changed since validation", 0) end
    end
  end
  local packs = {}
  local function pack(flag, name, p, h) if flag then packs[#packs + 1] = { name = name, path = p, hash = h } end end
  pack(build.runtime_presentation, "locomotion", build.locomotion_manifest, build.locomotion_manifest_sha256)
  pack(build.runtime_phone_use, "phone", build.phone_manifest, build.phone_manifest_sha256)
  pack(build.runtime_npc_drink, "drink", build.drink_manifest, build.drink_manifest_sha256)
  pack(build.runtime_npc_selfie, "selfie", build.selfie_manifest, build.selfie_manifest_sha256)
  pack(build.runtime_npc_groom, "groom", build.groom_manifest, build.groom_manifest_sha256)
  pack(build.runtime_touch_body_reaction, "touch", build.touch_manifest, build.touch_manifest_sha256)
  for _, p in ipairs(packs) do
    if not same(p.path, p.hash) then error("Shared " .. p.name .. " manifest changed since validation", 0) end
    for _, a in ipairs(read_json(p.path).actions or {}) do
      if not same(path.join(path.parent(p.path), a.file), a.sha256) then error("Shared " .. p.name .. " animation changed since validation", 0) end
    end
  end
end

-- Newest StreetSocial stage executable (CrowdRoom*.stage is the crowd room's).
local function newest_stage(root)
  local dir, best, best_time = path.join(root, "Saved/StreetSocial/Build"), nil, -1
  if not path.is_dir(dir) then return nil end
  for _, n in ipairs(path.list(dir)) do
    if n:lower():match("%.stage$") and not n:lower():match("^crowdroom") then
      local exe = path.join(dir, n, "Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe")
      local t = path.mtime(exe)
      if t and t > best_time then best, best_time = exe, t end
    end
  end
  return best
end

local function main()
  local o = parse(arg)
  local root = ok.root
  local build, launch
  if o.strict then
    local receipt = path.join(root, "Saved/StreetSocial/Build/receipt.json")
    if not path.is_file(receipt) then error("StreetSocial Development has not passed build/runtime validation yet", 0) end
    build = read_json(receipt)
  else
    -- Saved/ is disposable: everything the launch needs comes from Content (Launch/runtime.json); the executable is the
    -- newest stage, rebuilt from Content + Source when none is left.
    launch = read_json(path.join(root, "Content/IndependentResources/StreetSocialData/Launch/runtime.json"))
    build = { executable = "", patch = path.join(root, launch.patch), catalogue = path.join(root, launch.catalogue),
      material_manifest = path.join(root, launch.materials), runtime_render_quality = launch.render_quality }
    if o.editor then o.executable = path.join(ok.engine_root(), "Engine/Binaries/Win64/UnrealEditor.exe") end
    if not o.executable then
      local latest = newest_stage(root)
      if not latest then
        say("No StreetSocial build under Saved/: rebuilding it from Content + Source (this takes a while)...")
        tool("rebuild_street_social_runtime.lua", { "--tag", "StreetAuto" .. os.date("%Y%m%d%H%M") })
        latest = newest_stage(root)
        if not latest then error("StreetSocial rebuild did not produce a stage", 0) end
      end
      o.executable = latest
    end
    build.executable = path.abs(o.executable)
    say("StreetSocial build: " .. build.executable)
    -- The runtime material shader library and audio settings ship as a patch beside the stage; rebuild it when gone.
    if not path.exists(build.patch) then
      say("No StreetSocial runtime patch under Saved/: rebuilding it from Content...")
      tool("rebuild_street_social_patch.lua")
      if not path.exists(build.patch) then error("StreetSocial patch rebuild failed", 0) end
    end
    -- The player's own roster wins; otherwise the default heroines from Content (made absolute for the selection tool).
    local default_selection = path.join(root, "Saved/StreetSocial/heroines.json")
    if not o.selectionfile and not path.exists(default_selection) then
      local roster = read_json(path.join(root, launch.roster))
      path.mkdir(path.join(root, "Saved/StreetSocial"), true)
      local cards = json.array({})
      for _, c in ipairs(roster.cards) do cards[#cards + 1] = path.abs(path.join(root, c)) end
      path.write_text(default_selection, json.encode(json.object({ { "cards", cards } }), { indent = 2, ensure_ascii = false }))
      o.selectionfile = default_selection
    end
    if not o.malecard then o.malecard = path.join(root, launch.male_card) end
  end
  local render_scale = tonumber(o.renderscale or "0")
  if not render_scale or render_scale < 0 or render_scale > 200 or (render_scale > 0 and render_scale < 100) then
    error("RenderScale must be 0 (verified default), or 100..200 percent", 0)
  end
  if o.strict then strict_checks(build) end
  o.selectionfile = o.selectionfile or path.join(root, "Saved/StreetSocial/heroines.json")
  o.malecard = o.malecard or path.join(root, "output/StreetSocialAPNG/Male01.png")
  -- The patch is not an input check outside -Strict: it was rebuilt above when missing (it carries the material roots).
  local inputs = { build.executable, build.catalogue, o.selectionfile, o.malecard }
  if o.strict then inputs[#inputs + 1] = build.patch end
  for _, p in ipairs(inputs) do if not exists(p) then error("Missing StreetSocial input: " .. tostring(p), 0) end end
  if o.strict then strict_inputs(build) end
  if o.retargetfile then
    if not path.is_file(o.retargetfile) then error("Missing identity retarget file: " .. o.retargetfile, 0) end
    local size = path.size(o.retargetfile)
    if size <= 0 or size > 65536 then error("Identity retarget file must be 1 to 65536 bytes: " .. o.retargetfile, 0) end
    retarget("check", o.retargetfile)
  end
  if o.retargetfile and o.selftest then error("Identity retarget file cannot be combined with -SelfTest", 0) end
  if o.validateonly then
    if o.retargetfile then say("Identity retarget file accepted: " .. o.retargetfile) end
    say("Verified new StreetSocial map: " .. build.executable)
    return
  end

  local output = path.join(root, "Saved/StreetSocial/Play-" .. os.date("%Y%m%dT%H%M%S"))
  local suffix = 0
  while path.exists(output .. (suffix > 0 and ("-" .. suffix) or "")) do suffix = suffix + 1 end
  output = output .. (suffix > 0 and ("-" .. suffix) or "")
  path.mkdir(output, true)
  local manifest = path.join(output, "selection.json")
  local materials = build.material_manifest or path.join(root, "Saved/Crowd/RuntimeSharedMaterial-R1145.json")
  -- Saved/ is disposable: when the material receipt is gone, derive it from its tag (the materials live in Content).
  if not path.exists(materials) then
    local tag = path.stem(materials):gsub("^RuntimeSharedMaterial%-", "")
    if not path.exists(path.join(root, "Content/IndependentResources/RuntimeMaterials", tag)) then error("Runtime materials missing: " .. tag, 0) end
    materials = path.join(output, "materials.json")
    path.write_text(materials, json.encode(json.object({ { "schema", 1 }, { "passed", true }, { "runtime_reveal_supported", true },
      { "root", "/Game/IndependentResources/RuntimeMaterials/" .. tag }, { "derived", true } }), { indent = 2 }))
  end
  local sel = proc.lua(path.join(root, "Tools/prepare_street_social_selection.lua"), { "--male", o.malecard, "--selection", o.selectionfile,
    "--catalogue", build.catalogue, "--materials", materials, "--output", manifest })
  if not sel.ok then error("StreetSocial APNG selection validation failed", 0) end
  if o.retargetfile then retarget("inject", o.retargetfile, manifest) end
  -- The editor has no cooked shader library; the crowd runtime then draws with the editor's own material resources.
  if o.editor then
    local m = read_json(manifest)
    m.editor_common_materials = true
    path.write_text(manifest, json.encode(m, { ensure_ascii = false, indent = 2 }) .. "\n", { newline = "\n" })
  end
  -- Replaced heroine cards (StreetSocialData/identity_aliases.json): the live game and every save slot move to the new
  -- ids before the game opens them (a no-op when no save names an old id).
  if not o.selftest and not o.sandbox and not tool("street_identity_migrate.lua", {}).ok then error("Identity alias migration failed", 0) end
  -- Saved/ is disposable: the player's own settings (microphone, voice, name) are kept at %LOCALAPPDATA%\OpenKoikatsu
  -- too, like deepseek.json, and put back from there when Saved/ was cleared. The newer copy wins.
  local user_dir = os.getenv("LOCALAPPDATA") and path.join(os.getenv("LOCALAPPDATA"), "OpenKoikatsu")
  if user_dir then
    path.mkdir(user_dir, true); path.mkdir(path.join(root, "Saved/StreetSocial"), true)
    for _, name in ipairs({ "asr-settings.json", "voice-settings.json" }) do
      local here, kept = path.join(root, "Saved/StreetSocial", name), path.join(user_dir, name)
      if path.is_file(here) and (not path.is_file(kept) or (path.mtime(here) or 0) > (path.mtime(kept) or 0)) then pcall(path.copy, here, kept)
      elseif not path.is_file(here) and path.is_file(kept) then pcall(path.copy, kept, here); say("Restored " .. name .. " from " .. user_dir) end
    end
  end
  -- Sandbox: the save (and what lives beside it: voice settings, free-talk logs) moves to the sandbox folder.
  local sandbox
  if o.sandbox then
    if not o.sandbox:match("^[%w_%-]+$") then error("-Sandbox takes a name of letters, digits, - and _", 0) end
    sandbox = path.join(root, "Saved/StreetSocial/Sandbox", o.sandbox)
    path.mkdir(sandbox, true)
    for _, name in ipairs({ "relationships.json", "voice-settings.json", "player.json", "phone.json", "free-talk-log.json", "free-talk-memory.json" }) do
      local main_file, own = path.join(root, "Saved/StreetSocial", name), path.join(sandbox, name)
      if not path.exists(own) and path.is_file(main_file) then path.copy(main_file, own) end
    end
    local m = json.decode(path.read_text(manifest))
    m.save_path = (path.join(sandbox, "relationships.json"):gsub("/", "\\"))
    path.write_text(manifest, json.encode(m, { ensure_ascii = false, indent = 2 }))
    say("Sandbox " .. o.sandbox .. ": save and settings in " .. sandbox)
  end

  -- This checkout's ports and voice servers (local-instance.json, Tools/ok_instance.lua).
  local ports = instance.ports(root)
  if sandbox then ports = instance.ports_at(math.tointeger(tonumber(o.portbase or "48060")) or error("-PortBase takes a number", 0), ports.voice) end
  local control_port = tonumber(o.controlport) or ports.street_control
  -- Voice servers (ok-tts speech, ok-asr voice input) live and die with the game: a copy this checkout left from an
  -- earlier run is stopped here, and the fresh ones start after the game with --parent-pid so they exit when it does.
  local voice = ports.voice and not o.selftest
  if not ports.voice then say("local-instance.json voice=false: no ok-tts / ok-asr from this checkout (built-in voices, no voice input)") end
  local servers = {}
  local asr_endpoint, tts_endpoint = "http://127.0.0.1:" .. ports.asr, "http://127.0.0.1:" .. ports.tts
  -- Without servers of its own (voice=false), a checkout may speak through another one's ok-tts (testing the voices on
  -- E with C's server): OK_TTS_ENDPOINT / OK_ASR_ENDPOINT from the environment win then.
  if not voice then
    tts_endpoint = fs.getenv("OK_TTS_ENDPOINT") or tts_endpoint
    asr_endpoint = fs.getenv("OK_ASR_ENDPOINT") or asr_endpoint
  end
  local asr_settings = path.join(root, "Saved/StreetSocial/asr-settings.json")
  if voice then
    if not sandbox then instance.stop_own_voice_servers(root) end
    local vs = path.join(root, "Saved/StreetSocial/voice-settings.json")
    local okv, v = pcall(read_json, vs)
    if path.is_file(vs) and okv and v.tts_server_command then say("tts_server_command is ignored: the server is tied to the game process (Build/ok-tts/ok-tts.exe)") end
    -- Free-talk replies are always spoken through ok-tts, so it starts in built-in voice mode too.
    local tts = path.join(root, "Build/ok-tts/ok-tts.exe")
    if path.is_file(tts) then servers[#servers + 1] = { name = "TTS", file = tts, args = { "serve", "--port", tostring(ports.tts) } }
    else say("TTS server not found; built-in voice will be used: " .. tts) end
    -- Free-talk voice input: asr-settings.json may name the microphone ({"device": "part of the input device name"});
    -- otherwise the Windows default recording device is used. ok-asr writes its own log (Build/ok-asr/logs/ok-asr.log).
    local oka, a = pcall(read_json, asr_settings)
    if path.is_file(asr_settings) and oka and type(a.endpoint) == "string" and a.endpoint ~= "" then asr_endpoint = a.endpoint end
    local asr = path.join(root, "Build/ok-asr/ok-asr.exe")
    if path.is_file(asr) then
      local args = { "serve", "--port", tostring(ports.asr) }
      if path.is_file(asr_settings) then args[#args + 1] = "--settings"; args[#args + 1] = asr_settings end
      servers[#servers + 1] = { name = "ASR", file = asr, args = args }
    else say("ASR server not found; voice input unavailable: " .. asr) end
    -- Heroine songs (Tools/ok-song; its ok-song.json beside the exe says where the model stages run). Optional: without
    -- it the game simply never offers a song.
    local song = path.join(root, "Build/ok-song/ok-song.exe")
    if path.is_file(song) then servers[#servers + 1] = { name = "Song", file = song, args = { "serve", "--port", tostring(ports.song) } }
    else say("Song server not found; heroines will not sing: " .. song) end
  end

  -- Heroine mind (Tools/ok-mind, cargo build --release): the game loads a copy, so cargo can rebuild while it runs.
  -- Without the DLL the street keeps its old behaviour.
  local mind_source = path.join(root, "Tools/ok-mind/target/release/ok_mind.dll")
  local mind_dll = path.join(sandbox or path.join(root, "Saved/StreetSocial/Build"), "ok-mind/ok_mind.dll")
  if path.is_file(mind_source) then
    path.mkdir(path.parent(mind_dll), true)
    local copied, err = pcall(path.copy, mind_source, mind_dll)
    if not copied then say("Heroine mind DLL in use, keeping the previous copy: " .. tostring(err)) end
  end
  -- Game rules (Tools/ok-rules): required. Built here when cargo is available (a no-op when up to date); the game loads
  -- a copy so cargo can rebuild while it runs, and ok.StreetRules.Reload (or ok-game-mcp game_call /rules reload) swaps it in.
  local rules_crate = path.join(root, "Tools/ok-rules")
  -- -RulesDll (sandbox): a rules build of its own (e.g. made with another CARGO_TARGET_DIR), so testing new rules never
  -- swaps the DLL the other game hot-reloads.
  local rules_source = (sandbox and o.rulesdll) and path.abs(o.rulesdll) or path.join(rules_crate, "target/release/ok_rules.dll")
  local rules_dll = path.join(sandbox or path.join(root, "Saved/StreetSocial/Build"), "ok-rules/ok_rules.dll")
  if proc.which("cargo") and not sandbox then
    local r = proc.run({ "cargo", "build", "--release", "--quiet" }, { cwd = rules_crate, capture = true })
    if not r.ok then say("Game rules build failed; using the previous build if there is one") end
  end
  if path.is_file(rules_source) then
    path.mkdir(path.parent(rules_dll), true)
    local copied, err = pcall(path.copy, rules_source, rules_dll)
    if not copied then say("Game rules DLL in use, keeping the previous copy: " .. tostring(err)) end
  else say("Game rules DLL not built (cargo build --release in Tools/ok-rules); the game will show the error and quit: " .. rules_source) end
  -- Conversation presentation (Tools/ok-story, galgame dialog box / portrait / shots): optional, a copy like the rules.
  local story_crate = path.join(root, "Tools/ok-story")
  local story_source = path.join(story_crate, "target/release/ok_story.dll")
  local story_dll = path.join(root, "Saved/StreetSocial/Build/ok-story/ok_story.dll")
  if proc.which("cargo") then
    local r = proc.run({ "cargo", "build", "--release", "--quiet" }, { cwd = story_crate, capture = true })
    if not r.ok then say("Conversation presenter build failed; using the previous build if there is one") end
  end
  if path.is_file(story_source) then
    path.mkdir(path.parent(story_dll), true)
    local copied, err = pcall(path.copy, story_source, story_dll)
    if not copied then say("Conversation presenter DLL in use, keeping the previous copy: " .. tostring(err)) end
  end

  -- The game reads these at start; set only for the processes started below (this launcher's own environment).
  local env = {
    OK_BUILTIN_RESOURCES = path.join(root, "Content/IndependentResources/v1"),
    OK_RULES_DLL = path.is_file(rules_dll) and rules_dll or "",
    OK_RULES_SOURCE_DLL = rules_source,
    OK_RULES_TUNING = path.join(root, "Content/IndependentResources/StreetSocialData/rules"),
    OK_STORY_DLL = path.is_file(story_dll) and story_dll or "",
    OK_STORY_DATA = path.join(root, "Content/IndependentResources/Story"),
    -- Free talk: staged builds cannot see the project Saved/Content folders, so pass absolute paths.
    OK_FREE_TALK_SETTINGS = path.join(root, "Saved/StreetSocial/deepseek.json"),
    OK_FREE_TALK_PERSONAS = path.join(root, "Content/IndependentResources/StreetSocialData/free_talk_personas.json"),
    OK_ASR_ENDPOINT = asr_endpoint,
    -- The launcher started ok-tts on this checkout's port; it wins over an endpoint saved in voice-settings.json.
    OK_TTS_ENDPOINT = tts_endpoint,
    OK_SONG_ENDPOINT = "http://127.0.0.1:" .. ports.song,
    -- The game saves the microphone chosen in its settings here; the next launch passes it to ok-asr.
    OK_ASR_SETTINGS = asr_settings,
    OK_ASR_SERVER = path.join(root, "Build/ok-asr/ok-asr.exe"),
    OK_MIND_DLL = path.is_file(mind_dll) and mind_dll or "",
    -- The build the mind's hot reload watches (ok-game-mcp game_call /mind reload).
    OK_MIND_SOURCE_DLL = mind_source,
  }
  -- Paths in Windows form; the voice endpoints are URLs ("http://127.0.0.1:47861") and stay as they are.
  for k, v in pairs(env) do
    if v == "" then fs.setenv(k, nil)
    elseif v:match("^%a[%w+.-]*://") then fs.setenv(k, v)
    else fs.setenv(k, (v:gsub("/", "\\"))) end
  end

  local win_w, win_h = (o.window or "1920x1080"):match("^(%d+)[xX](%d+)$")
  if not win_w then error("-Window takes WxH, e.g. 1280x720", 0) end
  local game = { build.executable, "/Game/Scenes/StreetSocial/StreetSocial", "-D3D12", "-SM6", "-windowed", "-ResX=" .. win_w, "-ResY=" .. win_h, "-WinX=40", "-WinY=40",
    "-StreetSocialManifest=" .. manifest }
  -- The editor reads the project's uncooked content: the project goes first, and the cooked patch is not mounted.
  if o.editor then table.insert(game, 2, path.join(root, "OpenKoikatsu.uproject")); game[#game + 1] = "-game"
  elseif path.exists(build.patch) then game[#game + 1] = "-pakdir=" .. build.patch end
  game[#game + 1] = "-abslog=" .. path.join(output, "game.log")
  game[#game + 1] = "-RuntimeGpuCacheMiB=4096"
  -- -GpuCacheDir <dir>: another runtime GPU pose cache (default %LOCALAPPDATA%, shared by every checkout).
  if o.gpucachedir then game[#game + 1] = "-RuntimeGpuCacheDir=" .. o.gpucachedir end
  local q = build.runtime_render_quality
  local execs = {}
  if q then
    local scale = render_scale > 0 and render_scale or q.screen_percentage
    execs[#execs + 1] = ("r.AntiAliasingMethod %s,r.ScreenPercentage %s"):format(util.str(q.antialiasing_method), util.str(scale))
  end
  -- Profiling (development): -Exec "stat dumphitches,t.HitchFrameTimeThreshold 60" runs console commands at start;
  -- -Trace records an Unreal Insights trace (default channels default,cpu,gpu,frame,bookmark) to trace.utrace beside game.log.
  if o.exec then execs[#execs + 1] = o.exec end
  if #execs > 0 then game[#game + 1] = "-ExecCmds=" .. table.concat(execs, ",") end
  if o.trace then
    game[#game + 1] = "-trace=" .. ((o.trace == true or o.trace == "") and "default,cpu,gpu,frame,bookmark" or o.trace)
    game[#game + 1] = "-tracefile=" .. path.join(output, "trace.utrace"); game[#game + 1] = "-statnamedevents"
  end
  if o.selftest then for _, a in ipairs({ "-StreetSocialSelfTest", "-unattended", "-RenderOffscreen" }) do game[#game + 1] = a end end
  -- Development control channel (http://127.0.0.1:<port>: state, say, click, key, navigate, screenshot); never on by default.
  if o.control then game[#game + 1] = "-StreetSocialControl"; game[#game + 1] = "-StreetSocialControlPort=" .. control_port end
  if o.clock then game[#game + 1] = "-StreetSocialClock=" .. o.clock end
  say("StreetSocial: one male + up to ten heroines. WASD move, Shift run, mouse camera, Ctrl/middle UI cursor, wheel zoom, E talk, 1/2/3 topics, Esc cancel, L Chinese/Japanese voices, V built-in/TTS voice, N skip to 07:00 next morning; friendship/confession results save automatically after dialogue; F9 CPU/GPU; conversation buttons: shared walk/explore, photo (P save), seated talk, drinks, splash either direction; Esc ends activity. Reading uses a hand-held book.")
  if o.control then say(("Control channel: http://127.0.0.1:%d (Tools/ok-game-mcp finds it through local-instance.json)"):format(control_port)) end
  local pid = proc.spawn(game, { cwd = root, hide = o.selftest and true or nil })
  path.write_text(path.join(output, "game.pid"), tostring(pid))
  local server_pids = {}
  for _, s in ipairs(servers) do
    local args = { s.file }
    for _, a in ipairs(s.args) do args[#args + 1] = a end
    args[#args + 1] = "--parent-pid"; args[#args + 1] = tostring(pid)
    server_pids[#server_pids + 1] = proc.spawn(args, { cwd = path.parent(s.file), hide = true })
    say(s.name .. " server starting with the game on port " .. s.args[3] .. " (exits when the game closes; model load ~5-10s): " .. s.file)
  end
  -- A normal launch stops this checkout's leftover servers; a running sandbox's are listed here so they are kept.
  if sandbox then path.write_text(path.join(sandbox, "running.json"), json.encode({ game_pid = pid, server_pids = server_pids })) end
  say(output)
end

ok.main(main)
