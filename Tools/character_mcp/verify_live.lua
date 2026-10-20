-- Launch an isolated editor and verify real stdio MCP edits and rendered captures.
local ok = require "ok"
local Client = require "client"
local json, util, path = ok.json, ok.util, ok.path

-- Ordered argument objects: merge(a, b, ...) like {**a, **b}.
local function merge(...)
  local o = json.object()
  for _, t in ipairs({ ... }) do for _, k in ipairs(json.keys(t)) do o[k] = t[k] end end
  return o
end
local function obj(list) return json.object(list) end
local function starts(s, p) return s:sub(1, #p) == p end
local function count(list, f) local n = 0; for _, v in ipairs(list) do if f(v) then n = n + 1 end end; return n end
local function close(a, b, tol) return math.abs(a - b) < tol end
local function uuid_hex()
  local t = {}
  for i = 1, 32 do t[i] = string.format("%x", math.random(0, 15)) end
  return table.concat(t)
end
local function write_json(p, v) path.write_text(p, json.encode(v, { ensure_ascii = false, indent = 2 })) end
local function png_size(data) return string.unpack(">I4I4", data, 17) end

local function main()
  local engine = (os.getenv("OK_ENGINE_ROOT") or "E:/PhysX/UE5.8") .. "/Engine/Binaries/Win64/UnrealEditor.exe"
  local args = ok.args.parser("Launch an isolated editor and verify real stdio MCP edits and rendered captures.")
    :opt("--card", { required = true }):opt("--editor", { default = engine }):opt("--output"):opt("--server")
    :parse(arg)
  math.randomseed(math.floor(ok.time.time() * 1e6) % 2147483647)
  local root = ok.root
  local output = path.abs(args.output or (root .. "/Saved/Tests/CharacterMCP/" .. uuid_hex()))
  if path.exists(output) then error("output exists: " .. output) end
  path.mkdir(output)
  local mailbox = output .. "/mailbox"
  local card = path.abs(args.card)
  local report = obj({ { "valid", false }, { "cases", json.array() }, { "output", output:gsub("/", "\\") }, { "card", card:gsub("/", "\\") } })
  -- This isolated renderer is a child worker. Unreal's Multiprocess option
  -- avoids launching unrelated AutoSDK/UBT setup into the shared build mutex.
  local editor = ok.proc.popen({ args.editor, root .. "/OpenKoikatsu.uproject", "/Engine/Maps/Entry", "-game", "-Multiprocess",
    "-RenderOffscreen", "-windowed", "-ResX=1600", "-ResY=1000", "-nosound", "-unattended", "-nop4", "-CharacterMCP",
    "-CharacterMCPDir=" .. mailbox, "-CharacterMCPCard=" .. card, "-abslog=" .. output .. "/editor.log" },
    { stdout = "NUL", stderr = "NUL", hide = true })
  local function editor_alive()
    local code = editor:poll()
    if code ~= nil then error(("Editor exited (%d); see editor.log"):format(code), 0) end
  end
  local client
  local okv, err = xpcall(function()
    client = Client.new(mailbox, editor_alive, args.server)
    local start = ok.time.clock()
    local state
    while true do
      local good, s = pcall(function() return (client:call("character_state")) end)
      if good then
        state = s
        if not util.truthy(state.busy) then break end
      elseif editor:poll() ~= nil then
        error(("Editor exited (%d); see editor.log"):format(editor:poll()), 0)
      end
      if ok.time.clock() - start > 300 then error("Editor load timeout", 0) end
      ok.time.sleep(0.5)
    end
    local function session()
      state = client:call("character_state")
      return obj({ { "session", state.session } })
    end
    local function mutation()
      local s = session()
      s.revision = state.revision
      return s
    end
    local function mark(case)
      local cases = report.cases
      cases[#cases + 1] = case
      print(case); io.stdout:flush()
    end
    mark("real_editor_ready")
    local tools = client:rpc("tools/list", json.object()).tools
    report.tools = #tools
    local parameters, offset = json.array(), 0
    while true do
      local page = client:call("parameters_list", merge(session(), obj({ { "offset", offset }, { "limit", 500 } })))
      for _, p in ipairs(page.parameters) do parameters[#parameters + 1] = p end
      offset = page.next_offset
      if offset >= page.total then break end
    end
    assert(count(parameters, function(p) return starts(p.key, "shape.body.") end) == 44)
    assert(count(parameters, function(p) return starts(p.key, "shape.face.") end) == 52)
    assert(count(parameters, function(p) return starts(p.key, "bone.") end) > 1192)
    local keyset = {}
    for _, p in ipairs(parameters) do keyset[p.key] = true end
    assert(util.len(keyset) == #parameters)
    write_json(output .. "/parameters.json", parameters)
    report.parameters = #parameters
    report.supported_bones = count(parameters, function(p) return starts(p.key, "bone.") and p.supported end)
    mark("full_shape_face_bone_property_catalog")
    local controls = client:call("editor_controls", merge(session(), obj({ { "limit", 500 } })))
    write_json(output .. "/controls.json", controls)
    assert(util.any(controls.controls, function(p) return p.key == "control.AutomaticBlinkCheck" end))
    mark("editor_control_inventory")
    local snap = client:call("snapshot_create", session())
    local capture_options = function(extra)
      return merge(mutation(), obj({ { "tpose", true }, { "projection", "orthographic" }, { "ortho_width", 250 }, { "hide_hair", true }, { "hide_accessories", true } }), extra or json.object())
    end
    client:call("editor_control_set", merge(mutation(), obj({ { "key", "control.AutomaticBlinkCheck" }, { "value", false } })))
    client:call("capture_configure", capture_options())
    local _, baseline_images = client:call("capture_views", merge(session(), obj({ { "target", "preview" } })))
    local baseline_front = util.b64decode(baseline_images[1].data)
    path.write_bytes(output .. "/baseline-front.png", baseline_front)
    mark("baseline_rendered_three_views")
    local key = "shape.body.0"
    local before = client:call("parameters_get", merge(session(), obj({ { "keys", { key } } })))
    local baseline = before.parameters[1].value
    client:call("parameters_set", merge(mutation(), obj({ { "edits", { obj({ { "key", key }, { "value", 0.7 } }), obj({ { "key", "invalid.key" }, { "value", 0.2 } }) } } })), true)
    local after = client:call("parameters_get", merge(session(), obj({ { "keys", { key } } })))
    assert(after.parameters[1].value == baseline)
    mark("invalid_batch_has_no_partial_write")
    local old = mutation()
    local target = math.min(1.5, baseline + 0.07)
    client:call("parameters_set", merge(old, obj({ { "edits", { obj({ { "key", key }, { "value", target } }) } } })))
    client:call("parameters_set", merge(old, obj({ { "edits", { obj({ { "key", key }, { "value", baseline } }) } } })), true)
    after = client:call("parameters_get", merge(session(), obj({ { "keys", { key } } })))
    assert(close(after.parameters[1].value, target, 1e-6))
    mark("shape_write_readback_and_stale_revision_rejection")
    local bone
    for _, p in ipairs(parameters) do
      if starts(p.key, "bone.") and p.key:sub(-7) == "scale.x" and p.supported then bone = p break end
    end
    assert(bone, "no supported bone scale.x parameter")
    local bone_target = math.min(bone.max, bone.value + 0.01)
    client:call("parameters_set", merge(mutation(), obj({ { "edits", { obj({ { "key", bone.key }, { "value", bone_target } }) } } })))
    local read = client:call("parameters_get", merge(session(), obj({ { "keys", { bone.key } } })))
    assert(close(read.parameters[1].value, bone_target, 1e-6))
    mark("real_bone_write_readback")
    client:call("editor_control_set", merge(mutation(), obj({ { "key", "control.AutomaticBlinkCheck" }, { "value", false } })))
    client:call("capture_configure", capture_options())
    local images, pixels = client:call("capture_views", merge(session(), obj({ { "target", "preview" } })))
    assert(#pixels == 3)
    local revisions, hashes = {}, {}
    for _, view in ipairs(images.images) do revisions[tostring(view.revision)] = true end
    assert(util.len(revisions) == 1)
    for _, p in ipairs(pixels) do hashes[ok.hash.sha256(util.b64decode(p.data))] = true end
    assert(util.len(hashes) == 3, "Three views must contain different rendered pixels")
    assert(util.b64decode(pixels[1].data) ~= baseline_front, "Body/bone editing must change rendered pixels")
    for index, content in ipairs(pixels) do
      local data = util.b64decode(content.data)
      assert(data:sub(1, 8) == "\137PNG\r\n\26\n")
      local width, height = png_size(data)
      assert(width > 100 and height > 100)
      path.write_bytes(("%s/view-%d.png"):format(output, index - 1), data)
    end
    mark("real_rendered_three_views_same_revision")
    local _, content = client:call("capture_region", merge(session(), obj({ { "target", "rect" }, { "rect", obj({ { "x", 0 }, { "y", 0 }, { "width", 320 }, { "height", 480 } }) } })))
    local data = util.b64decode(content[1].data)
    local rw, rh = png_size(data)
    assert(rw == 320 and rh == 480)
    path.write_bytes(output .. "/region.png", data)
    mark("exact_region_capture_dimensions")
    local color
    for _, p in ipairs(parameters) do if p.key == "hair.parts.0.baseColor.0" then color = p break end end
    assert(color, "hair.parts.0.baseColor.0 missing")
    local color_target = math.abs(color.value - 0.17) > 0.01 and 0.17 or 0.83
    local staged = client:call("parameters_set", merge(mutation(), obj({ { "edits", { obj({ { "key", color.key }, { "value", color_target } }) } } })))
    assert(staged.pending_properties == 1)
    client:call("character_apply", mutation())
    local color_readback = client:call("parameters_get", merge(session(), obj({ { "keys", { color.key } } })))
    assert(close(color_readback.parameters[1].value, color_target, 1e-6))
    mark("staged_property_apply_and_reloaded_readback")
    client:call("snapshot_restore", merge(mutation(), obj({ { "snapshot", snap.snapshot } })))
    local restored = client:call("parameters_get", merge(session(), obj({ { "keys", { key } } })))
    assert(close(restored.parameters[1].value, baseline, 1e-6))
    mark("snapshot_restore_body_readback")
    local saved = client:call("character_save", merge(mutation(), obj({ { "name", "MCP audit" } })))
    assert(path.is_file(saved.saved_card))
    report.saved_card = saved.saved_card
    mark("save_new_character_card")
    client:call("character_load", merge(mutation(), obj({ { "path", saved.saved_card } })))
    local reopened = client:call("parameters_get", merge(session(), obj({ { "keys", { key, bone.key, color.key } } })))
    local reopened_values = {}
    for _, p in ipairs(reopened.parameters) do reopened_values[p.key] = p.value end
    assert(close(reopened_values[key], baseline, 1e-6))
    assert(close(reopened_values[bone.key], bone.value, 1e-6))
    assert(close(reopened_values[color.key], color.value, 1e-6))
    mark("saved_card_reopen_preserves_shape_bone_and_property")
    client:call("editor_action", merge(mutation(), obj({ { "command", "clothing" }, { "clothing_kind", 102 } })))
    client:call("capture_configure", merge(mutation(), obj({ { "tpose", true }, { "hide_hair", true }, { "hide_accessories", true } })))
    local geometry = client:call("body_geometry", session())
    assert(geometry.bounds_method == "current_posed_triangle_vertices")
    assert(geometry.vertex_count > 1000 and geometry.triangle_count > 1000,
      ("Missing body geometry: vertices=%s triangles=%s extent=%s"):format(util.str(geometry.vertex_count), util.str(geometry.triangle_count), util.repr(geometry.extent)))
    assert(#geometry.landmarks > 100)
    assert(geometry.extent[3] > 100)
    mark("exact_posed_geometry_and_skeleton_landmarks")
    -- Exercise every native body/face key through the actual editor, then restore.
    local originals, native_keys = {}, {}
    for _, p in ipairs(parameters) do
      if starts(p.key, "shape.body.") or starts(p.key, "shape.face.") then originals[p.key] = p.value; native_keys[#native_keys + 1] = p.key end
    end
    local edits = {}
    for _, k in ipairs(native_keys) do edits[#edits + 1] = obj({ { "key", k }, { "value", math.min(1.9, originals[k] + 0.013) } }) end
    client:call("parameters_set", merge(mutation(), obj({ { "edits", edits } })))
    read = client:call("parameters_get", merge(session(), obj({ { "keys", native_keys } })))
    for i, p in ipairs(read.parameters) do assert(math.abs(p.value - edits[i].value) < 1e-5) end
    local restore = {}
    for _, k in ipairs(native_keys) do restore[#restore + 1] = obj({ { "key", k }, { "value", originals[k] } }) end
    client:call("parameters_set", merge(mutation(), obj({ { "edits", restore } })))
    mark("all_96_native_parameters_real_batch_write_read_restore")
    local z0, z1 = geometry.min[3], geometry.max[3]
    local heights = {}
    for _, fraction in ipairs({ 0.45, 0.55, 0.65 }) do heights[#heights + 1] = z0 + (z1 - z0) * fraction end
    local measured = client:call("body_sections", merge(session(), obj({ { "heights", heights } })))
    write_json(output .. "/sections.json", measured)
    assert(util.any(measured.sections, function(s) return util.truthy(s.closed_loops) end), "Real body must have measurable closed sections")
    for _, s in ipairs(measured.sections) do for _, loop in ipairs(s.closed_loops) do assert(loop.perimeter > 0) end end
    mark("real_triangle_cross_sections")
    local masks, contents = client:call("body_masks", session())
    local views = { "front", "side", "back" }
    for i, view in ipairs(views) do
      if contents[i] then path.write_bytes(("%s/target-%s.png"):format(output, view), util.b64decode(contents[i].data)) end
    end
    local function targets(extra)
      local o = session()
      for _, v in ipairs(views) do o[v] = (("%s/target-%s.png"):format(output, v)):gsub("/", "\\") end
      return merge(o, extra)
    end
    local reference = client:call("reference_prepare", targets(obj({ { "physical_height", geometry.extent[3] } })))
    local compare_target = client:call("body_compare", merge(session(), obj({ { "reference", reference.reference } })))
    report.target_alignment_loss = compare_target.loss
    assert(compare_target.loss < 0.035, "Prepared same-body target alignment must be close")
    mark("real_three_view_body_masks_reference_alignment")
    -- Targets generated with a known camera retain exact projection; natural
    -- image alignment above remains independently checked.
    reference = client:call("reference_prepare", targets(obj({ { "projection_frame", masks.frame } })))
    local identical = client:call("body_compare", merge(session(), obj({ { "reference", reference.reference } })))
    assert(identical.loss < 0.005, "Known projection must preserve target framing")
    local fitting_keys = { "shape.body.0", "shape.body.22", "shape.body.23" }
    local perturb = {}
    for i, d in ipairs({ -0.12, 0.18, -0.15 }) do perturb[i] = obj({ { "key", fitting_keys[i] }, { "value", originals[fitting_keys[i]] + d } }) end
    client:call("parameters_set", merge(mutation(), obj({ { "edits", perturb } })))
    local before_fit = client:call("body_compare", merge(session(), obj({ { "reference", reference.reference } })))
    local sensitivity = client:call("body_sensitivity", merge(mutation(), obj({ { "reference", reference.reference }, { "keys", fitting_keys }, { "step_fraction", 0.01 } })))
    assert(sensitivity.stop_reason == "sensitivity_complete")
    local changed = false
    for _, s in ipairs(sensitivity.sensitivities) do
      for _, p in ipairs(s.probes) do
        if p.silhouette_change_fraction ~= json.null and p.silhouette_change_fraction ~= nil and p.silhouette_change_fraction > 0 then changed = true end
      end
    end
    assert(changed)
    local sensitivity_read = client:call("parameters_get", merge(session(), obj({ { "keys", fitting_keys } })))
    for i, p in ipairs(sensitivity_read.parameters) do assert(math.abs(p.value - perturb[i].value) < 1e-5) end
    mark("real_parameter_sensitivity_and_restoration")
    local job = client:call("body_fit_start", merge(mutation(), obj({ { "reference", reference.reference }, { "keys", fitting_keys },
      { "max_evaluations", 120 }, { "step_fraction", 0.02 }, { "target_loss", 0.004 } })))
    local deadline = ok.time.clock() + 1800
    local fitted
    while true do
      local job_state = client:call("body_fit_status", obj({ { "job", job.job } }))
      if job_state.status == "completed" then fitted = job_state.result break end
      if job_state.status == "failed" or job_state.status == "cancelled" or job_state.status == "interrupted" then error(json.encode(job_state), 0) end
      assert(ok.time.clock() < deadline, "Background fit exceeded audit deadline")
      ok.time.sleep(0.5)
    end
    local after_fit = client:call("body_compare", merge(session(), obj({ { "reference", reference.reference } })))
    report.fitting = obj({ { "before_loss", before_fit.loss }, { "after_loss", after_fit.loss }, { "target_alignment_loss", compare_target.loss }, { "run", fitted } })
    assert(after_fit.loss < before_fit.loss * 0.7, "Generic real-body search must materially reduce silhouette error")
    assert(math.abs(after_fit.loss - fitted.best_loss) < 1e-6)
    mark("real_unknown_target_bounded_automatic_body_fit")
    local fitted_save = client:call("character_save", merge(mutation(), obj({ { "name", "MCP fitted" } })))
    client:call("character_load", merge(mutation(), obj({ { "path", fitted_save.saved_card } })))
    local fit_read = client:call("parameters_get", merge(session(), obj({ { "keys", fitting_keys } })))
    for i, p in ipairs(fit_read.parameters) do assert(math.abs(p.value - fitted.best_values[i]) < 1e-5) end
    mark("fitted_character_save_reopen_preserves_best_values")
    report.valid = true
  end, debug.traceback)
  if not okv then
    report.error = tostring(err):match("^[^\n]*")
    report.traceback = tostring(err)
  end
  write_json(output .. "/report.json", report)
  if client then client:close() end
  -- Only terminate the exact process launched by this audit.
  if editor:poll() == nil then editor:kill() end
  editor:wait(15)
  if not okv then io.stderr:write(tostring(err), "\n"); os.exit(1) end
end

if ok.is_main() then main() end
