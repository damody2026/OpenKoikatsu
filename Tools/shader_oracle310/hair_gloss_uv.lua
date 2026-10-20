-- Compare production Koikano gloss coordinates with exact native VS/PS instructions.
--
-- This checks the coordinate stage only, not the remaining hair lighting formula.
local ok = require "ok"
package.path = ok.root .. "/Tools/?.lua;" .. package.path
local cg = require "shader_program_codegen"
local json, path, util = ok.json, ok.path, ok.util

local ROOT = ok.root
local SRC = ROOT .. "/Saved/Tests/Hair322Shader"
local OUT = ROOT .. "/Saved/Tests/Hair323GlossUv"
local PRODUCTION = ROOT .. "/Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders/KoikanoHairGlossUv.inl"
local DESCRIPTION = "Compare production Koikano gloss coordinates with exact native VS/PS instructions.\n\nThis checks the coordinate stage only, not the remaining hair lighting formula."

local function winpath(p) return (path.abs(p):gsub("/", "\\")) end

local function extract(bundle)
  local version = ok.kit.call({ "unity-version", bundle }).unity_version
  local major, minor = version:match("^(%d+)%.(%d+)")
  major, minor = tonumber(major), tonumber(minor)
  local stride = (major > 2019 or (major == 2019 and minor >= 3)) and 12 or 8
  local rows = json.array({})
  for _, obj in ipairs(cg.shader_trees(bundle)) do
    local shader = obj.tree
    local name = shader.m_ParsedForm.m_Name
    if name == "Koikano/hair_main_sun" or name == "Koikano/hair_main_sun_front" then
      local function first(v) if type(v[1]) == "table" then return v[1][1] end return v[1] end
      local off, length = first(shader.offsets), first(shader.compressedLengths)
      local raw = ok.kit.lz4_block_decompress_bytes(shader.compressedBlob:sub(off + 1, off + length), first(shader.decompressedLengths))
      local front = util.endswith(name, "_front")
      for _, index in ipairs({ 96, 104, 112, 120, 32, 48 }) do
        local program = cg.sub_program_at(raw, index, stride)
        local data = program.m_ProgramCode
        local at = data:find("DXBC", 1, true)
        assert(at ~= nil)
        local label = (front and "front" or "") .. ((index == 32 or index == 48) and "vertex" or "") .. string.format("%04d", index)
        path.write_bytes(SRC .. "/" .. label .. ".dxbc", data:sub(at))
        rows[#rows + 1] = json.object({ { "shader", name }, { "label", label }, { "keywords", json.array(program.m_Keywords) },
          { "local_keywords", program.m_LocalKeywords and json.array(program.m_LocalKeywords) or json.null }, { "sha256", ok.hash.sha256(data:sub(at)) } })
      end
    end
  end
  assert(#rows == 12)
  path.write_text(OUT .. "/Extraction.json", json.encode(json.object({ { "bundle", winpath(bundle) },
    { "bundle_sha256", ok.hash.sha256_file(bundle) }, { "programs", rows } }), { indent = 2 }))
end

local function find_first(list, pred)
  for _, x in ipairs(list) do if pred(x) then return x end end
  return nil
end
local function next_or_fail(list, pred)
  local x = find_first(list, pred)
  if x == nil then error("StopIteration", 0) end
  return x
end
local function contains(list, x) for _, v in ipairs(list) do if v == x then return true end end return false end

local function main()
  local parser = ok.args.parser(DESCRIPTION)
  parser:opt("--extract-bundle", { help = "Extract exact installed shader bytecode; disassemble using FXC before preparing" })
  parser:flag("--baseline", { help = "Generate counterexample using the previous shared Forge UV expression" })
  parser:opt("--run", { help = "Run compiled compute_oracle and audit new plus expected-failing baseline cases" })
  local args = parser:parse(arg)
  path.mkdir(OUT)
  if args.run then
    local rows = json.array({})
    local exe = winpath(args.run)
    for _, source in ipairs(path.glob(OUT, "*.hlsl")) do
      local name = path.name(source)
      local baseline = util.startswith(name, "baseline-")
      local run = ok.proc.run({ exe, winpath(source) }, { capture = true })
      local output = util.strip(run.stdout .. run.stderr)
      assert(run.code == (baseline and 1 or 0), util.repr({ winpath(source), output }))
      assert(util.startswith(output, baseline and "FAIL UV mismatch max=" or "PASS cases=512 taps=2 UV max="), util.repr({ winpath(source), output }))
      rows[#rows + 1] = json.object({ { "name", name }, { "exit", run.code }, { "expected_counterexample", baseline },
        { "output", output }, { "hlsl_sha256", ok.hash.sha256_file(source) } })
      print(output)
    end
    local counter = 0
    for _, r in ipairs(rows) do if r.expected_counterexample then counter = counter + 1 end end
    assert(#rows == 8 and counter == 4)
    local report = json.object({ { "passed", true }, { "scope", "Extracted coordinate instructions only; synthetic WARP controls, not complete native lighting" },
      { "cases", 2048 }, { "counterexample_cases", 2048 }, { "rows", rows },
      { "production_sha256", ok.hash.sha256_file(PRODUCTION) },
      { "evidence_sha256", ok.hash.sha256_file(OUT .. "/Evidence.json") },
      { "baseline_evidence_sha256", ok.hash.sha256_file(OUT .. "/BaselineEvidence.json") },
      { "extraction_sha256", ok.hash.sha256_file(OUT .. "/Extraction.json") },
      { "oracle_sha256", ok.hash.sha256_file(args.run) },
      { "oracle_source_sha256", ok.hash.sha256_file(ROOT .. "/Tools/shader_oracle310/compute_oracle.cpp") } })
    path.write_text(ROOT .. "/Saved/Logs/Hair323GlossUvAudit.json", json.encode(report, { indent = 2 }))
    return
  end
  if args.extract_bundle then
    extract(args.extract_bundle)
    return
  end
  local body = path.read_text(PRODUCTION):match('R"OKHAIR%((.-)%)OKHAIR"')
  local evidence = json.array({})
  for _, front in ipairs({ false, true }) do
    for _, old in ipairs({ false, true }) do
      local blob = old and 104 or 96
      local label = (front and "front" or "") .. string.format("%04d", blob)
      local ps = cg.precise_lines(path.read_bytes(SRC .. "/" .. label .. ".dxbc"), path.read_text(SRC .. "/" .. label .. ".asm.txt"))
      local reverse_label = (front and "front" or "") .. string.format("%04d", blob + 16)
      local reverse = cg.precise_lines(path.read_bytes(SRC .. "/" .. reverse_label .. ".dxbc"), path.read_text(SRC .. "/" .. reverse_label .. ".asm.txt"))
      -- Read native instructions; reject changes in the dataflow rather than
      -- silently substituting a manually reconstructed coordinate formula.
      local st = find_first(ps, function(x) return x:find("^mad r8.zw, v1.zzzw, cb0%[15%].xxxy, cb0%[15%].zzzw$") ~= nil end)
      assert((st ~= nil) == old)
      local tap0 = next_or_fail(ps, function(x) return util.startswith(x, "sample r11.xyzw,") end)
      local ureg = old and "r8" or "r10"
      local u = next_or_fail(ps, function(x) return util.startswith(x, "mad " .. ureg .. ".x, r6.w,") end)
      local y = next_or_fail(ps, function(x) return util.startswith(x, "add " .. ureg .. ".y,") end)
      assert(tap0 == "sample r11.xyzw, " .. ureg .. ".xyxx, t4.xyzw, s5")
      local need = st and { st, u, y, tap0 } or { u, y, tap0 }
      for _, x in ipairs(need) do assert(contains(reverse, x), "Reverse-color keyword changed gloss coordinates") end
      local tap1
      if old then
        tap1 = "r8.xw"
        assert(contains(ps, "sample r9.xyzw, r8.xwxx, t4.xyzw, s5"))
      else
        tap1 = "r10.xz"
        assert(contains(ps, "mov r10.z, v1.w"))
        assert(contains(ps, "sample r10.xyzw, r10.xzxx, t4.xyzw, s5"))
      end
      local vslabel = (front and "front" or "") .. string.format("vertex%04d", old and 48 or 32)
      local vsasm = path.read_text(SRC .. "/" .. vslabel .. ".asm.txt")
      -- The shared pixel translator strips ps_* headers; remove the VS
      -- profile header here before checking instruction correspondence.
      local kept = {}
      for line in (vsasm .. "\n"):gmatch("(.-)\n") do kept[#kept + 1] = line:find("^vs_%S+%s*$") and "" or line end
      local vs = cg.precise_lines(path.read_bytes(SRC .. "/" .. vslabel .. ".dxbc"), table.concat(kept, "\n"))
      local vst = next_or_fail(vs, function(x) return util.startswith(x, "mad o1.zw,") end)
      assert(vst == "mad o1.zw, v4.xxxy, cb0[23].xxxy, cb0[23].zzzw")
      local native = cg.translate({ vst }) .. "\nv1.zw=o1.zw;\n"
      native = native .. cg.translate(st and { st, u, y } or { u, y })
      if not old then native = native .. "\n" .. cg.translate({ "mov r10.z, v1.w" }) end
      native = native .. "\nreturn Tap != 0.0 ? " .. ureg .. ".xy : " .. tap1 .. ";"
      -- Compare native coordinates after UE row conversion. 4 variants,
      -- 512 cases each, both taps, non-unit/negative ST, UV outside [0,1].
      local production = body
      if args.baseline then
        local cpp = path.read_text(path.parent(path.parent(PRODUCTION)) .. "/KKPreviewActor.cpp")
        local old_bodies = {}
        for b in cpp:gmatch('TEXT%("(float3 bitan = normalize%(Parameters.TangentToWorld%[1%][^\n]-)"%)') do old_bodies[#old_bodies + 1] = b end
        assert(#old_bodies == 2)
        production = "float Front=" .. (front and 1 or 0) .. ".0,SpecHeight=.95; float3 View=float3(.3,.6,.7);\n"
        production = production .. "if(Tap!=0.0){" .. util.replace(old_bodies[1], "Parameters.TangentToWorld[1]", "float3(0,1,0)") .. "}\n"
        production = production .. util.replace(old_bodies[2], "Parameters.TangentToWorld[1]", "float3(0,1,0)")
      end
      local hlsl = "float2 Production(float2 UV,float4 VertexST,float4 ST,float Lambert,float OldHair,float Tap){" .. production .. "}\n"
        .. "float2 Native(float2 UV,float4 VertexST,float4 ST,float Lambert,float Tap){\n"
        .. "precise float4 v4=float4(UV.x,1.0-UV.y,0,0),o1=0,v1=0,r6=0,r8=0,r10=0;\n"
        .. "float4 cb0[24]; cb0[23]=VertexST; cb0[15]=ST; r6.w=Lambert;\n"
        .. native .. "\n"
        .. "}\n"
        .. "RWStructuredBuffer<float4> Output:register(u0);\n"
        .. "[numthreads(64,1,1)]void main(uint3 id:SV_DispatchThreadID){\n"
        .. "uint i=id.x;if(i>=512)return;\n"
        .. "float2 UV=float2((i%17)*.137-.4,(i%23)*.081-.3);\n"
        .. "float4 VertexST=float4(.35+(i%7)*.27,-1.3+(i%11)*.31,-.21,.16);\n"
        .. "float4 ST=float4(-.7+(i%9)*.25,.2+(i%13)*.12,.33,-.19);\n"
        .. "float Lambert=(i%29)/14.0-1.0,Tap=i%2;\n"
        .. "float2 a=Native(UV,VertexST,ST,Lambert,Tap); a.y=1.0-a.y;\n"
        .. "float2 b=Production(UV,VertexST,ST,Lambert," .. (old and 1 or 0) .. ".0,Tap);\n"
        .. "Output[i]=float4(a,b);\n"
        .. "}"
      path.write_text(OUT .. "/" .. (args.baseline and "baseline-" or "") .. label .. ".hlsl", hlsl)
      local instructions = json.array({ vst })
      if st then instructions[#instructions + 1] = st end
      instructions[#instructions + 1] = u
      instructions[#instructions + 1] = y
      evidence[#evidence + 1] = json.object({ { "label", label }, { "old", old }, { "front", front }, { "cases", 512 },
        { "shader_sha256", ok.hash.sha256(path.read_bytes(SRC .. "/" .. label .. ".dxbc")) },
        { "vertex_shader_sha256", ok.hash.sha256(path.read_bytes(SRC .. "/" .. vslabel .. ".dxbc")) },
        { "reverse_shader_sha256", ok.hash.sha256(path.read_bytes(SRC .. "/" .. reverse_label .. ".dxbc")) },
        { "native_instructions", instructions },
        { "production_sha256", ok.hash.sha256_file(PRODUCTION) } })
    end
  end
  path.write_text(OUT .. "/" .. (args.baseline and "BaselineEvidence.json" or "Evidence.json"), json.encode(evidence, { indent = 2 }))
  print("Prepared 2048 native VS/PS coordinate controls; full lighting remains outside this audit.")
end

ok.main(main)
