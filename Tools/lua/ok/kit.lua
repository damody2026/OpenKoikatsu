-- Bridge to ok-luakit.exe (Rust): heavy work Lua cannot do well (image codecs,
-- Unity bundles, zstd/lz4, msgpack, large-file hashing, fonts, PDF text,
-- Chinese script conversion). Every subcommand prints one JSON value.
local M = {}

-- Locate ok-luakit.exe; (re)build it with cargo when missing or older than its
-- sources, so scripts never run against a stale binary. Checked once per process.
local checked
local function exe()
  local ok = require "ok"
  local path = require "ok.path"
  local crate = ok.root .. "/Tools/ok-luakit"
  local p = crate .. "/target/release/ok-luakit.exe"
  if checked then return p end
  local built = path.mtime(p)
  local newest = math.max(path.mtime(crate .. "/Cargo.toml") or 0, path.mtime(crate .. "/Cargo.lock") or 0)
  for _, f in ipairs(path.glob(crate .. "/src", "*.rs")) do newest = math.max(newest, path.mtime(f) or 0) end
  if not built or newest > built then
    io.stderr:write(built and "ok-luakit sources changed; rebuilding...\n" or "ok-luakit.exe missing; building...\n")
    local res = require("ok.proc").run({ "cargo", "build", "--release", "--quiet", "--manifest-path", crate .. "/Cargo.toml" }, { stdout_file = "NUL" })
    if not res.ok or not path.is_file(p) then
      error("ok-luakit build failed; run: cargo build --release --manifest-path Tools/ok-luakit/Cargo.toml", 3)
    end
  end
  checked = true
  return p
end
M.exe = exe

