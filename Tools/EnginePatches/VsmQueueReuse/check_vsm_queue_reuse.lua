-- Static contract check for the VSM marking-queue reuse candidate.
-- Reads the engine shader, writes only next to this script, and does not modify the engine file.
local ok = require "ok"
local json, P, U = ok.json, ok.path, ok.util
local O = json.object

local SHADER = "Engine/Shaders/Private/VirtualShadowMaps/VirtualShadowMapBuildPerPageDrawCommands.usf"
local ENGINE = ok.engine_root() .. "/" .. SHADER
local ROOT = ok.script_dir
local KERNEL = P.join(ROOT, "VsmMarkingQueueReuse.kernel.hlsl")
local CANDIDATE = P.join(ROOT, "VsmMarkingQueueReuse.usf")
local PATCH = P.join(ROOT, "VsmMarkingQueueReuse.patch")
local META = P.join(ROOT, "VsmMarkingQueueReuse.meta.json")

local BANNED_IN_BARRIER_COND = { "bItemValid", "bViewVisible", "bDoLargeJob", "bMarkThisMip", "bIsSmallJob", "bHzbCulled",
  "WorkSetup", "bIsVisible", "Overlaps", "bViewHZB", "Cull.", "bThisJobOverflow", "NumMappedPages" }

local function exit_msg(msg) io.stdout:flush(); io.stderr:write(msg, "\n"); os.exit(1) end
local function sha256(data)
  local tmp = P.temp(".bin"); P.write_bytes(tmp, data)
  local d = ok.hash.file(tmp, "sha256"); P.remove(tmp, true)
  return d
end
local function find(s, sub, init) return s:find(sub, init or 1, true) end
-- str.rfind(sub, 0, stop): last start index (1-based) whose match ends at or before stop (1-based exclusive end).
local function rfind(s, sub, stop)
  local best, i = nil, 1
  while true do
    local a, b = s:find(sub, i, true)
    if not a or b > stop then return best end
    best, i = a, a + 1
  end
