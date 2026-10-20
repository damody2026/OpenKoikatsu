-- Extract shared outline pixel programs from installed shaders; no card inputs.
local ok = require "ok"
local cg = require "shader_program_codegen"
local json, path, util = ok.json, ok.path, ok.util

local ROOT = ok.root
local BASE = ok.game_root(true) .. "/kks_decomp/plugins/KKS_Plugins/KKS_MaterialEditor"
local OUT = ROOT .. "/Saved/Tests/RuntimeOutlines01/Source"
local DEST = ROOT .. "/Plugins/KKRuntimePose/Source/KKRuntimePose/Private/SharedShaders/OutlinePrograms.inl"
local hair_sun = {}
for i = 0, 7 do hair_sun[#hair_sun + 1] = "KoikanoHairSun" .. i end
local families = {
  ["Shader Forge/main_opaque"] = { "ForgeOpaque" },
  ["Shader Forge/main_alpha"] = { "ForgeAlphaMasked", "ForgeAlphaTranslucent" },
  ["Shader Forge/main_item"] = { "ForgeItem" },
  ["Shader Forge/main_hair"] = { "ForgeHair" },
  ["Shader Forge/main_hair_front"] = { "ForgeHairFront" },
  ["Shader Forge/main_skin"] = { "ForgeSkin" },
  ["Koikano/main_skin"] = { "KoikanoSkin" },
  ["Koikano/main_clothes_opaque"] = { "KoikanoClothesOpaque", "KoikanoClothesColor" },
  ["Koikano/main_clothes_item"] = { "KoikanoClothesItem", "KoikanoClothesItemAlpha", "KoikanoClothesItemTranslucent", "KoikanoClothesItemAlphaTranslucent" },
  ["Koikano/hair_main_sun"] = hair_sun,
}
-- Engine uniforms an outline vertex program may read; the shared outline parent provides them (camera position,
-- identity object/view-projection matrices).
local VERTEX_BUILTINS = { _WorldSpaceCameraPos = true, unity_ObjectToWorld = true, unity_WorldToObject = true, unity_MatrixVP = true }
-- The vertex program's inputs by semantic (its DXBC input signature) as the values the outline parent provides in
-- Unity space: OKPosition (world, w=1), OKNormal, OKTangent, OKUV0..3 (Unity UV rows), OKColor.
local SEMANTICS = { POSITION0 = "OKPosition", NORMAL0 = "OKNormal", TANGENT0 = "OKTangent", TEXCOORD0 = "OKUV0",
  TEXCOORD1 = "OKUV1", TEXCOORD2 = "OKUV2", TEXCOORD3 = "OKUV3", COLOR0 = "OKColor" }
local function vertex_inputs(shader, asm)
  local out, inside = {}, false
  for line in asm:gmatch("[^\r\n]+") do
    if line:find("^// Input signature:") then inside = true
    elseif line:find("^// Output signature:") then break
    elseif inside then
      local name, index, register = line:match("^// (%u+)%s+(%d+)%s+%a+%s+(%d+)%s")
      if name then
        local value = SEMANTICS[name .. index]
        if not value then error(shader .. ": outline vertex input " .. name .. index .. " unsupported", 0) end
        out[#out + 1] = "float4 v" .. register .. "=" .. value .. ";"
      end
    end
  end
  if #out == 0 then error(shader .. ": outline vertex program has no input signature", 0) end
  return table.concat(out, "\n") .. "\n"
end
local function vertex_outputs(asm)
  -- One register can be declared in parts (o1.xy, o1.zw).
  local out, seen = {}, {}
  for register in asm:gmatch("dcl_output[%w_]* o(%d+)") do
    local n = tonumber(register)
    if not seen[n] then seen[n] = true; out[#out + 1] = n end
  end
  return out
end
local family_count = 0
for _ in pairs(families) do family_count = family_count + 1 end

-- Code of sub-program `index` and its first DXBC container.
local function native_blob(raw, index, stride)
  local sp = cg.sub_program_at(raw, index, stride)
  return cg.dxbc_in(sp.m_ProgramCode, "index")
end

local function dump(file, blob)
  path.write_bytes(file, blob)
  local asm = cg.dumpbin(file)
  path.write_text(path.with_suffix(file, ".asm.txt"), asm)
  return asm
end

local function main()
  path.mkdir(OUT)
  local lines = { "// Installed outline programs. Regenerate with Tools/generate_runtime_outlines.lua." }
  local records, generated = json.array({}), {}
  for _, spec in ipairs({ { "KK_Plugins.Resources.EC_Shaders.unity3d", 8 }, { "KK_Plugins.Resources.KKS_Shaders.unity3d", 12 } }) do
    local file, stride = spec[1], spec[2]
    for _, obj in ipairs(cg.shader_trees(BASE .. "/" .. file)) do
      local tree = obj.tree
      local parsed = tree.m_ParsedForm
      local shader = parsed.m_Name
      if families[shader] then
        local raw = cg.platform_raw(tree, 4)
        local p = parsed.m_SubShaders[1].m_Passes[1]
        local names = cg.names(p.m_NameIndices)
        -- These installed shaders put their authored front-cull outline before ForwardBase.
        assert(p.m_State.culling.val == 1, util.repr({ shader, p.m_State.culling }))
        local program
        for _, x in ipairs(p.progFragment.m_SubPrograms) do if x.m_GpuProgramType == 17 then program = x break end end
        assert(program, "StopIteration")
        local label = families[shader][1] .. "Outline"
        local index = program.m_BlobIndex
        local blob = native_blob(raw, index, stride)
        local vertex
        for _, x in ipairs(p.progVertex.m_SubPrograms) do if x.m_GpuProgramType == 15 then vertex = x break end end
        assert(vertex, "StopIteration")
        local vertex_blob = native_blob(raw, vertex.m_BlobIndex, stride)
        local vertex_asm = dump(OUT .. "/" .. label .. ".vs.dxbc", vertex_blob)
        local forward = parsed.m_SubShaders[1].m_Passes[2]
        local fv
        for _, x in ipairs(forward.progVertex.m_SubPrograms) do if x.m_GpuProgramType == 15 then fv = x break end end
        if fv then dump(OUT .. "/" .. label .. ".forward.vs.dxbc", native_blob(raw, fv.m_BlobIndex, stride)) end
        local asm = dump(OUT .. "/" .. label .. ".dxbc", blob)
        local sizes = cg.cb_sizes(asm)
        assert(#sizes == #program.m_ConstantBuffers)
        lines[#lines + 1] = "// " .. shader .. "; original DXBC " .. cg.sha256(blob)
        lines[#lines + 1] = "static const int32 G" .. label .. "CBSize[]={" .. table.concat(sizes, ",") .. "};"
        lines[#lines + 1] = "static const FOKAdditionalUniform G" .. label .. "Uniforms[]={"
        for cb, c in ipairs(program.m_ConstantBuffers) do
          for _, u in ipairs(cg.concat(c.m_VectorParams, c.m_MatrixParams)) do lines[#lines + 1] = cg.uniform_line(names, cb - 1, u) end
        end
        lines[#lines + 1] = "};"
        lines[#lines + 1] = "static const TArray<FOKAdditionalTexture> G" .. label .. "Textures={"
        local texture_params = cg.concat(program.m_TextureParams)
        for _, vt in ipairs(vertex.m_TextureParams) do
          local dup = false
          for _, t in ipairs(texture_params) do if names[t.m_NameIndex] == names[vt.m_NameIndex] then dup = true break end end
          if not dup then
            local new = util.deepcopy(vt)
            local top = -1
            for _, t in ipairs(texture_params) do if t.m_Index > top then top = t.m_Index end end
            new.m_Index = top + 1
            new.m_SamplerIndex = new.m_Index
            texture_params[#texture_params + 1] = new
          end
        end
        for _, t in ipairs(texture_params) do lines[#lines + 1] = cg.texture_line(names[t.m_NameIndex], t.m_Index, t.m_SamplerIndex, t.m_Dim) end
        lines[#lines + 1] = "};"
        lines[#lines + 1] = "static const FOKAdditionalProperty G" .. label .. "Properties[]={"
        for _, prop in ipairs(parsed.m_PropInfo.m_Props) do lines[#lines + 1] = cg.property_line(prop) end
        lines[#lines + 1] = "};"
        lines[#lines + 1] = "static const TCHAR* G" .. label .. "Program="
        cg.text_chunks("\n" .. cg.translate(cg.precise_lines(blob, asm)) .. "\n", lines)
        lines[#lines + 1] = ";"
        -- The outline pass's vertex program: the hull extrusion exactly as the installed shader computes it. The
        -- shared outline parent runs it in Unity world space (object and view-projection matrices identity).
        local vertex_sizes = cg.cb_sizes(vertex_asm)
        assert(#vertex_sizes == #vertex.m_ConstantBuffers, shader .. ": vertex constant buffers")
        lines[#lines + 1] = "static const int32 G" .. label .. "VertexCBSize[]={" .. table.concat(vertex_sizes, ",") .. "};"
        lines[#lines + 1] = "static const FOKAdditionalUniform G" .. label .. "VertexUniforms[]={"
        for cb, c in ipairs(vertex.m_ConstantBuffers) do
          for _, u in ipairs(cg.concat(c.m_VectorParams, c.m_MatrixParams)) do
            local name = names[u.m_NameIndex]
            -- Engine values the outline parent supplies itself; anything else engine-side would be unsupported.
            if (name:find("^unity_") or name:find("^_WorldSpace") or name:find("^_Projection") or name:find("^_Screen") or name:find("^_Time"))
              and not VERTEX_BUILTINS[name] then
              error(shader .. ": outline vertex program uses unsupported engine value " .. name, 0)
            end
            lines[#lines + 1] = cg.uniform_line(names, cb - 1, u)
          end
        end
        lines[#lines + 1] = "};"
        lines[#lines + 1] = "static const TArray<FOKAdditionalTexture> G" .. label .. "VertexTextures={"
        for _, t in ipairs(vertex.m_TextureParams) do lines[#lines + 1] = cg.texture_line(names[t.m_NameIndex], t.m_Index, t.m_SamplerIndex, t.m_Dim) end
        lines[#lines + 1] = "};"
        lines[#lines + 1] = "static const TCHAR* G" .. label .. "VertexProgram="
        cg.text_chunks("\n" .. vertex_inputs(shader, vertex_asm) .. cg.translate(cg.precise_lines(vertex_blob, vertex_asm), vertex_outputs(vertex_asm), "") .. "\n", lines)
        lines[#lines + 1] = ";"
        generated[#generated + 1] = { label, families[shader] }
        local vertex_uniforms, vertex_textures = json.array({}), json.array({})
        for _, c in ipairs(vertex.m_ConstantBuffers) do
          for _, u in ipairs(c.m_VectorParams) do vertex_uniforms[#vertex_uniforms + 1] = json.object({ { "name", names[u.m_NameIndex] }, { "offset", u.m_Index } }) end
        end
        for _, t in ipairs(vertex.m_TextureParams) do vertex_textures[#vertex_textures + 1] = names[t.m_NameIndex] end
        records[#records + 1] = json.object({ { "shader", shader }, { "recipes", families[shader] }, { "blob", index }, { "sha256", cg.sha256(blob) },
          { "state", p.m_State }, { "uniforms", program.m_ConstantBuffers }, { "vertex_uniforms", vertex_uniforms }, { "vertex_textures", vertex_textures } })
      end
    end
  end
  assert(#generated == family_count)
  local function cases(recipes)
    local s = {}
    for _, r in ipairs(recipes) do s[#s + 1] = "case EKKRuntimeMaterialRecipe::" .. r .. ":" end
    return table.concat(s)
  end
  lines[#lines + 1] = "static const FKKRuntimeShaderLayout* OutlineLayout(EKKRuntimeMaterialRecipe Recipe){switch(Recipe){"
  for _, g in ipairs(generated) do
    local label = g[1]
    lines[#lines + 1] = cases(g[2]) .. "{static const auto L=BuildAdditionalLayout(MakeArrayView(G" .. label .. "CBSize),MakeArrayView(G" .. label .. "Uniforms),MakeArrayView(G" .. label
      .. "Textures),MakeArrayView(G" .. label .. "Properties),EKKRuntimeMaterialRecipe::None);return &L;}"
  end
  lines[#lines + 1] = "default:return nullptr;}}"
  lines[#lines + 1] = "static FString OutlineProgram(EKKRuntimeMaterialRecipe Recipe){switch(Recipe){"
  for _, g in ipairs(generated) do lines[#lines + 1] = cases(g[2]) .. "return G" .. g[1] .. "Program;" end
  lines[#lines + 1] = "default:return {};}}"
  lines[#lines + 1] = "static const FKKRuntimeShaderLayout* OutlineVertexLayout(EKKRuntimeMaterialRecipe Recipe){switch(Recipe){"
  for _, g in ipairs(generated) do
    local label = g[1]
    lines[#lines + 1] = cases(g[2]) .. "{static const auto L=BuildAdditionalLayout(MakeArrayView(G" .. label .. "VertexCBSize),MakeArrayView(G" .. label
      .. "VertexUniforms),MakeArrayView(G" .. label .. "VertexTextures),MakeArrayView(G" .. label .. "Properties),EKKRuntimeMaterialRecipe::None);return &L;}"
  end
  lines[#lines + 1] = "default:return nullptr;}}"
  lines[#lines + 1] = "static FString OutlineVertexProgram(EKKRuntimeMaterialRecipe Recipe){switch(Recipe){"
  for _, g in ipairs(generated) do lines[#lines + 1] = cases(g[2]) .. "return G" .. g[1] .. "VertexProgram;" end
  lines[#lines + 1] = "default:return {};}}"
  path.write_text(DEST, table.concat(lines, "\n") .. "\n")
  path.write_text(path.parent(OUT) .. "/sources.json", json.encode(records, { indent = 2 }))
  local recipes = 0
  for _, g in ipairs(generated) do recipes = recipes + #g[2] end
  print(json.encode(json.object({ { "families", #generated }, { "recipes", recipes }, { "output", (DEST:gsub("/", "\\")) } })))
end

ok.main(main)
