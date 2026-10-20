-- Export every Koikatsu Sunshine animation clip inventory, Studio animation
-- catalogue, and FaceBlendShape (mouth/eye/eyebrow) pattern table.
--
-- Base game data comes from <game>/abdata; mod data comes from the TSV scans
-- produced by scan_mods.lua (mod_anim_bundles.tsv / mod_studio_info.tsv /
-- mod_head_prefabs.tsv).
--
-- Outputs (UTF-8, tab separated) under Saved/anim_export/:
--   animation_sources.tsv        one row per animation bundle
--   animation_clips.tsv          one row per AnimationClip
--   studio_catalog.tsv           Anime_/HAnime_/HandAnime_/AnimeGroup_/AnimeCategory_
--   fbs_patterns.tsv             every FaceBlendShape PtnSet Close/Open pair
--   fbs_channels.tsv             blend shape channel names of every target mesh
--
-- Run:
--   Tools\lua\lua.exe Tools/anim_export/export_anim_mouth.lua [game_root] [out_dir]
--
-- Module use (e.g. Tools/GamePoseProbe):
--   local exporter = dofile(ok.root .. "/Tools/anim_export/export_anim_mouth.lua")
--   local patterns, channels = exporter.tsv_writer(), exporter.tsv_writer()
--   exporter.extract_fbs(bundle_path, "source", card, patterns, channels)
--   -- patterns.rows / channels.rows hold the written rows; :text() gives the TSV
-- `env` is a bundle/serialized file path (UnityPy.load(path) in the original).
local ok = require "ok"
local lfs = require "lfs"
local json, path, util = ok.json, ok.path, ok.util
local M = {}

M.GAME = ok.game_root(true, "\\")
M.OUT = "C:\\portable\\OpenKoikatsu\\Saved\\anim_export"

-- ---------------------------------------------------------------------------
-- base-game animation bundle directories (character motion used by the game)
-- ---------------------------------------------------------------------------
local BASE_ANIM_DIRS = {
  "studio/anime",
  "studio_k/anime",
  "h/anim",
  "esthetic/animator",
  "adv/motion/controller/adv",
  "action/animator",
  "custom",
}
local BASE_ANIM_FILES = { "custom/custompose.unity3d" }
local TWOSHOT = "twoshot"

local FBS_CTRLS = { "BlinkCtrl", "EyebrowCtrl", "EyesCtrl", "MouthCtrl", "CheekCtrl", "TongueCtrl", "NeckCtrl" }

local function norm(p) return (p:gsub("\\", "/")) end
local function join(a, b)
  if a == "" then return b end
  local last = a:sub(-1)
  if last == "\\" or last == "/" then return a .. b end
  return a .. "\\" .. b
