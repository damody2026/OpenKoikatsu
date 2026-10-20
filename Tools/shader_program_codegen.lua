-- Translate the supported straight-line D3D11 pixel program into HLSL.
--
-- Immediate constants come from DXBC words, never dumpbin's rounded decimals.
-- Unsupported instructions or metadata fail closed. Generation is offline;
-- production uses the checked-in program, not a card/image or Saved directory.
--
-- Module use (generators):  local codegen = require "shader_program_codegen"
--   codegen.precise_lines(blob, asm), codegen.translate(lines[, output_registers[, return_statement]])
-- plus shared helpers for the installed-shader generators (bundle loading,
-- platform blob decompression, DXBC scans, UnityPy ShaderSubProgram parsing).
-- CLI: lua.exe Tools/shader_program_codegen.lua <source> <output>
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local M = {}

local function fail(msg) error(msg, 0) end

-- Python str.splitlines() for ASCII text.
local function splitlines(s)
  local out, start, i, n = {}, 1, 1, #s
  while i <= n do
    local c = s:byte(i)
    if c == 13 or c == 10 or c == 11 or c == 12 or c == 28 or c == 29 or c == 30 then
      out[#out + 1] = s:sub(start, i - 1)
      if c == 13 and s:byte(i + 1) == 10 then i = i + 1 end
      start = i + 1
    end
    i = i + 1
  end
  if start <= n then out[#out + 1] = s:sub(start) end
  return out
end
M.splitlines = splitlines

local function strip(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
local function startswith(s, p) return s:sub(1, #p) == p end
local function endswith(s, p) return p == "" or s:sub(-#p) == p end
local function index_xyzw(c)
  local i = ("xyzw"):find(c, 1, true)
  if not i or #c ~= 1 then fail("ValueError: substring not found") end
  return i
end

-- re.split(r",\s*(?![^()]*\))", text.strip()): split at commas that are not
-- inside a parenthesised group; the comma and following whitespace are dropped.
function M.split_args(text)
  text = strip(text)
  local out, start, i, n = {}, 1, 1, #text
  while i <= n do
    if text:sub(i, i) == "," then
      local j = i + 1
      while j <= n and not text:sub(j, j):find("[()]") do j = j + 1 end
      if text:sub(j, j) ~= ")" then
        out[#out + 1] = text:sub(start, i - 1)
        local k = i + 1
        while k <= n and text:sub(k, k):find("%s") do k = k + 1 end
        start = k
        i = k - 1
      end
    end
    i = i + 1
  end
  out[#out + 1] = text:sub(start)
  return out
end

local function u32(blob, pos0) return (string.unpack("<I4", blob, pos0 + 1)) end

-- Instruction token arrays (1-based Lua arrays of words) of the SHDR/SHEX chunk.
function M.instructions(blob)
  local count = u32(blob, 28)
  local offset
  for i = 0, count - 1 do
    local o = u32(blob, 32 + 4 * i)
    local tag = blob:sub(o + 1, o + 4)
    if tag == "SHDR" or tag == "SHEX" then offset = o break end
  end
  if offset == nil then fail("StopIteration") end
  local nwords = u32(blob, offset + 4) // 4
  local words = {}
  for i = 0, nwords - 1 do words[i + 1] = u32(blob, offset + 8 + 4 * i) end
  local cursor = 3 -- Python index 2
  local result = {}
  while cursor <= #words do
    local size = (words[cursor] >> 24) & 127
    -- D3D custom-data blocks carry their length in the following word.
    -- Opcode 53/class 3 is an immediate constant buffer, not executable code.
    if words[cursor] & 0x7ff == 53 then
      if cursor + 1 > #words then fail("truncated custom data") end
      size = words[cursor + 1]
    end
    if size < 1 or cursor - 1 + size > #words then fail("invalid instruction length") end
    result[#result + 1] = table.move(words, cursor, cursor + size - 1, 1, {})
    cursor = cursor + size
  end
  return result
end

-- re.sub(r'dcl_immediateConstantBuffer\s*\{.*?\}\s*\}', collapse whitespace, asm, flags=re.S)
local function collapse_icb(asm)
  local key = "dcl_immediateConstantBuffer"
  local out, pos = {}, 1
  while true do
    local a = asm:find(key, pos, true)
    if not a then break end
    local b = a + #key
    local _, ws = asm:find("^%s*", b)
    local matched = false
    if asm:sub(ws + 1, ws + 1) == "{" then
      local j = ws + 2
      while true do
        local c = asm:find("}", j, true)
        if not c then break end
        local _, e = asm:find("^%s*}", c + 1)
        if e then
          out[#out + 1] = asm:sub(pos, a - 1)
          out[#out + 1] = (asm:sub(a, e):gsub("%s+", " "))
          pos = e + 1
          matched = true
          break
        end
        j = c + 1
      end
    end
    if not matched then
      out[#out + 1] = asm:sub(pos, a)
      pos = a + 1
    end
  end
  out[#out + 1] = asm:sub(pos)
  return table.concat(out)
end

local function f32_of_bits(bits) return (string.unpack("<f", string.pack("<I4", bits))) end

-- Disassembly lines with every immediate replaced by its exact DXBC bit pattern.
function M.precise_lines(blob, asm)
  -- dumpbin prints one custom-data instruction across multiple text lines.
  asm = collapse_icb(asm)
  local lines = {}
  for _, x in ipairs(splitlines(asm)) do
    local s = strip(x)
    if s ~= "" and not startswith(x, "//") and not (x:find("^ps_%d") or x:find("^vs_%d")) then lines[#lines + 1] = s end
  end
  local bytecode = M.instructions(blob)
  if #lines ~= #bytecode then fail("disassembly and bytecode instruction counts differ") end
  local out = {}
  for idx = 1, #lines do
    local line, words = lines[idx], bytecode[idx]
    if words[1] & 0x7ff == 53 then
      if words[1] >> 11 ~= 3 or #words < 6 or (#words - 2) % 4 ~= 0 or not startswith(line, "dcl_immediateConstantBuffer") then
        fail("unsupported custom-data class/shape")
      end
      local hex = {}
      for i = 3, #words do hex[#hex + 1] = string.format("0x%08x", words[i]) end
      out[#out + 1] = "dcl_immediateConstantBuffer " .. table.concat(hex, ",")
    else
      local literals = {}
      local i = 2
      while i <= #words do
        if words[i] == 0x4001 or words[i] == 0x4002 then
          local n = words[i] == 0x4001 and 1 or 4
          literals[#literals + 1] = table.move(words, i + 1, math.min(i + n, #words), 1, {})
          i = i + n + 1
        else
          i = i + 1
        end
      end
      local matches = {}
      for s, inner, e in line:gmatch("()l%(([^)]*)%)()") do matches[#matches + 1] = { s = s, e = e, inner = inner } end
      if #literals ~= #matches then fail("immediate count mismatch: " .. line) end
      for k = #matches, 1, -1 do
        local match, raw = matches[k], literals[k]
        -- Check correspondence before replacing with exact bit patterns.
        local printed = {}
        for _, v in ipairs(util.split(match.inner, ",")) do printed[#printed + 1] = strip(v) end
        if #printed ~= #raw then fail("immediate width mismatch: " .. line) end
        for j = 1, #printed do
          local value, bits = printed[j], raw[j]
          if startswith(value, "0x") then
            assert(tonumber(value:sub(3), 16) == bits)
          elseif value:find(".", 1, true) then
            local actual = f32_of_bits(bits)
            local v = tonumber(value)
            assert(v and math.abs(v - actual) <= 5.1e-7 * math.max(1, math.abs(actual)), util.repr({ line, value, actual }))
          else
            local v = math.tointeger(tonumber(value))
            assert(v and (v & 0xffffffff) == bits)
          end
        end
        local hex = {}
        for j = 1, #raw do hex[j] = string.format("0x%08x", raw[j]) end
        line = line:sub(1, match.s - 1) .. "l(" .. table.concat(hex, ",") .. ")" .. line:sub(match.e)
      end
      out[#out + 1] = line
    end
  end
  return out
end

-- Base register forms accepted by operand(): returns base, rest or nil.
local BASES = {
  "^([rvo]%d+)(.*)$",
  "^(cb%d+%[%d+%])(.*)$",
  "^(x%d+%[%d+%])(.*)$",
  "^(icb%[asint%(r%d+%.[xyzw]%)%+%d+%])(.*)$",
  "^(cb%d+%[asint%(r%d+%.[xyzw]%)%+%d+%])(.*)$",
  "^(x%d+%[asint%(r%d+%.[xyzw]%)%+%d+%])(.*)$",
}
local function match_register(value)
  for _, pat in ipairs(BASES) do
    local base, rest = value:match(pat)
    if base then
      if rest == "" then return base, nil end
      local sw = rest:match("^%.([xyzw]+)$")
      if sw then return base, sw end
      return nil
    end
  end
  return nil
end

local function dynamic_index(value)
  for _, pre in ipairs({ "icb", "cb%d+", "x%d+" }) do
    local buffer, register, offset, swizzle = value:match("^(" .. pre .. ")%[(r%d+%.[xyzw]) %+ (%d+)%]%.([xyzw]+)$")
    if buffer then return buffer, register, offset, swizzle end
  end
end

function M.operand(value, mask)
  mask = mask or "xyzw"
  local neg = startswith(value, "-")
  if neg then value = value:sub(2) end
  local absolute = startswith(value, "|") and endswith(value, "|")
  if absolute then value = value:sub(2, -2) end
  local result
  if startswith(value, "l(") then
    local literals = util.split(value:sub(3, -2), ",")
    if #literals == 1 then
      result = "asfloat(" .. literals[1] .. "u)"
    elseif #mask > 1 then
      local parts = {}
      for c in mask:gmatch(".") do parts[#parts + 1] = "asfloat(" .. literals[index_xyzw(c)] .. "u)" end
      result = "float" .. #mask .. "(" .. table.concat(parts, ",") .. ")"
    else
      result = "asfloat(" .. literals[index_xyzw(mask)] .. "u)"
    end
  else
    local buffer, register, offset, swizzle = dynamic_index(value)
    if buffer then value = buffer .. "[asint(" .. register .. ")+" .. offset .. "]." .. swizzle end
    local base, sw = match_register(value)
    if not base then fail("unsupported operand: " .. value) end
    sw = sw or "xyzw"
    if #sw == 1 then
      result = base .. "." .. sw
    else
      if #sw ~= 4 then sw = sw .. string.rep(sw:sub(-1), 4 - #sw) end
      local picked = {}
      for c in mask:gmatch(".") do picked[#picked + 1] = sw:sub(index_xyzw(c), index_xyzw(c)) end
      result = base .. "." .. table.concat(picked)
    end
  end
  return (neg and "-" or "") .. (absolute and ("abs(" .. result .. ")") or result)
end
local operand = M.operand

local function is_resource_arg(x) return x:find("^[ts]%d+$") or x:find("^[ts]%d+%.[xyzw]+$") end

local ROUND = { rsq = "rsqrt", log = "log2", exp = "exp2", frc = "frac", round_ni = "floor", round_pi = "ceil", round_ne = "round", round_z = "trunc" }
local UNARY = { rsq = true, sqrt = true, log = true, exp = true, frc = true, round_ni = true, round_pi = true, round_ne = true, round_z = true }
local COMPARE = { lt = "<", ge = ">=", eq = "==", ne = "!=", ine = "!=", ilt = "<", ige = ">=" }

function M.translate(lines, output_registers, return_statement)
  if return_statement == nil then return_statement = "return o0;" end
  local textures = {}
  for _, line in ipairs(lines) do
    if startswith(line, "dcl_resource_") then
      local kind, t = line:match("^dcl_resource_texture(%w+) .* t(%d+)$")
      if not (kind == "2d" or kind == "3d" or kind == "cube") then fail("unsupported resource declaration: " .. line) end
      textures[tonumber(t)] = kind
    end
  end
  local branches, loops = {}, 0
  local out = {}
  for _, i in ipairs(output_registers or { 0 }) do out[#out + 1] = "precise float4 o" .. i .. "=0.0;" end
  for _, line in ipairs(lines) do
    local name, rest = line:match("^(.-) (.*)$")
    if not name then name, rest = line, "" end
    if startswith(name, "dcl_") then
      if name == "dcl_immediateConstantBuffer" then
        local raw = util.split(rest, ",")
        if #raw % 4 ~= 0 then fail("bad immediate constant buffer") end
        local vecs = {}
        for i = 1, #raw, 4 do
          local f = {}
          for j = i, i + 3 do f[#f + 1] = "asfloat(" .. raw[j] .. "u)" end
          vecs[#vecs + 1] = "float4(" .. table.concat(f, ",") .. ")"
        end
        out[#out + 1] = "const float4 icb[" .. (#raw // 4) .. "]={" .. table.concat(vecs, ",") .. "};"
      end
      if name == "dcl_indexableTemp" then
        local reg, size = rest:match("^(x%d+)%[(%d+)%], 4$")
        if not reg then fail("unsupported indexable temp: " .. line) end
        out[#out + 1] = "precise float4 " .. reg .. "[" .. size .. "];"
      end
      if name == "dcl_temps" then
        local n = math.tointeger(tonumber(strip(rest)))
        if not n then fail("ValueError: invalid literal for int(): " .. rest) end
        for i = 0, n - 1 do out[#out + 1] = "precise float4 r" .. i .. "=0.0;" end
      end
      goto continue
    end
    if name == "ret" then
      if return_statement and return_statement ~= "" then out[#out + 1] = return_statement end
      goto continue
    end
    if name == "loop" then loops = loops + 1; out[#out + 1] = "[loop] while(true){"; goto continue end
    if name == "endloop" then
      if loops == 0 then fail("unmatched endloop") end
      loops = loops - 1; out[#out + 1] = "}"; goto continue
    end
    if name == "breakc_nz" or name == "breakc_z" then
      if loops == 0 then fail("break outside loop") end
      out[#out + 1] = "if(asuint(" .. operand(strip(rest), "x") .. ")" .. (name == "breakc_nz" and "!=" or "==") .. "0u) break;"
      goto continue
    end
    if name == "if_nz" or name == "if_z" then
      out[#out + 1] = "if(asuint(" .. operand(strip(rest), "x") .. ")" .. (name == "if_nz" and "!=" or "==") .. "0u){"
      branches[#branches + 1] = false
      goto continue
    end
    if name == "else" then
      if #branches == 0 or branches[#branches] then fail("unmatched/duplicate else") end
      branches[#branches] = true
      out[#out + 1] = "}else{"
      goto continue
    end
    if name == "endif" then
      if #branches == 0 then fail("unmatched endif") end
      branches[#branches] = nil
      out[#out + 1] = "}"
      goto continue
    end
    do
      local args = M.split_args(rest)
      if name == "discard_nz" then
        out[#out + 1] = "if(asuint(" .. operand(args[1], "x") .. ")!=0u) clip(-1.0);"
        goto continue
      end
      if name == "sincos" and (args[1] == "null" or args[2] == "null") then
        -- One of the two destinations (sin, cos) is null: only the other is written.
        for k, fn in ipairs({ "sin", "cos" }) do
          if args[k] ~= "null" then
            local d, m = args[k]:match("^(.*)%.([^.]*)$")
            if not d then fail("ValueError: unpack sincos destination: " .. line) end
            out[#out + 1] = d .. "." .. m .. "=" .. fn .. "(" .. operand(args[3], m) .. ");"
          end
        end
        goto continue
      end
      local dest, mask = args[1]:match("^(.*)%.([^.]*)$")
      if not dest then fail("ValueError: not enough values to unpack: " .. line) end
      local dreg, dr, doff = dest:match("^(x%d+)%[(r%d+%.[xyzw]) %+ (%d+)%]$")
      if dreg then dest = dreg .. "[asint(" .. dr .. ")+" .. doff .. "]" end
      local sat = endswith(name, "_sat")
      if sat then name = name:sub(1, -5) end
      local v = {}
      for k = 2, #args do
        if not is_resource_arg(args[k]) then v[#v + 1] = operand(args[k], mask) end
      end
      local expr
      if name == "sincos" then
        -- DXBC has two destinations; a null destination is unsupported here.
        local parts = util.split(args[2], ".")
        if #parts ~= 2 then fail("ValueError: unpack sincos destination: " .. line) end
        local d2, m2 = parts[1], parts[2]
        out[#out + 1] = dest .. "." .. mask .. "=sin(" .. operand(args[3], mask) .. ");"
        out[#out + 1] = d2 .. "." .. m2 .. "=cos(" .. operand(args[3], m2) .. ");"
        goto continue
      end
      if name == "sample" or name == "sample_l" then
        local tnum, tsw = args[3]:match("^t(%d+)%.([xyzw]+)$")
        local sampler = math.tointeger(tonumber(args[4]:sub(2)))
        local t = tonumber(tnum)
        if not textures[t] then fail("sample uses undeclared texture t" .. tostring(t)) end
        local dim = textures[t] == "2d" and "xy" or "xyz"
        local coords = operand(args[2], dim)
        if dim == "xy" then
          coords = "float2((" .. coords .. ").x,lerp((" .. coords .. ").y,1.0-(" .. coords .. ").y,UVFlip))"
        end
        local call = "Texture" .. t .. ".Sample" .. (name == "sample_l" and "Level" or "") .. "(Sampler" .. sampler .. "," .. coords
        if name == "sample_l" then call = call .. "," .. operand(args[5], "x") end
        call = call .. ")"
        local picked = {}
        for c in mask:gmatch(".") do
          local ix = index_xyzw(c)
          if ix > #tsw then fail("IndexError: string index out of range") end
          picked[#picked + 1] = tsw:sub(ix, ix)
        end
        expr = "(" .. call .. ")." .. table.concat(picked)
      elseif name == "mov" then expr = v[1]
      elseif name == "add" then expr = "(" .. v[1] .. "+" .. v[2] .. ")"
      elseif name == "mul" then expr = "(" .. v[1] .. "*" .. v[2] .. ")"
      elseif name == "mad" then expr = "mad(" .. v[1] .. "," .. v[2] .. "," .. v[3] .. ")"
      elseif name == "div" then expr = "(" .. v[1] .. "/" .. v[2] .. ")"
      elseif name == "min" or name == "max" then expr = name .. "(" .. v[1] .. "," .. v[2] .. ")"
      elseif name == "dp2" or name == "dp3" or name == "dp4" then
        local m = ("xyzw"):sub(1, tonumber(name:sub(-1)))
        expr = "dot(" .. operand(args[2], m) .. "," .. operand(args[3], m) .. ")"
      elseif UNARY[name] then
        expr = (ROUND[name] or name) .. "(" .. v[1] .. ")"
      elseif COMPARE[name] then
        -- Comparisons write all-one bit masks, NOT the float number 1.
        local terms = {}
        for c in mask:gmatch(".") do
          local a, b = operand(args[2], c), operand(args[3], c)
          if name == "ine" or name == "ilt" or name == "ige" then a, b = "asint(" .. a .. ")", "asint(" .. b .. ")" end
          terms[#terms + 1] = "asfloat((" .. a .. COMPARE[name] .. b .. ")?0xffffffffu:0u)"
        end
        expr = #terms == 1 and terms[1] or ("float" .. #terms .. "(" .. table.concat(terms, ",") .. ")")
      elseif name == "and" then expr = "asfloat(asuint(" .. v[1] .. ")&asuint(" .. v[2] .. "))"
      elseif name == "ishl" or name == "ishr" or name == "ushr" then
        local lhs = name == "ishr" and ("asint(" .. v[1] .. ")") or ("asuint(" .. v[1] .. ")")
        expr = "asfloat(" .. lhs .. (name == "ishl" and "<<" or ">>") .. "(asuint(" .. v[2] .. ")&31u))"
      elseif name == "iadd" then expr = "asfloat(asuint(" .. v[1] .. ")+asuint(" .. v[2] .. "))"
      elseif name == "itof" or name == "utof" then
        expr = "float" .. (#mask > 1 and tostring(#mask) or "") .. "(" .. (name == "itof" and "asint" or "asuint") .. "(" .. v[1] .. "))"
      elseif name == "ftou" then expr = "asfloat((uint" .. (#mask > 1 and tostring(#mask) or "") .. ")(" .. v[1] .. "))"
      elseif name == "ftoi" then expr = "asfloat((int" .. (#mask == 1 and "" or tostring(#mask)) .. ")(" .. v[1] .. "))"
      elseif name == "movc" then
        local terms = {}
        for c in mask:gmatch(".") do
          terms[#terms + 1] = "(asuint(" .. operand(args[2], c) .. ")!=0u?" .. operand(args[3], c) .. ":" .. operand(args[4], c) .. ")"
        end
        expr = #terms == 1 and terms[1] or ("float" .. #terms .. "(" .. table.concat(terms, ",") .. ")")
      else
        fail("unsupported instruction: " .. line)
      end
      out[#out + 1] = dest .. "." .. mask .. "=" .. (sat and ("saturate(" .. expr .. ")") or expr) .. ";"
    end
    ::continue::
  end
  if loops ~= 0 then fail("unterminated shader loop") end
  if #branches ~= 0 then fail("unterminated shader branch") end
  return table.concat(out, "\n")
end

-- Shared helpers for the installed-shader generators -------------------------

-- Python format(v, '.17g') + 'f' / '.0f' (C++ float literal of a property default).
function M.number(v)
  local s = string.format("%.17g", v)
  if s == "inf" or s == "-inf" or s == "nan" or s == "-nan" then
    s = (s:find("^%-") and "-" or "") .. (s:find("n$") and "nan" or "inf")
  end
  return s .. ((s:find(".", 1, true) or s:find("e", 1, true)) and "f" or ".0f")
end

function M.sha256(s) return ok.hash.sha256(s) end

-- {index: name} from Unity m_NameIndices pairs [[name, index], ...]
function M.names(pairs_list)
  local t = {}
  for _, p in ipairs(pairs_list) do t[p[2]] = p[1] end
  return t
end

-- Python list(seq(v)): scalar or list -> list.
function M.seq(v) if type(v) == "table" then return v end return { v } end

-- First Shader object named `shader` in a bundle (UnityPy read_typetree()).
-- Byte arrays are fetched into a temporary folder.
function M.shader_tree(bundle, shader)
  local dir = path.temp("_shader")
  local objs = ok.kit.unity_objects(bundle, { type = "Shader", name = shader, bytes_dir = dir })
  local tree = objs[1] and objs[1].tree
  if not tree then path.rmtree(dir); fail("StopIteration: no Shader " .. shader .. " in " .. tostring(bundle)) end
  tree.compressedBlob = ok.kit.bytes(tree.compressedBlob)
  path.rmtree(dir)
  return tree
end

-- All Shader objects (UnityPy env.objects order) with materialised compressedBlob.
function M.shader_trees(bundle)
  local dir = path.temp("_shader")
  local objs = ok.kit.unity_objects(bundle, { type = "Shader", bytes_dir = dir })
  for _, o in ipairs(objs) do o.tree.compressedBlob = ok.kit.bytes(o.tree.compressedBlob) end
  path.rmtree(dir)
  return objs
end

-- Decompressed program data of the first platform equal to `platform`
-- (b''.join(lz4.block.decompress(blob[o:o+s], uncompressed_size=n) ...)).
function M.platform_raw(tree, platform)
  local blob = tree.compressedBlob
  for i, p in ipairs(tree.platforms) do
    if p == platform then
      local offsets, sizes, lengths = M.seq(tree.offsets[i]), M.seq(tree.compressedLengths[i]), M.seq(tree.decompressedLengths[i])
      local parts = {}
      for k = 1, math.min(#offsets, #sizes, #lengths) do
        parts[k] = ok.kit.lz4_block_decompress_bytes(blob:sub(offsets[k] + 1, offsets[k] + sizes[k]), lengths[k])
      end
      return table.concat(parts)
    end
  end
  fail("StopIteration: platform " .. tostring(platform) .. " not present")
end

-- Every DXBC container found by scanning `raw` front to back.
function M.scan_dxbc(raw)
  local blobs, cursor = {}, 1
  while true do
    cursor = raw:find("DXBC", cursor, true)
    if not cursor then break end
    local size = string.unpack("<I4", raw, cursor + 24)
    assert(32 <= size and size <= #raw - cursor + 1)
    blobs[#blobs + 1] = raw:sub(cursor, cursor + size - 1)
    cursor = cursor + size
  end
  return blobs
end

-- UnityPy EndianBinaryReader helpers on a byte string (0-based positions).
local Reader = {}
Reader.__index = Reader
function M.reader(data) return setmetatable({ data = data, pos = 0 }, Reader) end
function Reader:int() local v = string.unpack("<i4", self.data, self.pos + 1); self.pos = self.pos + 4; return v end
function Reader:align() self.pos = (self.pos + 3) // 4 * 4 end
function Reader:bytes(n) local s = self.data:sub(self.pos + 1, self.pos + n); self.pos = self.pos + n; return s end
function Reader:aligned_string() local n = self:int(); local s = self:bytes(n); self:align(); return s end

-- UnityPy.export.ShaderConverter.ShaderSubProgram(reader) at reader.pos.
function M.sub_program(reader)
  local sp = {}
  sp.m_Version = reader:int()
  sp.m_ProgramType = reader:int()
  reader.pos = reader.pos + 12
  if sp.m_Version >= 201608170 then reader.pos = reader.pos + 4 end
  local n = reader:int()
  sp.m_Keywords = {}
  for i = 1, n do sp.m_Keywords[i] = reader:aligned_string() end
  if sp.m_Version >= 201806140 and sp.m_Version < 202012090 then
    local m = reader:int()
    sp.m_LocalKeywords = {}
    for i = 1, m do sp.m_LocalKeywords[i] = reader:aligned_string() end
  end
  local size = reader:int()
  sp.m_ProgramCode = reader:bytes(size)
  reader:align()
  return sp
end

-- Sub-program `index` of a ShaderProgram blob with entry size `stride` (8 before 2019.3, else 12).
function M.sub_program_at(raw, index, stride)
  local r = M.reader(raw)
  r.pos = 4 + index * stride
  r.pos = r:int()
  return M.sub_program(r), r
end

-- First DXBC container inside program code (Python code[code.find(b'DXBC'):][:size]).
function M.dxbc_in(code, must)
  local start = code:find("DXBC", 1, true)
  if not start then
    if must == "index" then fail("ValueError: subsection not found") end
    assert(false)
  end
  local size = string.unpack("<I4", code, start + 24)
  return code:sub(start, start + size - 1)
end

-- [int(n) for _, n in re.findall(r'dcl_constantbuffer CB(\d+)\[(\d+)\]', asm)]
function M.cb_sizes(asm)
  local out = {}
  for _, n in asm:gmatch("dcl_constantbuffer CB(%d+)%[(%d+)%]") do out[#out + 1] = math.tointeger(tonumber(n)) end
  return out
end
-- {int(b): int(n)} keeping first-seen order (dict comprehension).
function M.cb_size_map(asm)
  local out = json.object()
  for b, n in asm:gmatch("dcl_constantbuffer CB(%d+)%[(%d+)%]") do out[math.tointeger(tonumber(b))] = math.tointeger(tonumber(n)) end
  return out
end

-- re.search(rf'\bt{index}\.\w+, s{sampler}\b', asm)
function M.uses_sampler(asm, index, sampler)
  return asm:find("%f[%w_]t" .. index .. "%.[%w_]+, s" .. sampler .. "%f[^%w_]") ~= nil
end
-- set(int(x) for x in re.findall(rf'\bt{index}\.\w+, s(\d+)\b', asm))
function M.samplers_of(asm, index)
  local set, list = {}, {}
  for s in asm:gmatch("%f[%w_]t" .. index .. "%.[%w_]+, s(%d+)%f[^%w_]") do
    local v = math.tointeger(tonumber(s))
    if not set[v] then set[v] = true; list[#list + 1] = v end
  end
  return list
end

M.FXC = "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64/fxc.exe"
-- subprocess.run([fxc, '/dumpbin', '/nologo', path], capture_output=True, check=True).stdout.decode()
function M.dumpbin(file)
  local res = ok.proc.run({ M.FXC, "/dumpbin", "/nologo", (path.norm(file):gsub("/", "\\")) }, { capture = true, text = false })
  if not res.ok then
    error(("Command '%s' returned non-zero exit status %d.\n%s"):format(M.FXC, res.code, res.stderr or ""), 0)
  end
  return res.stdout
end

-- Python str(v) for an int-valued metadata cell.
function M.str(v) return util.str(v) end

-- 'TEXT(R"OKSHADER(' + body[start:start+6000] + ')OKSHADER")' chunks.
function M.text_chunks(body, list)
  list = list or {}
  for start = 1, #body, 6000 do list[#list + 1] = 'TEXT(R"OKSHADER(' .. body:sub(start, start + 5999) .. ')OKSHADER")' end
  return list
end

-- '{TEXT("name"),type,true|false,FLinearColor(...),TEXT("default")},'
function M.property_line(p)
  local vals = {}
  for i = 0, 3 do vals[#vals + 1] = M.number(p["m_DefValue[" .. i .. "]"]) end
  return '{TEXT("' .. p.m_Name .. '"),' .. util.str(p.m_Type) .. ',' .. ((p.m_Flags & 32 ~= 0) and "true" or "false")
    .. ',FLinearColor(' .. table.concat(vals, ",") .. '),TEXT("' .. p.m_DefTexture.m_DefaultName .. '")},'
end

-- '{TEXT("name"),cb,offset,dim,type,rows},' for one vector/matrix uniform.
function M.uniform_line(names, cb, u)
  local f = { cb, u.m_Index, M.get(u, "m_Dim", 4), u.m_Type, M.get(u, "m_RowCount", 0) }
  for i, v in ipairs(f) do f[i] = util.str(v) end
  return '{TEXT("' .. names[u.m_NameIndex] .. '"),' .. table.concat(f, ",") .. "},"
end
-- '{TEXT("name"),a,b,c},' for one texture binding.
function M.texture_line(name, ...)
  local f = { ... }
  for i, v in ipairs(f) do f[i] = util.str(v) end
  return '{TEXT("' .. name .. '"),' .. table.concat(f, ",") .. "},"
end
-- [names[i] for i in indices]
function M.keyword_names(names, indices)
  local out = {}
  for i, k in ipairs(indices or {}) do out[i] = names[k] end
  return out
end

-- Python list equality for flat lists of scalars/strings.
function M.list_eq(a, b)
  if #a ~= #b then return false end
  for i = 1, #a do if a[i] ~= b[i] then return false end end
  return true
end

-- Python `v.get(k, default)` for decoded JSON objects.
function M.get(t, k, default) local v = t[k]; if v == nil then return default end return v end

-- Python list concatenation.
function M.concat(a, b) local out = {}; for _, v in ipairs(a) do out[#out + 1] = v end; for _, v in ipairs(b or {}) do out[#out + 1] = v end; return out end

-- Shared body of generate_forge_alpha333 / generate_forge_item332 / generate_forge_opaque340:
-- prepare the installed EC Forge program for independent GPU parity and write the
-- checked-in production bindings. cfg: shader, source, out, bundle, indices, passes
-- ({index, native, label}...), sizes, tp (type prefix), file (production file prefix),
-- bindings_header, code_header, message.
function M.generate_forge_parity(cfg)
  local root = ok.root
  local source = root .. "/" .. cfg.source
  local out = root .. "/" .. cfg.out
  path.mkdir(out)
  local tree = M.shader_tree(cfg.bundle, cfg.shader)
  local parsed = tree.m_ParsedForm
  local raw = M.platform_raw(tree, 4)
  local installed = M.scan_dxbc(raw)
  path.write_text(out .. "/parsed.json", json.encode(parsed, { ensure_ascii = false, indent = 2 }))
  -- The native binding IDs and raw-scan DXBC IDs are separate namespaces.
  -- Keep both, along with resource signatures, so they cannot be interchanged.
  local p = parsed.m_SubShaders[1].m_Passes[2]
  local names = M.names(p.m_NameIndices)
  local programs = json.array({})
  for _, program in ipairs(p.progFragment.m_SubPrograms) do
    if program.m_GpuProgramType == 15 or program.m_GpuProgramType == 17 then
      local keywords, cb_sizes, uniforms, textures = json.array({}), json.array({}), json.array({}), json.array({})
      for _, i in ipairs(M.get(program, "m_KeywordIndices", {})) do keywords[#keywords + 1] = names[i] end
      for i, c in ipairs(program.m_ConstantBuffers) do
        cb_sizes[#cb_sizes + 1] = c.m_Size // 16
        for _, v in ipairs(M.concat(c.m_VectorParams, c.m_MatrixParams)) do
          uniforms[#uniforms + 1] = json.object({ { "cb", i - 1 }, { "name", names[v.m_NameIndex] }, { "offset", v.m_Index }, { "type", v.m_Type },
            { "dim", M.get(v, "m_Dim", 4) }, { "rows", M.get(v, "m_RowCount", 0) } })
        end
      end
      for _, v in ipairs(program.m_TextureParams) do
        textures[#textures + 1] = json.object({ { "index", v.m_Index }, { "name", names[v.m_NameIndex] }, { "sampler", v.m_SamplerIndex }, { "dim", v.m_Dim } })
      end
      programs[#programs + 1] = json.object({ { "native_blob", program.m_BlobIndex }, { "keywords", keywords }, { "cb_sizes", cb_sizes },
        { "uniforms", uniforms }, { "textures", textures } })
    end
  end
  local function src(i, ext) return string.format("%s/%04d.%s", source, i, ext) end
  for _, index in ipairs(cfg.indices) do
    local blob = path.read_bytes(src(index, "dxbc"))
    assert(blob == installed[index + 1], "cached raw-scan " .. index .. " differs from installed shader")
    local body = M.translate(M.precise_lines(blob, path.read_text(src(index, "asm.txt"))))
    path.write_text(out .. "/Forward" .. index .. ".hlsl", body .. "\n")
    path.write_bytes(out .. "/Forward" .. index .. ".dxbc", blob)
  end
  local raw_scan = json.array({})
  for _, i in ipairs(cfg.indices) do
    raw_scan[#raw_scan + 1] = json.object({ { "index", i }, { "sha256", M.sha256(path.read_bytes(src(i, "dxbc"))) } })
  end
  local report = json.object({ { "shader", cfg.shader }, { "source_bundle_sha256", ok.hash.sha256_file(cfg.bundle) },
    { "raw_scan_programs", raw_scan }, { "native_programs", programs }, { "properties", parsed.m_PropInfo.m_Props },
    { "status", "translation prepared; GPU parity and renderer integration pending" } })
  path.write_text(out .. "/Bindings.json", json.encode(report, { ensure_ascii = false, indent = 2 }))
  print(cfg.message)

  -- Checked-in production source is generated from the same verified programs.
  local production = root .. "/Plugins/KKUnityBundle/Source/KKUnityBundle/Private/Shaders"
  local tp = cfg.tp
  local bindings = { cfg.bindings_header }
  local code = { cfg.code_header }
  for _, spec in ipairs(cfg.passes) do
    local index, native, label = spec[1], spec[2], spec[3]
    local program
    for _, q in ipairs(programs) do if q.native_blob == native then program = q break end end
    assert(program, "StopIteration")
    local asm = path.read_text(src(index, "asm.txt"))
    local sizes = M.cb_sizes(asm)
    assert(M.list_eq(sizes, cfg.sizes))
    bindings[#bindings + 1] = "static const int32 G" .. tp .. label .. "CBSize[]={" .. table.concat(sizes, ",") .. "};"
    bindings[#bindings + 1] = "static const FOK" .. tp .. "Uniform G" .. tp .. label .. "Uniforms[]={"
    for _, u in ipairs(program.uniforms) do
      assert(u.offset // 16 < sizes[u.cb + 1])
      bindings[#bindings + 1] = M.texture_line(u.name, u.cb, u.offset, u.dim, u.type, u.rows)
    end
    bindings[#bindings + 1] = "};"
    bindings[#bindings + 1] = "static const FOK" .. tp .. "Texture G" .. tp .. label .. "Textures[]={"
    for _, t in ipairs(program.textures) do
      assert(M.uses_sampler(asm, t.index, t.sampler), util.repr(t))
      bindings[#bindings + 1] = M.texture_line(t.name, t.index, t.sampler, t.dim)
    end
    bindings[#bindings + 1] = "};"
    code[#code + 1] = "// SHA256 " .. M.sha256(path.read_bytes(src(index, "dxbc")))
    code[#code + 1] = "static const TCHAR* G" .. tp .. label .. "Program="
    M.text_chunks("\n" .. path.read_text(out .. "/Forward" .. index .. ".hlsl"), code)
    code[#code + 1] = ";"
  end
  bindings[#bindings + 1] = "static const FOK" .. tp .. "Property G" .. tp .. "Properties[]={"
  for _, prop in ipairs(report.properties) do bindings[#bindings + 1] = M.property_line(prop) end
  bindings[#bindings + 1] = "};"
  path.write_text(production .. "/" .. cfg.file .. "Bindings.inl", table.concat(bindings, "\n") .. "\n")
  path.write_text(production .. "/" .. cfg.file .. "Program.inl", table.concat(code, "\n") .. "\n")
end

local function main()
  local p = ok.args.parser("Translate the supported straight-line D3D11 pixel program into HLSL.")
  p:arg("source")
  p:arg("output")
  local args = p:parse(arg)
  local parsed = ok.read_json(path.join(args.source, "parsed.json"))
  if parsed.m_Name ~= "xukmi/MainItemPlus" then fail("wrong source shader") end
  path.mkdir(args.output)
  local header = { "// Generated by Tools/shader_program_codegen.lua from installed Vanilla Plus 1.7.2.", "// Exact immediate bit patterns retained; regenerate and run the DXBC GPU oracle after edits." }
  local bindings = { "// Generated source property declarations and native constant-buffer bindings." }
  local programs = json.array({})
  for _, spec in ipairs({ { 1, 6, "Forward" }, { 0, 1, "Outline" } }) do
    local pass_index, blob_index, label = spec[1], spec[2], spec[3]
    local blob = path.read_bytes(path.join(args.source, string.format("%04d.dxbc", blob_index)))
    local lines = M.precise_lines(blob, path.read_text(path.join(args.source, string.format("%04d.asm.txt", blob_index))))
    local body = M.translate(lines)
    path.write_text(path.join(args.output, "MainItemPlus" .. label .. ".hlsl"), body .. "\n")
    local shader_pass = parsed.m_SubShaders[1].m_Passes[pass_index + 1]
    local names = M.names(shader_pass.m_NameIndices)
    local program
    for _, v in ipairs(shader_pass.progFragment.m_SubPrograms) do if v.m_BlobIndex == blob_index then program = v break end end
    if not program then fail("StopIteration") end
    local cb_sizes, uniforms, textures = json.array({}), json.array({}), json.array({})
    for i, c in ipairs(program.m_ConstantBuffers) do
      cb_sizes[#cb_sizes + 1] = c.m_Size // 16
      for _, v in ipairs(M.concat(c.m_VectorParams, c.m_MatrixParams)) do
        uniforms[#uniforms + 1] = json.object({ { "cb", i - 1 }, { "name", names[v.m_NameIndex] }, { "offset", v.m_Index }, { "type", v.m_Type },
          { "dim", M.get(v, "m_Dim", 4) }, { "rows", M.get(v, "m_RowCount", 0) } })
      end
    end
    for _, v in ipairs(program.m_TextureParams) do
      textures[#textures + 1] = json.object({ { "index", v.m_Index }, { "name", names[v.m_NameIndex] }, { "sampler", v.m_SamplerIndex }, { "dim", v.m_Dim } })
    end
    local metadata = json.object({ { "label", label }, { "blob", blob_index }, { "sha256", M.sha256(blob) }, { "cb_sizes", cb_sizes },
      { "uniforms", uniforms }, { "textures", textures } })
    programs[#programs + 1] = metadata
    local sizes = {}
    for i, n in ipairs(cb_sizes) do sizes[i] = util.str(n) end
    bindings[#bindings + 1] = "static const int32 G" .. label .. "CBSize[]={" .. table.concat(sizes, ",") .. "};"
    bindings[#bindings + 1] = "static const FOKItemUniform G" .. label .. "Uniforms[]={"
    for _, u in ipairs(uniforms) do
      local f = {}
      for _, k in ipairs({ "cb", "offset", "dim", "type", "rows" }) do f[#f + 1] = util.str(u[k]) end
      bindings[#bindings + 1] = '{TEXT("' .. u.name .. '"),' .. table.concat(f, ",") .. "},"
    end
    bindings[#bindings + 1] = "};"
    bindings[#bindings + 1] = "static const FOKItemTexture G" .. label .. "Textures[]={"
    for _, t in ipairs(textures) do
      local f = {}
      for _, k in ipairs({ "index", "sampler", "dim" }) do f[#f + 1] = util.str(t[k]) end
      bindings[#bindings + 1] = '{TEXT("' .. t.name .. '"),' .. table.concat(f, ",") .. "},"
    end
    bindings[#bindings + 1] = "};"
    header[#header + 1] = "// DXBC SHA256 " .. metadata.sha256
    -- MSVC limits an individual string literal, even when it is a raw string.
    -- Adjacent small literals preserve the generated program byte for byte.
    header[#header + 1] = "static const TCHAR* GMainItemPlus" .. label .. "="
    M.text_chunks("\n" .. body .. "\n", header)
    header[#header + 1] = ";"
  end
  path.write_text(path.join(args.output, "MainItemPlusProgram.inl"), table.concat(header, "\n") .. "\n")
  bindings[#bindings + 1] = "static const FOKItemProperty GItemProperties[]={"
  for _, prop in ipairs(parsed.m_PropInfo.m_Props) do bindings[#bindings + 1] = M.property_line(prop) end
  bindings[#bindings + 1] = "};"
  path.write_text(path.join(args.output, "MainItemPlusBindings.inl"), table.concat(bindings, "\n") .. "\n")
  path.write_text(path.join(args.output, "MainItemPlusBindings.json"),
    json.encode(json.object({ { "programs", programs }, { "properties", parsed.m_PropInfo.m_Props } }), { indent = 2 }))
  print("Generated exact Forward and Outline programs")
end

if ok.is_main() then ok.main(main) end
return M
