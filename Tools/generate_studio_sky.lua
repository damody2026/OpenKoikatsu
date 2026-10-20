-- Translates Studio's sky shader (Funly/Sky Studio/Skybox/3D Standard, the one the StudioSky profiles' skybox
-- materials use, in abdata/studio/map/materials/00.unity3d) into HLSL for OK Studio's sky dome:
-- Plugins/OKStudio/Source/OKStudio/Private/StudioSkyPrograms.inl. Every shipped variant (keyword set) gets its vertex
-- and pixel program, the constant buffers they read (uniform name, buffer slot, byte offset, components, property
-- type and gamma flag) and the textures (name, t slot, sampler slot). The values come from the OK base library
-- (studio/sky.json, `ok-extract <game> --oklib studio-sky <lib>`); the programs are generated offline and checked in.
--
--   Tools\lua\lua.exe Tools\generate_studio_sky.lua [game root]
local ok = require "ok"
local cg = require "shader_program_codegen"
local json, path, util = ok.json, ok.path, ok.util

local ROOT = ok.root
local GAME = arg[1] or ok.game_root(true)
local BUNDLE = GAME .. "/abdata/studio/map/materials/00.unity3d"
local SHADER = "Funly/Sky Studio/Skybox/3D Standard"
local DEST = ROOT .. "/Plugins/OKStudio/Source/OKStudio/Private/StudioSkyPrograms.inl"
local WORK = ROOT .. "/Saved/Tests/StudioSky/Source"
-- Unity 2019.4: 12-byte ShaderProgram entries.
local STRIDE = 12

local function dump(file, blob)
  path.write_bytes(file, blob)
  local asm = cg.dumpbin(file)
  path.write_text(path.with_suffix(file, ".asm.txt"), asm)
  return asm
end

-- Signature rows of a DXBC disassembly section ("Input signature" / "Output signature"): register -> "NAME index".
local function signature(asm, title)
  local out, inside = {}, false
  for _, line in ipairs(cg.splitlines(asm)) do
    if line:find("^// " .. title) then inside = true
    elseif inside and line:find("^// %-%-%-") then -- column rule
    elseif inside and line:match("^// (%u[%w_]*)%s+(%d+)%s+(%w+)%s+(%d+)") then
      local name, index, _, reg = line:match("^// (%u[%w_]*)%s+(%d+)%s+(%w+)%s+(%d+)")
      out[tonumber(reg)] = name .. " " .. index
    elseif inside and line == "//" and next(out) then break end
  end
  return out
end