end
-- Python str.splitlines() for the separators that occur in text files.
local function splitlines(s)
  local out, i, n = {}, 1, #s
  while i <= n do
    local a, b = s:find("\r\n", i, true)
    local c = s:find("[\n\r\v\f\28\29\30]", i)
    if not c then out[#out + 1] = s:sub(i); break end
    out[#out + 1] = s:sub(i, c - 1)
    i = (a == c) and b + 1 or c + 1
  end
  return out
end

local function strip_comments(text)
  text = text:gsub("/%*.-%*/", "")
  text = text:gsub("//[^\n]*", "")
  return text
end

local function extract_function(text, name)
  local start = find(text, "void " .. name .. "(")
  if not start then exit_msg("missing " .. name) end
  -- Include the preceding attribute line when present.
  local attr = rfind(text, "[numthreads", start - 1)
  -- Python text.find("void ", attr, start): the match must end before `start`.
  local v = attr and find(text, "void ", attr)
  if v and v + 4 > start - 1 then v = nil end
  if v == start then start = attr end
  local brace = find(text, "{", start)
  local depth = 0
  for index = brace, #text do
    local c = text:sub(index, index)
    if c == "{" then depth = depth + 1
    elseif c == "}" then
      depth = depth - 1
      if depth == 0 then return text:sub(start, index) end
    end
  end
  exit_msg("unclosed " .. name)
end

local function starts_if_for(code) return code:match("^if%f[^%w_]") ~= nil or code:match("^for%f[^%w_]") ~= nil end

-- Return {line, active if/for conditions} for each group barrier.
local function barrier_conditions(fn)
  local lines = splitlines(strip_comments(fn))
  local stack, found, depth, pending = {}, {}, 0, nil
  for line_no, line in ipairs(lines) do
    local code = U.strip(line)
    local skip = false
    if pending == nil and starts_if_for(code) then
      local kind = code:sub(1, 2) == "if" and "if" or "for"
      pending = { kind, code, nil }
      if find(code, "{") then
        pending[3] = depth + 1
        stack[#stack + 1] = pending
        pending = nil
      end
      skip = true
    end
    if not skip then
      if pending ~= nil then
        pending[2] = pending[2] .. " " .. code
        if find(code, "{") then
          pending[3] = depth + 1
          stack[#stack + 1] = pending
          pending = nil
        end
      end
      if find(line, "{") then depth = depth + U.count(line, "{") end
      if find(code, "GroupMemoryBarrierWithGroupSync") then
        local conds = {}
        for i, item in ipairs(stack) do conds[i] = item[2] end
        found[#found + 1] = { line_no, conds }
      end
      if find(line, "}") then
        depth = depth - U.count(line, "}")
        local kept = {}
        for _, item in ipairs(stack) do if item[3] <= depth then kept[#kept + 1] = item end end
        stack = kept
      end
    end
  end
  return found
end

local function build_candidate(original, kernel)
  local newline = find(original, "\r\n") and "\r\n" or "\n"
  kernel = kernel:gsub("\r\n", "\n"):gsub("\r", "\n")
  if kernel:sub(-1) ~= "\n" then kernel = kernel .. "\n" end
  kernel = kernel:gsub("\n", newline)
  local start = find(original, "void CullPerPageDrawCommandsCs(")
  start = rfind(original, "[numthreads", start - 1)
  local end_marker = find(original, "Buffer<uint> DrawIndirectArgsBuffer;", start)
  if not end_marker then exit_msg("tail marker missing") end
  -- Keep the blank line that preceded the tail marker.
  local tail = original:sub(end_marker)
  local head = original:sub(1, start - 1)
  local needle = "groupshared uint2 SharedMarkingJobs[MARKING_JOB_QUEUE_SIZE];"
  local insert = needle .. newline .. "groupshared uint GroupFirstPrimaryView;" .. newline .. "groupshared uint GroupPrimaryViewCount;" .. newline .. "groupshared uint GroupMipCount;"
  if not find(head, needle) then exit_msg("groupshared queue declaration missing") end
  if U.count(head, needle) ~= 1 then exit_msg("queue declaration is not unique") end
  head = U.replace(head, needle, insert)
  return head .. kernel .. newline .. tail
end

local INTERFACE_CALLS = {
  "GetUnWrappedDispatchGroupId", "InstanceCullingLoadBalancer_GetNumBatches", "InstanceCullingLoadBalancer_Setup",
  "LoadBatchInfo", "GetItemDataOffset", "LoadInstanceCullingPayload", "UnpackDrawCommandDesc", "GetInstanceSceneData",
  "GetPrimitiveData", "LoadDynamicMeshBounds", "AddOBBWS", "DFHackToFloat", "GetNaniteView", "CalculateInstanceDynamicData",
  "VirtualShadowMapIsWPOAllowed", "ShouldCacheInstanceAsStatic", "ShouldMaterialInvalidateShadowCache", "GetInstanceViewData",
  "CalcClipSpaceRadiusEstimate", "GetScreenRect", "IsDetailGeometry", "VirtualShadowMapGetUncachedScreenRect",
  "VirtualShadowMapGetPageRect", "OverlapsAnyValidPage", "IsVisibleMaskedHZB", "VirtualShadowMapGetMarkPageDirtyFlags",
  "PackJob", "UnpackJob", "CalcPageTableLevelOffset", "MarkPageDirty", "WriteCmd", "StatsBufferInterlockedAdd",
  "StatsBufferInterlockedInc", "StatsBufferInterlockedEnableFlags", "GroupMemoryBarrierWithGroupSync",
}
local INTERFACE_TYPES = {
  "FContextBatchInfo", "FVSMCullingBatchInfo", "FInstanceWorkSetup", "FInstanceCullingPayload", "FDrawCommandDesc",
  "FInstanceSceneData", "FPrimitiveSceneData", "FNaniteView", "FInstanceDynamicData", "FBoxCull", "FFrustumCullData",
  "FScreenRect", "FVirtualShadowMapHandle", "FVirtualSMLevelOffset", "FSharedMarkingJob",
}
local INTERFACE_FILES = {
  "D:\\UE5.8\\Engine\\Shaders\\Private\\VirtualShadowMaps\\VirtualShadowMapBuildPerPageDrawCommands.usf",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\Common.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\ViewData.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\VirtualShadowMaps\\VirtualShadowMapPageOverlap.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\VirtualShadowMaps\\VirtualShadowMapProjectionCommon.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\Nanite\\NaniteCullingCommon.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\Nanite\\NaniteHZBCull.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\Nanite\\NaniteDataDecode.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\InstanceCulling\\InstanceCullingCommon.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\InstanceCulling\\InstanceCullingLoadBalancer.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\WaveOpUtil.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\DynamicMeshBounds.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Shared\\VirtualShadowMapDefinitions.h",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\VirtualShadowMaps\\VirtualShadowMapStats.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\VirtualShadowMaps\\VirtualShadowMapPageCacheCommon.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\ShaderPrint.ush",
  "D:\\UE5.8\\Engine\\Shaders\\Private\\SceneData.ush",
}

-- Name presence in the usf include set. Not a type check and not a DXC compile.
local function interface_check()
  local missing_files, texts = {}, {}
  for _, p in ipairs(INTERFACE_FILES) do
    if P.is_file(p) then texts[#texts + 1] = P.read_text(p) else missing_files[#missing_files + 1] = p end
  end
  local text = table.concat(texts, "\n")
  local missing = {}
  for _, list in ipairs({ INTERFACE_CALLS, INTERFACE_TYPES }) do
    for _, name in ipairs(list) do if not find(text, name) then missing[#missing + 1] = name end end
  end
  if #missing > 0 or #missing_files > 0 then
    exit_msg(("shader interface names missing: %s files: %s"):format(U.repr(missing), U.repr(missing_files)))
  end
  return { missing = json.array({}), files = #INTERFACE_FILES }
end

-- Executable model of the round protocol, not a shader interpreter.
local function schedule_model()
  local function run(name, lanes, views, mips, classify)
    local barriers = {}
    for lane = 1, lanes do barriers[lane] = 0 end
    local decisions, max_jobs, overflows, original_peak, original_queued = 0, 0, 0, 0, 0
    for lane = 1, lanes do barriers[lane] = barriers[lane] + 2 end -- initial queue zero + view-range publish
    for view = 0, views - 1 do
      for lane = 1, lanes do barriers[lane] = barriers[lane] + 1 end -- mip-count publish
      for mip = 0, mips - 1 do
        for lane = 1, lanes do barriers[lane] = barriers[lane] + 1 end -- queue reset
        local jobs = 0
        for lane = 0, lanes - 1 do
          local kind = classify(lane, view, mip)
          if kind == "large" then
            local index = jobs
            jobs = jobs + 1
            original_queued = original_queued + 1
            original_peak = math.max(original_peak, original_queued)
            if index >= 128 then overflows = overflows + 1 end
          end
          if kind == "large" or kind == "small" or kind == "unflagged" then
            -- Eligibility only. Small jobs can still get NumMappedPages 0.
            decisions = decisions + 1
          end
        end
        max_jobs = math.max(max_jobs, jobs)
        for lane = 1, lanes do barriers[lane] = barriers[lane] + 2 end -- publish jobs + drain completion
      end
    end
    for lane = 2, lanes do
      if barriers[lane] ~= barriers[1] then exit_msg(name .. ": divergent barrier count") end
    end
    return { name = name, barriers = barriers[1], decisions = decisions, max_jobs = max_jobs, overflows = overflows, original_queue_peak = original_peak }
  end
  local results = {
    run("directional-64-large", 64, 17, 1, function() return "large" end),
    run("point-light-mixed", 64, 6, 8, function(lane)
      if lane >= 40 then return "invalid" end
      if lane % 11 == 0 then return "hzb" end
      if lane % 5 == 0 then return "small" end
      if lane % 7 == 0 then return "culled" end
      return "large"
    end),
    run("all-invalid", 64, 17, 1, function() return "invalid" end),
    run("unflagged-large-rect", 64, 1, 8, function(lane) return lane < 30 and "unflagged" or "culled" end),
  }
  for _, result in ipairs(results) do
    if result.max_jobs > 64 then exit_msg(("%s: round queued %d jobs"):format(result.name, result.max_jobs)) end
    if result.overflows > 0 then exit_msg(result.name .. ": overflow is reachable") end
  end
  return results
end

local function main()
  local original_bytes = P.read_bytes(ENGINE)
  local original_hash = sha256(original_bytes)
  if original_hash ~= "71eb11cb6277cd8d298a9808601b517922bd2ae65492fc3b408bec86c4429c11" then
    exit_msg("Known original required; use --original-shader to select the verified backup after applying")
  end
  if not utf8.len(original_bytes) then error("UnicodeDecodeError: 'utf-8' codec can't decode the original shader", 0) end
  local original = original_bytes
  local kernel = P.read_text(KERNEL, { keep_bom = true })
  local candidate = build_candidate(original, kernel)
  if not find(candidate, "\r\n") then exit_msg("candidate lost CRLF") end
  -- Engine file is only read.
  P.write_bytes(CANDIDATE, candidate)
  if sha256(P.read_bytes(ENGINE)) ~= original_hash then exit_msg("engine shader changed") end

  local candidate_fn = extract_function(candidate, "CullPerPageDrawCommandsCs")
  extract_function(original, "CullPerPageDrawCommandsCs")
  for _, name in ipairs({ "AllocateCommandInstanceOutputSpaceCs", "OutputCommandInstanceListsCs" }) do
    if extract_function(original, name) ~= extract_function(candidate, name) then exit_msg(name .. " drifted") end
  end

  local required = {
    "MAX_SINGLE_THREAD_MARKING_AREA (8U)",
    "MARKING_JOB_QUEUE_SIZE (NUM_THREADS_PER_GROUP * 2U)",
    "LoadDynamicMeshBounds",
    "VirtualShadowMapIsWPOAllowed",
    "ShouldCacheInstanceAsStatic",
    "ShouldMaterialInvalidateShadowCache",
    "GetInstanceViewData",
    "OverlapsAnyValidPage",
    "IsVisibleMaskedHZB",
    "VirtualShadowMapGetMarkPageDirtyFlags",
    "false /*bInvalidateLODDelta*/",
    "PixelEstRadius *= 0.5f",
    "NumMappedPages = 1U",
    "VSM_STAT_NON_NANITE_INSTANCES_TOTAL",
    "VSM_STAT_NON_NANITE_INSTANCES_FRUSTUM_CULLED",
    "VSM_STAT_NON_NANITE_INSTANCES_PAGE_MASK_CULLED",
    "VSM_STAT_NON_NANITE_INSTANCES_HZB_CULLED",
    "VSM_STAT_NON_NANITE_INSTANCES_DRAWN",
    "VSM_STAT_OVERFLOW_FLAG_MARKING_JOB_QUEUE",
    "VSM_STAT_OVERFLOW_FLAG_VISIBLE_INSTANCES",
    "NumPageAreaDiagnosticSlots",
    "LargeJobIndex >= MARKING_JOB_QUEUE_SIZE",
    "min(MARKING_JOB_QUEUE_SIZE, NumMarkingJobs)",
    "WriteCmd(MipViewId, InstanceId, IndirectArgIndex, ViewCullingFlags, bViewCacheAsStatic)",
    "DrawIndirectArgsBufferOut[IndirectArgIndex * INDIRECT_ARGS_NUM_WORDS + 1]",
  }
  for _, token in ipairs(required) do
    -- Constants live outside the function.
    if not find(candidate_fn, token) and not find(candidate, token) then exit_msg("missing contract token: " .. token) end
  end

  if U.count(candidate_fn, "return") ~= 1 then exit_msg("Cull function must have exactly one return") end
  if not find(candidate_fn, "DispatchGroupId >= InstanceCullingLoadBalancer_GetNumBatches()") then
    exit_msg("early return is no longer the uniform batch reject")
  end
  if strip_comments(candidate_fn):find("%f[%w_]continue%f[^%w_]") then exit_msg("continue can skip a round barrier") end
  if U.count(candidate_fn, "InterlockedAdd(NumSharedMarkingJobs") ~= 1 then exit_msg("expected exactly one queue enqueue per round") end

  local raw_barriers = U.count(strip_comments(candidate_fn), "GroupMemoryBarrierWithGroupSync")
  local barriers = barrier_conditions(candidate_fn)
  if raw_barriers ~= #barriers then exit_msg(("barrier parser missed a site: raw %d parsed %d"):format(raw_barriers, #barriers)) end
  if #barriers < 4 then exit_msg(("expected round barriers, found %d"):format(#barriers)) end
  for _, token in ipairs({
    "WaveReadLaneFirst(GroupPrimaryViewCount)",
    "WaveReadLaneFirst(GroupFirstPrimaryView)",
    "WaveReadLaneFirst(GroupMipCount)",
    "const uint PrimaryViewCount = GroupPrimaryViewCount;",
    "const uint MipCount = GroupMipCount;",
  }) do
    if not find(candidate_fn, token) then exit_msg("missing uniform broadcast: " .. token) end
  end
  for _, b in ipairs(barriers) do
    for _, cond in ipairs(b[2]) do
      for _, banned in ipairs(BANNED_IN_BARRIER_COND) do
        if find(cond, banned) then exit_msg(("barrier on line %d is under divergent condition: %s"):format(b[1], cond)) end
      end
      if U.strip(cond):sub(1, 2) == "if" and find(cond, "GroupThreadIndex") then
        exit_msg(("barrier on line %d is under a lane condition: %s"):format(b[1], cond))
      end
    end
  end
  local any_view, any_mip = false, false
  for _, b in ipairs(barriers) do
    for _, cond in ipairs(b[2]) do
      if U.strip(cond):sub(1, 3) == "for" then
        if find(cond, "PrimaryViewCount") then any_view = true end
        if find(cond, "MipCount") then any_mip = true end
      end
    end
  end
  if not any_view then exit_msg("no barrier is inside the uniform primary-view loop") end
  if not any_mip then exit_msg("no barrier is inside the uniform mip loop") end

  -- The three round barriers sit in the mip loop and outside the enqueue if.
  local mip_barriers = 0
  for _, b in ipairs(barriers) do
    for _, c in ipairs(b[2]) do
      if U.strip(c):sub(1, 3) == "for" and find(c, "MipCount") then mip_barriers = mip_barriers + 1; break end
    end
  end
  if mip_barriers ~= 3 then exit_msg(("mip round should contain 3 barriers, found %d"):format(mip_barriers)) end

  local function index(s, sub, init)
    local i = find(s, sub, init)
    if not i then error("ValueError: substring not found", 0) end
    return i
  end
  local drain_end = index(candidate_fn, "uint NumMappedPages = 0U;", index(candidate_fn, "uint NumMarkingJobs = NumSharedMarkingJobs;"))
  local final_barrier = index(candidate_fn, "GroupMemoryBarrierWithGroupSync();", drain_end)
  if final_barrier < index(candidate_fn, "bVisibleInstancesOverflow |= !WriteCmd", drain_end) then
    exit_msg("round completion barrier must follow drain and command work")
  end
  local results = schedule_model()
  local interface = interface_check()
  local original_lines = splitlines(original)
  local candidate_lines = splitlines(candidate)
  local shader = ok.engine_root() .. "/" .. SHADER
  local diff = ok.difflib.unified_diff(original_lines, candidate_lines, { fromfile = shader, tofile = shader, n = 3, lineterm = "" })
  P.write_text(PATCH, table.concat(diff, "\n") .. "\n", { newline = "\n" })
  local candidate_hash = sha256(P.read_bytes(CANDIDATE))
  local changed = 0
  for _, line in ipairs(ok.difflib.ndiff(original_lines, candidate_lines)) do
    local p2 = line:sub(1, 2)
    if p2 == "+ " or p2 == "- " then changed = changed + 1 end
  end
  local max_jobs = 0
  for _, r in ipairs(results) do max_jobs = math.max(max_jobs, r.max_jobs) end
  local meta = O({
    { "original_sha256", original_hash },
    { "candidate_sha256", candidate_hash },
    { "OriginalPath", shader },
    { "OriginalSHA256", original_hash },
    { "CandidateSHA256", candidate_hash },
    { "Candidate", ".ai-collab/VsmMarkingQueueReuse.usf" },
    { "Patch", ".ai-collab/VsmMarkingQueueReuse.patch" },
    { "EngineBytesUnchanged", #original_bytes },
    { "DiffTouchLines", changed },
    { "BarrierCountInCull", #barriers },
    { "ScheduleMaxJobs", max_jobs },
    { "InterfaceMissing", interface.missing },
  })
  P.write_text(META, json.encode(meta, { indent = 2 }) .. "\n", { newline = "\n" })
  if sha256(P.read_bytes(ENGINE)) ~= original_hash then exit_msg("engine shader changed during emit") end
  print("original_sha256 " .. original_hash)
  print("candidate_sha256 " .. candidate_hash)
  print(("interface_files %d missing %s"):format(interface.files, U.repr(interface.missing)))
  print(("barriers %d"):format(#barriers))
  for _, b in ipairs(barriers) do print(("  barrier line %d: %s"):format(b[1], U.repr(json.array(b[2])))) end
  for _, r in ipairs(results) do
    print(("schedule %s: barriers/lane=%d decisions=%d max_jobs=%d overflows=%d original_queue_peak=%d"):format(
      r.name, r.barriers, r.decisions, r.max_jobs, r.overflows, r.original_queue_peak))
  end
  print(("diff_touch_lines %d"):format(changed))
  print("PASS")
end

local p = ok.args.parser("Static contract check for the VSM marking-queue reuse candidate.")
p:opt("--original-shader", { default = ENGINE })
ENGINE = p:parse(arg).original_shader
ok.main(main)