-- call({subcommand, args...}, {input=string}) -> decoded JSON result; errors on failure.
function M.call(args, opts)
  local proc, json = require "ok.proc", require "ok.json"
  local list = { exe() }
  for _, a in ipairs(args) do list[#list + 1] = tostring(a) end
  local res = proc.run(list, { capture = true, input = opts and opts.input, text = false })
  if not res.ok then
    error(("ok-luakit %s failed (%d): %s"):format(args[1], res.code, (res.stderr or ""):gsub("%s+$", "")), 2)
  end
  local out = res.stdout:gsub("^%s+", ""):gsub("%s+$", "")
  if out == "" then return nil end
  -- unity-dump marks non-finite floats as {"$float": "..."}; json.decode reads the bare tokens.
  if out:find('{"$float":', 1, true) then
    out = out:gsub('{"%$float":"(%-?Infinity)"}', "%1"):gsub('{"%$float":"NaN"}', "NaN")
  end
  return json.decode(out)
end

-- Decode bytes marked {"$bytes": hex} produced by unity/msgpack dumps.
function M.bytes(v)
  if type(v) == "table" and v["$bytes"] then return require("ok.util").unhex(v["$bytes"]) end
  if type(v) == "table" and v["$file"] then return require("ok.path").read_bytes(v["$file"]) end
  return v
end

-- Data codecs (file to file).
function M.zstd_decompress(src, dst) return M.call({ "zstd-d", src, dst }) end
function M.zstd_compress(src, dst, level) return M.call({ "zstd-c", src, dst, level or 3 }) end
function M.lz4_block_decompress(src, dst, size) return M.call({ "lz4-block-d", src, dst, size }) end

-- In-memory variants via temp files.
local function via_files(sub, data, ...)
  local path = require "ok.path"
  local a, b = path.temp(".in"), path.temp(".out")
  path.write_bytes(a, data)
  local okv, err = pcall(M.call, { sub, a, b, ... })
  local out = okv and path.exists(b) and path.read_bytes(b) or nil
  path.remove(a, true); path.remove(b, true)
  if not okv then error(err, 2) end
  return out
end
function M.zstd_decompress_bytes(data) return via_files("zstd-d", data) end
function M.zstd_compress_bytes(data, level) return via_files("zstd-c", data, level or 3) end
function M.lz4_block_decompress_bytes(data, size) return via_files("lz4-block-d", data, size) end
function M.zlib_decompress_bytes(data) return via_files("zlib-d", data) end
function M.zlib_compress_bytes(data, level) return via_files("zlib-c", data, level or 6) end
function M.gzip_decompress_bytes(data) return via_files("gzip-d", data) end
function M.gzip_compress_bytes(data, level) return via_files("gzip-c", data, level or 9) end

-- msgpack file -> Lua value (binary values become {"$bytes": hex}).
function M.msgpack_load(path) return M.call({ "msgpack-json", path }) end
function M.msgpack_decode(data)
  local p = require "ok.path"
  local t = p.temp(".mp"); p.write_bytes(t, data)
  local okv, v = pcall(M.call, { "msgpack-json", t })
  p.remove(t, true)
  if not okv then error(v, 2) end
  return v
end
-- Lua value -> msgpack bytes written to path.
function M.msgpack_save(path, value)
  local json = require "ok.json"
  M.call({ "json-msgpack", "-", path }, { input = json.encode(value, { ensure_ascii = false }) })
end

-- Unity bundles / serialized files. See Tools/ok-luakit/README.md.
-- unity_objects(path, {type="Shader", name="...", bytes_dir=dir, tree=true}) -> array of
-- {file, path_id, class_id, type, name, container, tree}
-- UnityPy ClassIDType names the Rust class table lacks (it reports them as "Class_<id>").
local CLASS_NAMES = {
  [12] = "ParticleAnimator", [15] = "EllipsoidParticleEmitter", [17] = "Pipeline", [18] = "EditorExtension", [19] = "Physics2DSettings", [26] =
  "ParticleRenderer", [29] = "OcclusionCullingSettings", [30] = "GraphicsSettings", [41] = "OcclusionPortal", [45] = "Skybox", [47] = "QualitySettings",
  [51] = "Physics2DManager", [53] = "Collider2D", [55] = "PhysicsManager", [57] = "Joint", [58] = "CircleCollider2D", [59] = "HingeJoint", [60] =
  "PolygonCollider2D", [61] = "BoxCollider2D", [62] = "PhysicsMaterial2D", [64] = "MeshCollider", [65] = "BoxCollider", [66] = "CompositeCollider2D",
  [68] = "EdgeCollider2D", [70] = "CapsuleCollider2D", [72] = "ComputeShader", [75] = "ConstantForce", [76] = "WorldParticleCollider", [78] =
  "TagManager", [81] = "AudioListener", [82] = "AudioSource", [84] = "RenderTexture", [86] = "CustomRenderTexture", [87] = "MeshParticleEmitter", [88] =
  "ParticleEmitter", [92] = "GUILayer", [93] = "RuntimeAnimatorController", [94] = "ScriptMapper", [96] = "TrailRenderer", [98] = "DelayedCallManager",
  [102] = "TextMesh", [104] = "RenderSettings", [109] = "CGProgram", [110] = "BaseAnimationTrack", [111] = "Animation", [116] = "MonoManager", [117] =
  "Texture3D", [118] = "NewAnimationTrack", [119] = "Projector", [120] = "LineRenderer", [121] = "Flare", [122] = "Halo", [123] = "LensFlare", [124] =
  "FlareLayer", [125] = "HaloLayer", [126] = "NavMeshProjectSettings", [127] = "HaloManager", [129] = "PlayerSettings", [130] = "NamedObject", [131] =
  "GUITexture", [132] = "GUIText", [133] = "GUIElement", [134] = "PhysicMaterial", [135] = "SphereCollider", [136] = "CapsuleCollider", [137] =
  "SkinnedMeshRenderer", [138] = "FixedJoint", [140] = "RaycastCollider", [141] = "BuildSettings", [143] = "CharacterController", [144] =
  "CharacterJoint", [145] = "SpringJoint", [146] = "WheelCollider", [147] = "ResourceManager", [148] = "NetworkView", [149] = "NetworkManager", [150] =
  "PreloadData", [153] = "ConfigurableJoint", [154] = "TerrainCollider", [155] = "MasterServerInterface", [156] = "TerrainData", [157] =
  "LightmapSettings", [158] = "WebCamTexture", [159] = "EditorSettings", [160] = "InteractiveCloth", [161] = "ClothRenderer", [162] =
  "EditorUserSettings", [163] = "SkinnedCloth", [164] = "AudioReverbFilter", [165] = "AudioHighPassFilter", [166] = "AudioChorusFilter", [167] =
  "AudioReverbZone", [168] = "AudioEchoFilter", [169] = "AudioLowPassFilter", [170] = "AudioDistortionFilter", [171] = "SparseTexture", [180] =
  "AudioBehaviour", [181] = "AudioFilter", [182] = "WindZone", [183] = "Cloth", [185] = "ProceduralMaterial", [186] = "ProceduralTexture", [187] =
  "Texture2DArray", [188] = "CubemapArray", [191] = "OffMeshLink", [192] = "OcclusionArea", [193] = "Tree", [194] = "NavMeshObsolete", [195] =
  "NavMeshAgent", [196] = "NavMeshSettings", [197] = "LightProbesLegacy", [198] = "ParticleSystem", [199] = "ParticleSystemRenderer", [200] =
  "ShaderVariantCollection", [205] = "LODGroup", [206] = "BlendTree", [207] = "Motion", [208] = "NavMeshObstacle", [210] = "SortingGroup", [214] =
  "CachedSpriteAtlas", [215] = "ReflectionProbe", [216] = "ReflectionProbes", [218] = "Terrain", [220] = "LightProbeGroup", [221] =
  "AnimatorOverrideController", [222] = "CanvasRenderer", [223] = "Canvas", [224] = "RectTransform", [225] = "CanvasGroup", [226] = "BillboardAsset",
  [227] = "BillboardRenderer", [228] = "SpeedTreeWindAsset", [229] = "AnchoredJoint2D", [230] = "Joint2D", [231] = "SpringJoint2D", [232] =
  "DistanceJoint2D", [233] = "HingeJoint2D", [234] = "SliderJoint2D", [235] = "WheelJoint2D", [236] = "ClusterInputManager", [237] = "BaseVideoTexture",
  [238] = "NavMeshData", [240] = "AudioMixer", [241] = "AudioMixerController", [243] = "AudioMixerGroupController", [244] =
  "AudioMixerEffectController", [245] = "AudioMixerSnapshotController", [246] = "PhysicsUpdateBehaviour2D", [247] = "ConstantForce2D", [248] =
  "Effector2D", [249] = "AreaEffector2D", [250] = "PointEffector2D", [251] = "PlatformEffector2D", [252] = "SurfaceEffector2D", [253] =
  "BuoyancyEffector2D", [254] = "RelativeJoint2D", [255] = "FixedJoint2D", [256] = "FrictionJoint2D", [257] = "TargetJoint2D", [258] = "LightProbes",
  [259] = "LightProbeProxyVolume", [271] = "SampleClip", [272] = "AudioMixerSnapshot", [273] = "AudioMixerGroup", [280] = "NScreenBridge", [290] =
  "AssetBundleManifest", [292] = "UnityAdsManager", [300] = "RuntimeInitializeOnLoadManager", [301] = "CloudWebServicesManager", [303] =
  "UnityAnalyticsManager", [304] = "CrashReportManager", [305] = "PerformanceReportingManager", [310] = "UnityConnectSettings", [319] = "AvatarMask",
  [320] = "PlayableDirector", [328] = "VideoPlayer", [329] = "VideoClip", [330] = "ParticleSystemForceField", [331] = "SpriteMask", [362] =
  "WorldAnchor", [363] = "OcclusionCullingData"
}
local CLASS_IDS = {}
for id, name in pairs(CLASS_NAMES) do CLASS_IDS[name] = id end
M.CLASS_NAMES = CLASS_NAMES
function M.unity_objects(path, opts)
  opts = opts or {}
  local function dump(type_name)
    local args = { "unity-dump", path }
    if type_name then args[#args + 1] = "--type"; args[#args + 1] = type_name end
    if opts.name then args[#args + 1] = "--name"; args[#args + 1] = opts.name end
    if opts.path_id then args[#args + 1] = "--path-id"; args[#args + 1] = opts.path_id end
    if opts.bytes_dir then args[#args + 1] = "--bytes-dir"; args[#args + 1] = opts.bytes_dir end
    if opts.tree == false then args[#args + 1] = "--no-tree" end
    return M.call(args)
  end
  if opts.fields then
    -- Pruned trees (ok-luakit unity-fields): only the listed dotted paths, "a.b#len" = len(a.b).
    local t = opts.type and CLASS_IDS[opts.type] and ("Class_" .. CLASS_IDS[opts.type]) or opts.type
    local args = { "unity-fields", path, "--type", assert(t, "unity_objects: fields need a type"), "--fields", table.concat(opts.fields, ",") }
    if opts.name then args[#args + 1] = "--name"; args[#args + 1] = opts.name end
    local objs = M.call(args)
    for _, o in ipairs(objs) do
      local id = tonumber(tostring(o.type):match("^Class_(%d+)$"))
      if id and CLASS_NAMES[id] then o.type = CLASS_NAMES[id] end
    end
    return objs
  end
  local objs = dump(opts.type)
  if opts.type and #objs == 0 and CLASS_IDS[opts.type] then objs = dump("Class_" .. CLASS_IDS[opts.type]) end
  for _, o in ipairs(objs) do
    local id = type(o.type) == "string" and tonumber(o.type:match("^Class_(%d+)$"))
    if id and CLASS_NAMES[id] then o.type = CLASS_NAMES[id] end
  end
  return objs
end

-- zstd frame with the content size in its header: python-zstandard ZstdCompressor(level).compress(data).
function M.zstd_compress_sized_bytes(data, level) return via_files("zstd-c-sized", data, level or 3) end

-- SQLite (sqlite3 module): run ops on one connection, see `sqlite` in Tools/ok-luakit/README.md.
--   kit.sqlite(db, {{sql="SELECT ...", params={...}, query=true}, {sql="INSERT ...", many={{...}}}}, {readonly=true})
-- Returns one result per op ({rows=...} / {changes=n} / ...). Ops run in autocommit mode unless they
-- issue BEGIN/COMMIT themselves.
function M.sqlite(db, ops, opts)
  local json = require "ok.json"
  local spec = json.object({ { "readonly", opts and opts.readonly or false }, { "timeout", opts and opts.timeout or 5 }, { "ops", json.array(ops) } })
  return M.call({ "sqlite", db, "-" }, { input = json.encode(spec, { ensure_ascii = false }) })
end
-- Rows of one query.
function M.sql_rows(db, sql, params, opts)
  local op = { sql = sql, params = require("ok.json").array(params or {}), query = true }
  return M.sqlite(db, { op }, opts)[1].rows
end

-- OpenCC conversion (opencc-python-reimplemented): kit.zhconv("s2tw", "汉字") or a list of strings.
function M.zhconv(conversion, s)
  if type(s) == "string" then return M.call({ "zhconv", conversion, "-" }, { input = s }).text end
  for _, x in ipairs(s) do assert(not x:find("\n", 1, true), "zhconv: newline inside a list item") end
  if #s == 0 then return {} end
  return M.call({ "zhconv", conversion, "-", "--lines" }, { input = table.concat(s, "\n") }).lines
end

-- Text of the first `pages` pages of a PDF given as bytes (nil when it cannot be parsed).
function M.pdf_text_pages(data, pages)
  local p = require "ok.path"
  local t = p.temp(".pdf"); p.write_bytes(t, data)
  local args = { "pdf-text", t }
  if pages then args[#args + 1] = "--pages"; args[#args + 1] = pages end
  local okv, v = pcall(M.call, args)
  p.remove(t, true)
  return okv and v.pages or nil
end

-- Fetch a URL with curl.exe (Windows ships it). Returns body string; errors on HTTP failure.
function M.http_get(url, opts)
  opts = opts or {}
  local proc = require "ok.proc"
  local args = { "curl.exe", "-sS", "-L", "--fail", "--max-time", tostring(opts.timeout or 60) }
  for k, v in pairs(opts.headers or {}) do args[#args + 1] = "-H"; args[#args + 1] = k .. ": " .. v end
  if opts.data then args[#args + 1] = "--data-binary"; args[#args + 1] = "@-" end
  args[#args + 1] = url
  local res = proc.run(args, { capture = true, text = false, input = opts.data })
  if not res.ok then error("http_get " .. url .. ": " .. (res.stderr or ""), 2) end
  return res.stdout
end

-- requests-like call that never raises on HTTP errors: returns {status=int, body=string} or
-- {status=-1, error=msg} when no response arrived. opts: method, headers {k=v}, params (list of
-- {k, v} pairs or a table, appended as query string), data (string body; form = table -> urlencoded),
-- timeout (s), compressed (send Accept-Encoding and decode, like requests).
function M.urlencode(s)
  return (tostring(s):gsub("[^%w%-%._~]", function(c) return string.format("%%%02X", c:byte()) end))
end
function M.query_string(params)
  local parts = {}
  local list = params
  if params[1] == nil then
    list = {}
    for _, k in ipairs(require("ok.util").sorted_keys(params)) do list[#list + 1] = { k, params[k] } end
  end
  for _, kv in ipairs(list) do parts[#parts + 1] = M.urlencode(kv[1]) .. "=" .. M.urlencode(kv[2]) end
  return table.concat(parts, "&")
end
-- Many GETs in one curl.exe run (--parallel): reqs = {{url=..., out=file}, ...}; opts: parallel (max
-- connections, default 3), headers {k=v}, timeout. Returns {[out] = status} (-1: no response). A body is
-- left in `out` for every answered request.
function M.http_batch(reqs, opts)
  opts = opts or {}
  local proc, path = require "ok.proc", require "ok.path"
  if #reqs == 0 then return {} end
  local function q(s) return '"' .. tostring(s):gsub("\\", "\\\\"):gsub('"', '\\"') .. '"' end
  local cfg = {}
  for k, v in pairs(opts.headers or {}) do cfg[#cfg + 1] = "header = " .. q(k .. ": " .. v) end
  for _, r in ipairs(reqs) do
    cfg[#cfg + 1] = "url = " .. q(r.url)
    cfg[#cfg + 1] = "output = " .. q(path.norm(r.out))
  end
  local cf = path.temp(".curl")
  path.write_bytes(cf, table.concat(cfg, "\n") .. "\n")
  local args = { "curl.exe", "-sS", "-L", "--compressed", "--parallel", "--parallel-max", tostring(opts.parallel or 3),
    "--max-time", tostring(opts.timeout or 60), "-w", "%{http_code}\t%{filename_effective}\n", "-K", cf }
  local res = proc.run(args, { capture = true, text = true })
  path.remove(cf, true)
  local status = {}
  for _, r in ipairs(reqs) do status[path.norm(r.out)] = -1 end
  for line in (res.stdout or ""):gmatch("[^\n]+") do
    local code, file = line:match("^(%d+)\t(.*)$")
    if code and file then
      file = path.norm(file)
      local c = tonumber(code)
      status[file] = (c and c > 0) and c or -1
    end
  end
  local out = {}
  for _, r in ipairs(reqs) do out[r.out] = status[path.norm(r.out)] end
  return out
end

function M.http_request(url, opts)
  opts = opts or {}
  local proc, path = require "ok.proc", require "ok.path"
  if opts.params then url = url .. (url:find("?", 1, true) and "&" or "?") .. M.query_string(opts.params) end
  local out = path.temp(".body")
  local args = { "curl.exe", "-sS", "-o", out, "-w", "%{http_code}", "--max-time", tostring(opts.timeout or 60) }
  if opts.follow ~= false then args[#args + 1] = "-L" end
  if opts.compressed ~= false then args[#args + 1] = "--compressed" end
  if opts.method then args[#args + 1] = "-X"; args[#args + 1] = opts.method end
  for k, v in pairs(opts.headers or {}) do args[#args + 1] = "-H"; args[#args + 1] = k .. ": " .. v end
  local data = opts.data
  if opts.form then data = M.query_string(opts.form); args[#args + 1] = "-H"; args[#args + 1] = "Content-Type: application/x-www-form-urlencoded" end
  if data then args[#args + 1] = "--data-binary"; args[#args + 1] = "@-" end
  args[#args + 1] = url
  local res = proc.run(args, { capture = true, text = false, input = data })
  local body = path.exists(out) and path.read_bytes(out) or ""
  path.remove(out, true)
  local status = tonumber((res.stdout or ""):match("(%d+)%s*$") or "")
  if not res.ok or not status or status == 0 then
    return { status = -1, error = ((res.stderr or ""):gsub("%s+$", "")), body = body }
  end
  return { status = status, body = body }
end

-- Zip members (Python zipfile names): zip_read(path, suffix) -> {names={...}, texts={{name, text}}}
-- where members whose lower-cased name ends with suffix are inflated and decoded as UTF-8 (replace).
function M.zip_read(path, suffix)
  local args = { "zip-read", path }
  if suffix then args[#args + 1] = "--suffix"; args[#args + 1] = suffix end
  return M.call(args)
end

return M