-- dcl_output registers of a vertex program.
local function outputs(asm)
  local regs, seen = {}, {}
  for r in asm:gmatch("dcl_output[_%w]* o(%d+)") do
    local n = tonumber(r)
    if not seen[n] then seen[n] = true; regs[#regs + 1] = n end
  end
  table.sort(regs)
  return regs
end

-- One HLSL statement per line, each its own TEXT() literal (MSVC string literal limits).
local function text_lines(code, indent)
  local out = {}
  for _, line in ipairs(cg.splitlines(code)) do
    if line ~= "" then out[#out + 1] = indent .. 'TEXT("' .. line:gsub("\\", "\\\\"):gsub('"', '\\"') .. '\\n")' end
  end
  return table.concat(out, "\n")
end

local function main()
  path.mkdir(WORK)
  local dir = path.temp("_sky_shader")
  local objs = ok.kit.unity_objects(BUNDLE, { type = "Shader", name = SHADER, bytes_dir = dir })
  local tree = assert(objs[1] and objs[1].tree, "no " .. SHADER .. " in " .. BUNDLE)
  tree.compressedBlob = ok.kit.bytes(tree.compressedBlob)
  path.rmtree(dir)
  local raw = cg.platform_raw(tree, 4)
  local parsed = tree.m_ParsedForm
  assert(#parsed.m_SubShaders == 1 and #parsed.m_SubShaders[1].m_Passes == 1, "one sub-shader, one pass")
  local pass = parsed.m_SubShaders[1].m_Passes[1]
  local names = cg.names(pass.m_NameIndices)
  local props = {}
  for _, p in ipairs(parsed.m_PropInfo.m_Props) do props[p.m_Name] = p end

  -- Vertex sub-programs by keyword set.
  local function keywords(x)
    local sp = cg.sub_program_at(raw, x.m_BlobIndex, STRIDE)
    local k = {}
    for _, w in ipairs(sp.m_Keywords) do k[#k + 1] = w end
    for _, w in ipairs(sp.m_LocalKeywords or {}) do k[#k + 1] = w end
    table.sort(k)
    return table.concat(k, " "), sp
  end
  local vertices = {}
  for _, x in ipairs(pass.progVertex.m_SubPrograms) do
    if x.m_GpuProgramType == 15 then local k = keywords(x); vertices[k] = x end
  end

  -- Uniforms of a sub-program: { name, buffer slot, byte offset, components (16: matrix), property type, gamma }.
  local function uniforms(x)
    local slots = {}
    for _, b in ipairs(x.m_ConstantBufferBindings) do slots[names[b.m_NameIndex]] = b.m_Index end
    local list = {}
    for _, cb in ipairs(x.m_ConstantBuffers) do
      local slot = assert(slots[names[cb.m_NameIndex]], "unbound constant buffer " .. tostring(names[cb.m_NameIndex]))
      for _, v in ipairs(cb.m_VectorParams) do
        local n = names[v.m_NameIndex]
        local p = props[n]
        list[#list + 1] = { n, slot, v.m_Index, v.m_Dim, p and p.m_Type or -1, p and (p.m_Flags & 32 ~= 0) or false }
      end
      for _, m in ipairs(cb.m_MatrixParams) do list[#list + 1] = { names[m.m_NameIndex], slot, m.m_Index, 16, -1, false } end
    end
    table.sort(list, function(a, b) if a[2] ~= b[2] then return a[2] < b[2] end return a[3] < b[3] end)
    return list
  end
  local function uniform_lines(list)
    local out = {}
    for _, u in ipairs(list) do
      out[#out + 1] = ('\t{TEXT("%s"),%d,%d,%d,%d,%s},'):format(u[1], u[2], u[3], u[4], u[5], u[6] and "true" or "false")
    end
    return table.concat(out, "\n")
  end

  local lines = {
    "// Studio's sky programs: " .. SHADER .. " (" .. path.name(BUNDLE) .. " of studio/map/materials), every shipped variant.",
    "// Generated by Tools/generate_studio_sky.lua; do not edit. Vertex programs write vo<n> (their o<n>) and use vr<n>",
    "// temporaries and vcb<n> buffers; pixel programs read v<n> and cb<n>, sample Texture<t> with Sampler<s> and return o0.",
  }
  local programs = {}
  local index = 0
  for _, x in ipairs(pass.progFragment.m_SubPrograms) do
    if x.m_GpuProgramType == 17 then
      local key = keywords(x)
      local vx = assert(vertices[key], "no vertex program for keywords '" .. key .. "'")
      local tag = "Sky" .. index
      local function program(sub, file)
        local sp = cg.sub_program_at(raw, sub.m_BlobIndex, STRIDE)
        local blob = cg.dxbc_in(sp.m_ProgramCode, "index")
        local asm = dump(WORK .. "/" .. file, blob)
        return blob, asm
      end
      local vblob, vasm = program(vx, tag .. ".vs.dxbc")
      local pblob, pasm = program(x, tag .. ".ps.dxbc")
      local vout = outputs(vasm)
      local vcode = cg.translate(cg.precise_lines(vblob, vasm), vout, "")
      -- Vertex registers renamed so the pixel program's own names stay free.
      vcode = vcode:gsub("%f[%w_]o(%d+)", "vo%1"):gsub("%f[%w_]r(%d+)", "vr%1"):gsub("%f[%w_]cb(%d+)", "vcb%1")
      local pcode = cg.translate(cg.precise_lines(pblob, pasm))
      -- Pixel inputs from the vertex outputs with the same semantic.
      local vsig, psig = signature(vasm, "Output signature"), signature(pasm, "Input signature")
      local glue = {}
      for reg, sem in pairs(psig) do
        if sem:find("^SV_") == nil then
          local from
          for r, s in pairs(vsig) do if s == sem then from = r end end
          assert(from, tag .. ": pixel input " .. sem .. " has no vertex output")
          glue[#glue + 1] = { reg, ("float4 v%d=vo%d;"):format(reg, from) }
        end
      end
      table.sort(glue, function(a, b) return a[1] < b[1] end)
      local glue_text = {}
      for _, g in ipairs(glue) do glue_text[#glue_text + 1] = g[2] end
      local vin = signature(vasm, "Input signature")
      assert(vin[0] == "POSITION 0" and not vcode:find("%f[%w_]v[1-9]"), tag .. ": the vertex program reads only POSITION")
      local textures = {}
      for _, t in ipairs(x.m_TextureParams) do
        textures[#textures + 1] = ('\t{TEXT("%s"),%d,%d},'):format(names[t.m_NameIndex], t.m_Index, t.m_SamplerIndex)
      end
      local function sizes(asm) local s = cg.cb_size_map(asm); local out = {}; for b, n in pairs(s) do out[#out + 1] = { b, n } end
        table.sort(out, function(a, b) return a[1] < b[1] end)
        local t = {}; for _, v in ipairs(out) do t[#t + 1] = ("{%d,%d}"):format(v[1], v[2]) end
        -- MSVC has no zero-length arrays: an unused placeholder, the count stays 0.
        return #t > 0 and table.concat(t, ",") or "{-1,0}", #t end
      lines[#lines + 1] = ""
      lines[#lines + 1] = ("// %s: %s; vertex DXBC %s, pixel DXBC %s"):format(tag, key == "" and "(no keywords)" or key, cg.sha256(vblob), cg.sha256(pblob))
      local vbuf, vbufs = sizes(vasm)
      local pbuf, pbufs = sizes(pasm)
      lines[#lines + 1] = ("static const FOKSkyBuffer G%sVertexBuffers[]={%s};"):format(tag, vbuf)
      lines[#lines + 1] = ("static const FOKSkyBuffer G%sPixelBuffers[]={%s};"):format(tag, pbuf)
      lines[#lines + 1] = ("static const FOKSkyUniform G%sVertexUniforms[]={\n%s\n};"):format(tag, uniform_lines(uniforms(vx)))
      local pu = uniforms(x)
      lines[#lines + 1] = ("static const FOKSkyUniform G%sPixelUniforms[]={\n%s\n};"):format(tag, #pu > 0 and uniform_lines(pu) or '\t{TEXT(""),-1,0,0,-1,false},')
      lines[#lines + 1] = ("static const FOKSkyTexture G%sTextures[]={\n%s\n};"):format(tag, #textures > 0 and table.concat(textures, "\n") or '\t{TEXT(""),-1,-1},')
      lines[#lines + 1] = ("static const TCHAR* G%sVertex=\n%s;"):format(tag, text_lines(vcode, "\t"))
      lines[#lines + 1] = ("static const TCHAR* G%sGlue=TEXT(\"%s\");"):format(tag, table.concat(glue_text, ""))
      lines[#lines + 1] = ("static const TCHAR* G%sPixel=\n%s;"):format(tag, text_lines(pcode, "\t"))
      programs[#programs + 1] = ("\t{TEXT(\"%s\"),G%sVertexBuffers,%d,G%sPixelBuffers,%d,G%sVertexUniforms,UE_ARRAY_COUNT(G%sVertexUniforms),G%sPixelUniforms,%d,G%sTextures,%d,G%sVertex,G%sGlue,G%sPixel},")
        :format(key, tag, vbufs, tag, pbufs, tag, tag, tag, #pu, tag, #textures, tag, tag, tag)
      index = index + 1
    end
  end
  lines[#lines + 1] = ""
  lines[#lines + 1] = "static const FOKSkyProgram GOKSkyPrograms[]={"
  for _, p in ipairs(programs) do lines[#lines + 1] = p end
  lines[#lines + 1] = "};"
  path.write_text(DEST, table.concat(lines, "\n") .. "\n")
  io.write(("%d sky programs -> %s\n"):format(index, DEST))
end

main()