end
local function endswith(s, suffix) return s:sub(-#suffix) == suffix end
local function isdir(p) local a = lfs.attributes(p); return a ~= nil and a.mode == "directory" end
local function isfile(p) local a = lfs.attributes(p); return a ~= nil and a.mode ~= "directory" end

-- os.walk(top) in file-system order; calls fn(root, files).
local function walk(top, fn)
  local okv, iter, state = pcall(lfs.dir, top)
  if not okv then return end
  local dirs, files = {}, {}
  for name in iter, state do
    if name ~= "." and name ~= ".." then
      if isdir(join(top, name)) then dirs[#dirs + 1] = name else files[#files + 1] = name end
    end
  end
  fn(top, files)
  for _, d in ipairs(dirs) do
    local full = join(top, d)
    local link = lfs.symlinkattributes(full)
    if not (link and link.mode == "link") then walk(full, fn) end
  end
end

-- os.path.relpath(full, base) with "/" separators (full is inside base).
local function relpath(full, base)
  local f, b = norm(path.abs(full)), norm(path.abs(base)):gsub("/+$", "")
  return f:sub(#b + 2)
end

-- Python tuple repr for log lines.
local function tuple_repr(t)
  local parts = {}
  for i, v in ipairs(t) do parts[i] = util.repr(v) end
  return "(" .. table.concat(parts, ", ") .. ")"
end

local function say(s) io.write(s, "\n"); io.stdout:flush() end

function M.base_anim_bundles()
  local out, seen = {}, {}
  local abdata = join(M.GAME, "abdata")
  local function scan(d)
    if not isdir(d) then return end
    walk(d, function(root, files)
      for _, fn in ipairs(files) do
        if endswith(fn:lower(), ".unity3d") then
          local inner = relpath(join(root, fn), abdata)
          local key = inner:lower()
          if not seen[key] then
            seen[key] = true
            out[#out + 1] = { "base", inner }
          end
        end
      end
    end)
  end
  for _, rel in ipairs(BASE_ANIM_DIRS) do scan(join(abdata, rel:gsub("/", "\\"))) end
  -- TwoShot runs its own controller bundle outside the usual dirs.
  scan(join(abdata, TWOSHOT))
  for _, rel in ipairs(BASE_ANIM_FILES) do
    if not seen[rel:lower()] and isfile(join(abdata, rel)) then out[#out + 1] = { "base", rel } end
  end
  return util.sorted(out)
end

-- (origin, zipmod_path, inner_path) rows of a two-column scan list.
local function read_pairs(file)
  local out = {}
  if not path.is_file(file) then return out end
  for line in (path.read_text(file, { keep_bom = true }) .. "\n"):gmatch("(.-)\n") do
    if line ~= "" then
      local zipmod, inner = line:match("^(.-)\t(.*)$")
      if not zipmod then zipmod, inner = line, "" end
      out[#out + 1] = { zipmod, inner }
    end
  end
  return out
end

function M.mod_anim_bundles()
  local out = {}
  for _, p in ipairs(read_pairs(join(M.OUT, "mod_anim_bundles.tsv"))) do out[#out + 1] = { "mod", p[1], p[2] } end
  return out
end

-- Materialise a source as a file for ok-luakit: returns file, cleanup().
local function source_file(source)
  if source[1] == "base" then return join(join(M.GAME, "abdata"), source[2]:gsub("/", "\\")), function() end end
  local tmp = path.temp(".unity3d")
  ok.kit.call({ "zip-member", source[2], source[3], tmp })
  return tmp, function() path.remove(tmp, true) end
end

-- load_env(source) -> {file=..., close=fn} or nil (prints like the original on failure).
function M.load_env(source)
  local okv, file, cleanup = pcall(source_file, source)
  if okv then
    local probe_ok, err = pcall(ok.kit.call, { "unity-version", file })
    if probe_ok then return { file = file, close = cleanup } end
    cleanup()
    file = err
  end
  say(("  !! load failed %s: %s"):format(tuple_repr(source), util.usub(tostring(file), 1, 120)))
  return nil
end

function M.source_label(source)
  if source[1] == "base" then return "abdata/" .. source[2] end
  return source[2] .. "!" .. source[3]
end

-- csv.writer(f, delimiter="\t") equivalent collecting rows; :writerow(row), :text().
function M.tsv_writer()
  local w = { rows = {}, lines = {} }
  function w:writerow(row)
    local n = 0
    for k in pairs(row) do if type(k) == "number" and k > n then n = k end end
    row.n = row.n or n
    self.rows[#self.rows + 1] = row
    self.lines[#self.lines + 1] = ok.csv.format_row(row, { delimiter = "\t" })
  end
  w.row = w.writerow
  function w:text() return table.concat(self.lines) end
  return w
end

-- ---------------------------------------------------------------------------
-- clip inventory
-- ---------------------------------------------------------------------------
local CLIP_FIELDS = { "m_Name", "m_SampleRate", "m_ClipBindingConstant.genericBindings#len", "m_MuscleClip.m_StartTime", "m_MuscleClip.m_StopTime",
  "m_MuscleClip.m_Clip.data.m_StreamedClip.curveCount", "m_MuscleClip.m_Clip.data.m_DenseClip.m_CurveCount",
  "m_MuscleClip.m_Clip.data.m_DenseClip.m_FrameCount", "m_MuscleClip.m_Clip.data.m_DenseClip.m_SampleRate",
  "m_MuscleClip.m_Clip.data.m_ConstantClip.data#len" }

local function truthy(v) return util.truthy(v) end

function M.clip_fields(clip)
  local d = { sample_rate = "", streamed_curves = "", dense_curves = "", constant_curves = "", bindings = "", frame_count = "", length = "" }
  if truthy(clip.m_SampleRate) then d.sample_rate = clip.m_SampleRate end
  local cbc = clip.m_ClipBindingConstant
  if cbc and cbc["genericBindings#len"] ~= nil then d.bindings = cbc["genericBindings#len"] end
  local mc = clip.m_MuscleClip
  if mc and mc.m_StartTime ~= nil and mc.m_StopTime ~= nil then
    local start, stop = mc.m_StartTime + 0.0, mc.m_StopTime + 0.0
    if stop > start then d.length = util.round(stop - start, 6) end
    local data = mc.m_Clip and mc.m_Clip.data
    if data then
      if data.m_StreamedClip and data.m_StreamedClip.curveCount ~= nil then d.streamed_curves = data.m_StreamedClip.curveCount end
      local dc = data.m_DenseClip
      if dc and dc.m_CurveCount ~= nil then
        d.dense_curves = dc.m_CurveCount
        if dc.m_FrameCount ~= nil then
          d.frame_count = dc.m_FrameCount
          if not truthy(d.length) and dc.m_SampleRate ~= nil and truthy(dc.m_SampleRate) then
            d.length = util.round((dc.m_FrameCount - 1) / (dc.m_SampleRate + 0.0), 6)
          end
        end
      end
      if data.m_ConstantClip and data.m_ConstantClip["data#len"] ~= nil then d.constant_curves = data.m_ConstantClip["data#len"] end
    end
  end
  return d
end

function M.scan_clips(env, origin, label, w_src, w_clip)
  local n_clip, n_ctrl, n_override = 0, 0, 0
  local by_type = {}
  for _, o in ipairs(ok.kit.unity_objects(env.file, { tree = false })) do
    local t = o.type
    by_type[t] = (by_type[t] or 0) + 1
    if t == "AnimatorController" then n_ctrl = n_ctrl + 1
    elseif t == "AnimatorOverrideController" then n_override = n_override + 1 end
  end
  for _, o in ipairs(ok.kit.unity_objects(env.file, { type = "AnimationClip", fields = CLIP_FIELDS })) do
    local f = M.clip_fields(o.tree)
    n_clip = n_clip + 1
    w_clip:writerow({ origin, label, o.tree.m_Name, f.length, f.sample_rate, f.frame_count, f.streamed_curves, f.dense_curves, f.constant_curves, f.bindings, n = 10 })
  end
  w_src:writerow({ origin, label, n_clip, n_ctrl, n_override, by_type.Mesh or 0, n = 6 })
end

-- ---------------------------------------------------------------------------
-- Studio animation catalogue
-- ---------------------------------------------------------------------------
local CAT_CATS = { "AnimeGroup_", "AnimeCategory_", "Anime_", "HAnime_", "HandAnime_" }

-- kind, origin, asset, id, group, category, name, bundle, file,
-- override_bundle, override_file, clip, options
local CAT_ROWS = {}
local CAT_HEADER = { "kind", "origin", "asset", "id", "group", "category", "name", "bundle",
  "file", "override_bundle", "override_file", "clip", "options" }
M.CAT_ROWS = CAT_ROWS

local function strip(s) return util.strip(s) end
local function isdigit(s) return s ~= "" and not s:find("%D") end
local function lstrip_minus(s) return (s:gsub("^%-+", "")) end
-- os.path.basename for Windows paths.
local function basename(p) return p:match("([^/\\]*)$") end
local function splitext_root(name)
  local stem = name:match("^(.*)%.[^.]*$")
  if stem == nil or stem:match("^%.*$") then return name end
  return stem
end

local function last_number(asset)
  local base = splitext_root(basename(asset))
  local parts = {}
  local start = 1
  for i = 1, #base do
    local c = base:sub(i, i)
    if c == "_" or c:find("%s") then parts[#parts + 1] = base:sub(start, i - 1); start = i + 1 end
  end
  parts[#parts + 1] = base:sub(start)
  for i = #parts, 1, -1 do if isdigit(parts[i]) then return parts[i] end end
  return ""
end

local function empty13() local r = {}; for i = 1, 13 do r[i] = "" end; return r end

-- Normalise an integer-like id (strip leading zeros) so '03' == '3'.
local function nz(s)
  s = strip(s or "")
  if isdigit(lstrip_minus(s)) then
    local v = math.tointeger(tonumber(s))
    if v == nil then error("ValueError: invalid literal for int() with base 10: '" .. s .. "'", 0) end
    return tostring(v)
  end
  return s
end

local function catalog_kind(name)
  for _, pre in ipairs({ "AnimeGroup_", "AnimeCategory_", "HAnime_", "HandAnime_", "Anime_" }) do
    if name:sub(1, #pre) == pre then return pre:sub(1, -2) end
  end
  return nil
end

local function excel_rows(tt)
  local rows = {}
  local list = tt.list
  if not truthy(list) then return rows end
  for _, param in ipairs(list) do
    local cells = (type(param) == "table" and json.is_object(param)) and (param.list or {}) or nil
    if cells and #cells > 0 then
      local row = {}
      for i, c in ipairs(cells) do row[i] = (c == json.null) and "" or util.str(c) end
      rows[#rows + 1] = row
    end
  end
  return rows
end

local function csv_rows(text)
  local rows = {}
  text = text:gsub("\r\n", "\n")
  for raw in (text .. "\n"):gmatch("(.-)\n") do
    if strip(raw) ~= "" then
      local row = {}
      for _, c in ipairs(util.split(raw, ",")) do row[#row + 1] = strip(c) end
      rows[#rows + 1] = row
    end
  end
  return rows
end

local function cell_or(r, i) if #r >= i then return strip(r[i]) end return "" end

local function emit_catalog(kind, rows, origin, asset, asset_name)
  for _, r in ipairs(rows) do
    if #r > 0 and isdigit(lstrip_minus(strip(r[1]))) then
      local no = nz(r[1])
      if kind == "AnimeGroup" then
        local row = empty13()
        row[1], row[2], row[3], row[4], row[5] = "group", origin, asset, no, cell_or(r, 2)
        CAT_ROWS[#CAT_ROWS + 1] = row
      elseif kind == "AnimeCategory" then
        -- asset basename AnimeCategory_<ignored>_<group>; row = (category, name)
        local row = empty13()
        row[1], row[2], row[3], row[4] = "category", origin, asset, no
        row[5] = nz(last_number((asset_name ~= nil and asset_name ~= "") and asset_name or asset))
        row[7] = cell_or(r, 2)
        CAT_ROWS[#CAT_ROWS + 1] = row
      elseif kind == "HandAnime" then
        -- no,name,bundle,file,clip
        local row = empty13()
        row[1], row[2], row[3], row[4] = "handanime", origin, asset, no
        row[7], row[8], row[9], row[12] = cell_or(r, 2), cell_or(r, 3), cell_or(r, 4), cell_or(r, 5)
        CAT_ROWS[#CAT_ROWS + 1] = row
      elseif kind == "HAnime" then
        -- no,group,category,name,bundle,file,overrideBundle,overrideFile,clip,breast,dyn...,pv
        if #r >= 5 then
          local g, c = nz(r[2]), nz(r[3])
          if isdigit(lstrip_minus(g)) and isdigit(lstrip_minus(c)) then
            local row = empty13()
            row[1], row[2], row[3], row[4], row[5], row[6] = "hanime", origin, asset, no, g, c
            row[7], row[8], row[9], row[10], row[11], row[12] = cell_or(r, 4), cell_or(r, 5), cell_or(r, 6), cell_or(r, 7), cell_or(r, 8), cell_or(r, 9)
            row[13] = #r > 9 and table.concat(r, ",", 10) or ""
            CAT_ROWS[#CAT_ROWS + 1] = row
          end
        end
      else -- Anime
        if #r >= 5 then
          local g, c = nz(r[2]), nz(r[3])
          if isdigit(lstrip_minus(g)) and isdigit(lstrip_minus(c)) then
            local row = empty13()
            row[1], row[2], row[3], row[4], row[5], row[6] = "anime", origin, asset, no, g, c
            row[7], row[8], row[9], row[12] = cell_or(r, 4), cell_or(r, 5), cell_or(r, 6), cell_or(r, 7)
            row[13] = #r > 7 and table.concat(r, ",", 8) or ""
            CAT_ROWS[#CAT_ROWS + 1] = row
          end
        end
      end
    end
  end
end

-- Add group/category names (base wins) and write the catalogue.
local function resolve_catalog(w)
  local groups, cats = {}, {}
  for _, row in ipairs(CAT_ROWS) do
    if row[1] == "group" then
      if groups[row[4]] == nil then groups[row[4]] = row[5] end
    elseif row[1] == "category" then
      local k = row[5] .. "\0" .. row[4]
      if cats[k] == nil then cats[k] = row[7] end
    end
  end
  local header = {}
  for i = 1, 7 do header[#header + 1] = CAT_HEADER[i] end
  header[#header + 1] = "group_name"; header[#header + 1] = "category_name"
  for i = 8, 13 do header[#header + 1] = CAT_HEADER[i] end
  w:writerow(header)
  for _, row in ipairs(CAT_ROWS) do
    local g, c = row[5], row[6]
    local out = {}
    for i = 1, 7 do out[#out + 1] = row[i] end
    out[#out + 1] = groups[g] or ""
    out[#out + 1] = cats[g .. "\0" .. c] or ""
    for i = 8, 13 do out[#out + 1] = row[i] end
    w:writerow(out)
  end
end

local function catalog_from_env(env, origin, asset)
  local n = 0
  for _, o in ipairs(ok.kit.unity_objects(env.file, { type = "MonoBehaviour", fields = { "m_Name", "list" } })) do
    local tt = o.tree
    if tt.list ~= nil then
      local name = norm(util.str(tt.m_Name or ""))
      local kind = catalog_kind(name)
      if kind ~= nil then
        emit_catalog(kind, excel_rows(tt), origin, asset, name)
        n = n + 1
      end
    end
  end
  return n
end

-- bytes.decode("utf-8-sig", "replace")
local function decode_utf8_replace(s)
  if s:sub(1, 3) == "\239\187\191" then s = s:sub(4) end
  if utf8.len(s) then return s end
  local out, i, n = {}, 1, #s
  local R = "\239\191\189"
  while i <= n do
    local c = s:byte(i)
    if c < 0x80 then out[#out + 1] = string.char(c); i = i + 1
    else
      local need, lo, hi = 0, 0x80, 0xBF
      if c >= 0xC2 and c <= 0xDF then need = 1
      elseif c >= 0xE0 and c <= 0xEF then need = 2; if c == 0xE0 then lo = 0xA0 elseif c == 0xED then hi = 0x9F end
      elseif c >= 0xF0 and c <= 0xF4 then need = 3; if c == 0xF0 then lo = 0x90 elseif c == 0xF4 then hi = 0x8F end end
      if need == 0 then out[#out + 1] = R; i = i + 1
      else
        local j, good = i + 1, true
        for k = 1, need do
          local b = s:byte(j)
          local l, h = 0x80, 0xBF
          if k == 1 then l, h = lo, hi end
          if not b or b < l or b > h then good = false break end
          j = j + 1
        end
        if good then out[#out + 1] = s:sub(i, j - 1) else out[#out + 1] = R end
        i = j
      end
    end
  end
  return table.concat(out)
end

-- ---------------------------------------------------------------------------
-- FaceBlendShape pattern extraction
-- ---------------------------------------------------------------------------
-- Objects of one type: from a file (pruned trees via ok-luakit) or from an
-- ok.kit.unity_objects() array with full trees.
local function objects_of(env, kind, fields)
  if type(env) == "string" then return ok.kit.unity_objects(env, { type = kind, fields = fields }) end
  if env.file then return ok.kit.unity_objects(env.file, { type = kind, fields = fields }) end
  local out = {}
  for _, o in ipairs(env) do if o.type == kind and o.tree then out[#out + 1] = o end end
  return out
end

-- extract_fbs(env, origin, asset, w_patterns, w_channels) -> FaceBlendShape components found.
-- env: bundle path, {file=path}, or an ok.kit.unity_objects(path) array (with trees).
-- Rows go to w_patterns / w_channels via :writerow(row) (or :row(row)).
function M.extract_fbs(env, origin, asset, w_patterns, w_channels)
  local function emit(w, row) if w.writerow then w:writerow(row) else w:row(row) end end
  local go_names, mesh_of_go, mesh_names, mesh_channels = {}, {}, {}, {}
  for _, o in ipairs(objects_of(env, "GameObject", { "m_Name" })) do
    go_names[o.path_id] = o.tree.m_Name or ""
  end
  for _, o in ipairs(objects_of(env, "Mesh", { "m_Name", "m_Shapes.channels" })) do
    mesh_names[o.path_id] = o.tree.m_Name or ""
    local chans = {}
    local shapes = o.tree.m_Shapes
    if shapes and shapes.channels then
      for i, c in ipairs(shapes.channels) do chans[i] = c.name end
    end
    mesh_channels[o.path_id] = chans
  end
  for _, o in ipairs(objects_of(env, "SkinnedMeshRenderer", { "m_GameObject", "m_Mesh" })) do
    local d = o.tree
    if d.m_GameObject and d.m_Mesh then mesh_of_go[d.m_GameObject.m_PathID] = d.m_Mesh.m_PathID end
  end
  local fields = { "m_GameObject" }
  for _, k in ipairs(FBS_CTRLS) do fields[#fields + 1] = k end
  local found = 0
  for _, o in ipairs(objects_of(env, "MonoBehaviour", fields)) do
    local d = o.tree
    local any = false
    for _, k in ipairs(FBS_CTRLS) do if d[k] ~= nil then any = true break end end
    if any then
      local root_go = d.m_GameObject and d.m_GameObject.m_PathID
      local root = (root_go ~= nil and go_names[root_go]) or ""
      found = found + 1
      for _, ctrl in ipairs(FBS_CTRLS) do
        local spec = d[ctrl]
        if type(spec) == "table" and json.is_object(spec) and spec.FBSTarget ~= nil then
          local targets = truthy(spec.FBSTarget) and spec.FBSTarget or {}
          for ti0, target in ipairs(targets) do
            local ti = ti0 - 1
            if type(target) == "table" and json.is_object(target) then
              local op = truthy(target.ObjTarget) and target.ObjTarget or {}
              local file_id = op.m_FileID
              if file_id == nil or file_id == 0 or file_id == json.null then
                local go = op.m_PathID
                local tname = (go ~= nil and go_names[go]) or ""
                local mesh_pid = go ~= nil and mesh_of_go[go] or nil
                local mesh_name = (mesh_pid ~= nil and mesh_names[mesh_pid]) or ""
                local channels = truthy(mesh_pid) and (mesh_channels[mesh_pid] or {}) or {}
                for ci, ch in ipairs(channels) do
                  emit(w_channels, { origin, asset, root, ctrl, ti, tname, mesh_name, ci - 1, ch, n = 9 })
                end
                local ptns = truthy(target.PtnSet) and target.PtnSet or {}
                for pi0, pat in ipairs(ptns) do
                  if type(pat) == "table" and json.is_object(pat) then
                    local close, open_ = pat.Close, pat.Open
                    if close == json.null then close = nil end
                    if open_ == json.null then open_ = nil end
                    local cn = (math.type(close) == "integer" and close >= 0 and close < #channels) and channels[close + 1] or ""
                    local on = (math.type(open_) == "integer" and open_ >= 0 and open_ < #channels) and channels[open_ + 1] or ""
                    emit(w_patterns, { origin, asset, root, ctrl, ti, tname, mesh_name, pi0 - 1, close, cn, open_, on, n = 12 })
                  end
                end
              end
            end
          end
        end
      end
    end
  end
  return found
end

-- ---------------------------------------------------------------------------
-- main
-- ---------------------------------------------------------------------------
local function main()
  M.GAME = arg[1] or M.GAME
  M.OUT = arg[2] or M.OUT
  local GAME, OUT = M.GAME, M.OUT
  path.mkdir(OUT)
  local w_src, w_clip, w_cat, w_pat, w_chan = M.tsv_writer(), M.tsv_writer(), M.tsv_writer(), M.tsv_writer(), M.tsv_writer()
  w_src:writerow({ "origin", "source", "clips", "animator_controllers", "animator_overrides", "meshes" })
  w_clip:writerow({ "origin", "source", "clip", "length_sec", "sample_rate", "frames",
    "streamed_curves", "dense_curves", "constant_curves", "bindings" })
  w_pat:writerow({ "origin", "asset", "prefab", "controller", "target_index", "target_go",
    "mesh", "pattern", "close", "close_name", "open", "open_name" })
  w_chan:writerow({ "origin", "asset", "prefab", "controller", "target_index", "target_go",
    "mesh", "channel_index", "channel_name" })

  -- ---- animation bundles (base + mods) ----
  local sources = {}
  for _, b in ipairs(M.base_anim_bundles()) do sources[#sources + 1] = { "base", b[2] } end
  for _, m in ipairs(M.mod_anim_bundles()) do sources[#sources + 1] = m end
  local limit = os.getenv("OK_ANIM_LIMIT")
  if limit and limit ~= "" then sources = util.slice(sources, 1, math.tointeger(tonumber(limit))) end
  say(("ok-extract: %d animation bundles"):format(#sources))
  local t0 = ok.time.time()
  for i, src in ipairs(sources) do
    local env = M.load_env(src)
    if env ~= nil then
      M.scan_clips(env, src[1], M.source_label(src), w_src, w_clip)
      env.close()
    end
    if i % 10 == 0 or i == #sources then
      say(("  clips [%d/%d] %.0fs"):format(i, #sources, ok.time.time() - t0))
    end
  end

  -- ---- Studio catalogue: base game ----
  local info_dir = join(join(GAME, "abdata"), "studio\\info")
  if isdir(info_dir) then
    local names = {}
    for name in lfs.dir(info_dir) do if name ~= "." and name ~= ".." then names[#names + 1] = name end end
    table.sort(names)
    for _, fn in ipairs(names) do
      if endswith(fn:lower(), ".unity3d") then
        local asset = "abdata/studio/info/" .. fn
        local env = M.load_env({ "base", "studio/info/" .. fn })
        if env ~= nil then
          catalog_from_env(env, "base", asset)
          env.close()
        end
      end
    end
  end
  -- ---- Studio catalogue: mods ----
  local tsv = join(OUT, "mod_studio_info.tsv")
  if path.is_file(tsv) then
    local seen = {}
    for _, p in ipairs(read_pairs(tsv)) do
      local zipmod, inner = p[1], p[2]
      local fname = basename(inner)
      local wanted = false
      for _, pre in ipairs(CAT_CATS) do if fname:lower():sub(1, #pre) == pre:lower() then wanted = true break end end
      local key = zipmod .. "\0" .. inner
      if wanted and not seen[key] then
        seen[key] = true
        local asset = zipmod .. "!" .. inner
        local okv, err = pcall(function()
          local tmp = path.temp(".bin")
          local cleanup = function() path.remove(tmp, true) end
          local fine, e = pcall(function()
            ok.kit.call({ "zip-member", zipmod, inner, tmp })
            if endswith(inner:lower(), ".csv") then
              local kind = catalog_kind(fname)
              if kind then emit_catalog(kind, csv_rows(decode_utf8_replace(path.read_bytes(tmp))), "mod", asset, fname) end
            else
              catalog_from_env({ file = tmp }, "mod", asset)
            end
          end)
          cleanup()
          if not fine then error(e, 0) end
        end)
        if not okv then say(("  !! catalog %s: %s"):format(asset, util.usub(tostring(err), 1, 100))) end
      end
    end
  end

  -- ---- FaceBlendShape patterns ----
  local heads = { { "base", "chara/bo_head_00.unity3d" } }
  for _, p in ipairs(read_pairs(join(OUT, "mod_head_prefabs.tsv"))) do heads[#heads + 1] = { "mod", p[1], p[2] } end
  for _, h in ipairs(heads) do
    local env = M.load_env(h)
    if env ~= nil then
      local n = M.extract_fbs(env, h[1], M.source_label(h), w_pat, w_chan)
      say(("ok-extract: FBS %s -> %d components"):format(M.source_label(h), n))
      env.close()
    end
  end

  resolve_catalog(w_cat)
  say(("ok-extract: catalogue rows=%d"):format(#CAT_ROWS))

  path.write_bytes(join(OUT, "animation_sources.tsv"), w_src:text())
  path.write_bytes(join(OUT, "animation_clips.tsv"), w_clip:text())
  path.write_bytes(join(OUT, "studio_catalog.tsv"), w_cat:text())
  path.write_bytes(join(OUT, "fbs_patterns.tsv"), w_pat:text())
  path.write_bytes(join(OUT, "fbs_channels.tsv"), w_chan:text())
  say(("ok-extract: done in %.0fs -> %s"):format(ok.time.time() - t0, OUT))
  return 0
end

if ok.is_main() then ok.main(main) end
return M
