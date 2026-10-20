-- Shared body of forge_alpha_oracle333 / forge_item_oracle332 / forge_opaque_oracle340:
-- independent original-DXBC versus translated-HLSL controls (prepare, --verify).
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local cg = require "shader_program_codegen"
local M = {}

-- variants: array of {{name, value}, ...} (value: float or array of floats)
function M.objects(list)
  local out = json.array({})
  for i, pairs_list in ipairs(list) do out[i] = json.object(pairs_list) end
  return out
end

local function fl(...) return json.array({ ... }) end
M.fl = fl

local DEFAULTS = {
  _LightColor0 = { 1.0, 1.0, 1.0, 1.0 }, _ambientshadowG = { 128 / 255, 128 / 255, 128 / 255, 0.26 },
  _LineColorG = { 0.5, 0.5, 0.5, 0.0 }, _WorldSpaceLightPos0 = { 0.0, 0.6427876, 0.7660446, 0.0 },
  _WorldSpaceCameraPos = { 0.0, 1.5, -3.0, 0.0 }, _Time = { 0.065, 1.3, 2.6, 3.9 },
}

-- cfg: name (MainAlpha...), gen, out, asm_dir, programs ({index, native}...), shadow_index,
--      sizes ({[0]=..,[1]=..,[2]=..}), variants
function M.prepare(cfg)
  local ROOT = ok.root
  local GEN, OUT = ROOT .. "/" .. cfg.gen, ROOT .. "/" .. cfg.out
  local data = ok.read_json(GEN .. "/Bindings.json")
  local props = {}
  for _, p in ipairs(data.properties) do props[p.m_Name] = p end
  path.mkdir(OUT)
  local variants = cfg.variants
  for _, spec in ipairs(cfg.programs) do
    local index, native = spec[1], spec[2]
    local p
    for _, q in ipairs(data.native_programs) do if q.native_blob == native then p = q break end end
    assert(p, "StopIteration")
    local label = "Forward" .. index
    local shadow = index == cfg.shadow_index
    local asm = path.read_text(string.format("%s/%s/%04d.asm.txt", ROOT, cfg.asm_dir, index))
    -- Metadata may reserve larger buffers; declare the exact DXBC ranges.
    local sizes = cg.cb_size_map(asm)
    local n_sizes = 0
    for b, n in pairs(sizes) do n_sizes = n_sizes + 1; assert(cfg.sizes[b] == n) end
    for b in pairs(cfg.sizes) do assert(sizes[b] ~= nil) end
    for _, t in ipairs(p.textures) do assert(cg.uses_sampler(asm, t.index, t.sampler), util.repr(t)) end
    local decl = {}
    for b, n in pairs(sizes) do decl[#decl + 1] = ("cbuffer Constants%d:register(b%d){float4 cb%d[%d];};"):format(b, b, b, n) end
    for _, t in ipairs(p.textures) do decl[#decl + 1] = ("Texture2D<float4> Texture%d:register(t%d);"):format(t.index, t.index) end
    for s = 0, #p.textures - 1 do decl[#decl + 1] = ("SamplerState Sampler%d:register(s%d);"):format(s, s) end
    local signature = "float4 pos:SV_POSITION,float2 uv:TEXCOORD0,float4 world:TEXCOORD1,float3 normal:TEXCOORD2,float3 tangent:TEXCOORD3,float3 bitangent:TEXCOORD4,"
    if shadow then signature = signature .. "float4 shadow:TEXCOORD5," end
    decl[#decl + 1] = "float4 main(" .. signature .. "bool front:SV_IsFrontFace):SV_Target{"
    decl[#decl + 1] = "float4 v1=float4(uv,0,0);float4 v2=world;float4 v3=float4(normal,0);float4 v4=float4(tangent,0);float4 v5=float4(bitangent,0);"
    if shadow then decl[#decl + 1] = "float4 v6=shadow;" end
    decl[#decl + 1] = "float4 v" .. (shadow and 7 or 6) .. "=float4(asfloat(front?0xffffffffu:0u),0,0,0);"
    decl[#decl + 1] = "const float UVFlip=0.0;"
    decl[#decl + 1] = path.read_text(GEN .. "/" .. label .. ".hlsl")
    decl[#decl + 1] = "}"
    path.write_text(OUT .. "/" .. label .. ".hlsl", table.concat(decl, "\n"))
    for case = 0, #variants * 2 - 1 do
      local buffers = json.object()
      for b, n in pairs(sizes) do
        local list = {}
        for i = 1, n * 4 do list[i] = 0.0 end
        buffers[b] = list
      end
      local overrides = variants[case // 2 + 1]
      for _, u in ipairs(p.uniforms) do
        local prop = props[u.name]
        local v
        if prop then v = {}; for i = 0, 3 do v[i + 1] = prop["m_DefValue[" .. i .. "]"] end else v = { 0.0, 0.0, 0.0, 0.0 } end
        if util.endswith(u.name, "_ST") then v = { 1.0, 1.0, 0.0, 0.0 } end
        v = DEFAULTS[u.name] or v
        local x = overrides[u.name]
        if x ~= nil then
          if type(x) == "table" then v = x else v = { x, 0.0, 0.0, 0.0 } end
        end
        if prop and (prop.m_Flags & 32) ~= 0 then
          local lin = {}
          for i = 1, 3 do
            local c = v[i]
            lin[i] = c <= 0.04045 and c / 12.92 or ((c + 0.055) / 1.055) ^ 2.4
          end
          for i = 4, #v do lin[i] = v[i] end
          v = lin
        end
        assert(not util.truthy(u.rows) and u.type == 0, util.repr(u))
        local at = u.offset // 4
        local buf = buffers[u.cb]
        assert(at + u.dim <= #buf, util.repr(u))
        for i = 1, u.dim do buf[at + i] = v[i] end
      end
      for b, v in pairs(buffers) do
        path.write_bytes(("%s/%s.%d.cb%d.bin"):format(OUT, label, case, b), string.pack("<" .. string.rep("f", #v), table.unpack(v)))
      end
    end
    path.write_text(OUT .. "/" .. label .. ".cases.json", json.encode(variants, { indent = 2 }))
  end
  print("Prepared 2 original " .. cfg.name .. " variants x 44 independent controls.")
end

-- cfg: labels, negative (string to replace), exe, cpp, log
function M.verify(cfg)
  local ROOT = ok.root
  local GEN, OUT = ROOT .. "/" .. cfg.gen, ROOT .. "/" .. cfg.out
  local executable = ROOT .. "/" .. cfg.exe
  local function run(label)
    local res = ok.proc.run({ (executable:gsub("/", "\\")), (ROOT:gsub("/", "\\")), label }, { capture = true })
    return res
  end
  local results = json.array({})
  for _, label in ipairs(cfg.labels) do
    local wrapper = OUT .. "/" .. label .. ".hlsl"
    local valid = path.read_text(wrapper)
    -- Wrong ShadowColor math must be detectable, including black input.
    local negative = util.replace(valid, cfg.negative, "float4(1.,1.,1.,1.)")
    assert(negative ~= valid)
    local okv, err = pcall(function()
      path.write_text(wrapper, negative)
      local result = run(label)
      path.write_text(OUT .. "/" .. label .. ".negative.log", result.stdout .. result.stderr)
      assert(result.code ~= 0 and result.stderr:find("pixel mismatch", 1, true), util.repr({ code = result.code, stdout = result.stdout, stderr = result.stderr }))
    end)
    path.write_text(wrapper, valid)
    if not okv then error(err, 0) end
    local result = run(label)
    local output = result.stdout .. result.stderr
    path.write_text(OUT .. "/" .. label .. ".log", output)
    print(util.strip(output))
    io.stdout:flush()
    assert(result.code == 0 and output:sub(1, 5) == "PASS ", util.repr({ code = result.code, stdout = result.stdout, stderr = result.stderr }))
    results[#results + 1] = json.object({ { "label", label }, { "result", util.strip(output) }, { "negative_wrong_shadow_color_rejected", true },
      { "wrapper_sha256", ok.hash.sha256_file(wrapper) },
      { "original_sha256", ok.hash.sha256_file(GEN .. "/" .. label .. ".dxbc") },
      { "generated_sha256", ok.hash.sha256_file(OUT .. "/" .. label .. ".generated.dxbc") } })
  end
  local report = json.object({ { "passed", true },
    { "scope", "fragment math for identical synthetic inputs; external UE shadow camera and final card render require separate validation" },
    { "variants", results }, { "cpp_sha256", ok.hash.sha256_file(ROOT .. "/" .. cfg.cpp) } })
  path.write_text(ROOT .. "/" .. cfg.log, json.encode(report, { indent = 2 }))
end

function M.main(cfg)
  ok.main(function()
    M.prepare(cfg)
    if ok.args.has(arg, "--verify") then M.verify(cfg) end
  end)
end

return M
