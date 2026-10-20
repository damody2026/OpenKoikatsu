-- Generate the maker registry from the installed ABMX BoneMeta definitions.
--   lua.exe Tools/generate_bone_controls.lua <InterfaceData.cs> [--output Source/OpenKoikatsu/OKBoneControls.h]
-- Module use: require("generate_bone_controls").generate(source) -> header, count
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util
local M = {}

local terms_cache
local function terms()
  if not terms_cache then
    local t = ok.read_json(ok.root .. "/Tools/editor_bone_terms_zh_tw.json")
    local keys = util.sorted(json.keys(t), function(k) return -#k end)
    terms_cache = { map = t, keys = keys }
  end
  return terms_cache
end

-- Python str.strip() whitespace: ASCII plus the Unicode spaces that may appear in labels.
local USPACE = { "\u{3000}", "\u{a0}", "\u{2000}", "\u{2001}", "\u{2002}", "\u{2003}", "\u{2004}", "\u{2005}", "\u{2006}", "\u{2007}", "\u{2008}", "\u{2009}", "\u{200a}", "\u{2028}", "\u{2029}", "\u{202f}", "\u{205f}", "\u{85}", "\u{1680}" }
local function pystrip(s)
  local changed = true
  while changed do
    changed = false
    local t = s:gsub("^[ \t\n\r\v\f\28\29\30\31]+", ""):gsub("[ \t\n\r\v\f\28\29\30\31]+$", "")
    if t ~= s then s = t; changed = true end
    for _, u in ipairs(USPACE) do
      if s:sub(1, #u) == u then s = s:sub(#u + 1); changed = true end
      if #s >= #u and s:sub(-#u) == u then s = s:sub(1, -#u - 1); changed = true end
    end
  end
  return s
end

function M.translate_label(label)
  local tm = terms()
  local out, i, n = {}, 1, #label
  while i <= n do
    local hit
    for _, k in ipairs(tm.keys) do
      if #k > 0 and label:sub(i, i + #k - 1) == k then hit = k break end
    end
    if hit then out[#out + 1] = tm.map[hit]; i = i + #hit
    else out[#out + 1] = label:sub(i, i); i = i + 1 end
  end
  local result = table.concat(out)
  if result:find("[A-Wa-z]") then error(("Missing Traditional Chinese editor label: %s -> %s"):format(label, result), 0) end
  return pystrip((result:gsub(" +", " ")))
end

-- re.findall of the BoneMeta constructor pattern: {bone, label, lo, hi, category, right, options}
local function bone_metas(text)
  local out, pos = {}, 1
  while true do
    local a, b = text:find("new BoneMeta(", pos, true)
    if not a then break end
    pos = a + 1
    local s = b + 1
    local bone, label, lo, hi, category, e = text:match('^"([^"]+)", "([^"]+)", ([%d.]+)f, ([%d.]+)f, ([%w_]+)()', s)
    if bone then
      local right, options = "", ""
      local r, e2 = text:match('^, "([^"]*)"()', e)
      if r then right = r; e = e2 end
      if text:sub(e, e) == ")" then
        e = e + 1
        local opt = text:match("^%s*{([^}]+)}", e)
        if opt then options = opt end
        out[#out + 1] = { bone, label, lo, hi, category, right, options }
      end
    end
  end
  return out
end

local function float(s)
  local v = tonumber(s)
  if v == nil then error("ValueError: could not convert string to float: '" .. s .. "'", 0) end
  return v + 0.0
end

function M.generate(source)
  local text = path.read_text(source)
  local rows = {}
  for _, m in ipairs(bone_metas(text)) do
    local bone, label, lo, hi, category, right, options = table.unpack(m)
    local axes = {}
    for i, axis in ipairs({ "X", "Y", "Z" }) do
      if not options:find(axis .. " = false", 1, true) then axes[#axes + 1] = i - 1 end
    end
    if #axes == 3 then
      local postfix = options:find("XYZPostfix = null", 1, true) and "" or " Scale"
      rows[#rows + 1] = { category, label .. postfix, bone, right, -1, float(lo), float(hi) }
    end
    for _, axis in ipairs(axes) do
      local letter = ("XYZ"):sub(axis + 1, axis + 1)
      local custom = options:match(letter .. 'DisplayName = "([^"]+)"')
      local name = custom or (label .. " Scale " .. letter)
      rows[#rows + 1] = { category, name, bone, right, axis, float(lo), float(hi) }
    end
    if options:find("%f[%w_]L = true") then
      local custom = options:match('LDisplayName = "([^"]+)"')
      local limit = options:match("LMax = ([%d.]+)f")
      rows[#rows + 1] = { category, custom or (label .. " Length"), bone, right, 3, 0.1, limit and float(limit) or float(hi) }
    end
  end
  -- Advanced Bonemod exposes position and Euler rotation on the same bones.
  local advanced, seen = {}, {}
  for _, r in ipairs(rows) do
    local category, label, bone, right = r[1], r[2], r[3], r[4]
    if not seen[bone] then
      seen[bone] = true
      local base = util.split(label, " Scale")[1]
      for axis = 4, 9 do
        advanced[#advanced + 1] = { category, base .. " " .. (axis < 7 and "Position " or "Rotation ") .. ("XYZ"):sub((axis - 4) % 3 + 1, (axis - 4) % 3 + 1),
          bone, right, axis, axis < 7 and -1.0 or -180.0, axis < 7 and 1.0 or 180.0 }
      end
    end
  end
  for _, r in ipairs(advanced) do rows[#rows + 1] = r end
  -- The maker finger control applies the same modifier to each selected joint.
  for _, finger in ipairs({ "thumb", "index", "middle", "ring", "little" }) do
    for joint = 1, 3 do
      for _, axis in ipairs({ -1, 0, 1, 2, 3 }) do
        local kind = axis == -1 and "Scale" or (axis == 3 and "Length" or ("Scale " .. ("XYZ"):sub(axis + 1, axis + 1)))
        rows[#rows + 1] = { "BodyHands", finger:sub(1, 1):upper() .. finger:sub(2) .. " " .. joint .. " " .. kind,
          ("cf_j_%s%02d_L"):format(finger, joint), ("cf_j_%s%02d_R"):format(finger, joint), axis, axis == 3 and 0.1 or 0.0, 3.0 }
      end
    end
  end
  local header = { "// Generated by Tools/generate_bone_controls.lua from installed ABMX InterfaceData.cs.\n#pragma once\nstruct FOKBoneControl { const TCHAR* Category; const TCHAR* Label; const TCHAR* Bone; const TCHAR* Right; int32 Axis; float Min; float Max; };\ninline const FOKBoneControl OKBoneControls[] = {\n" }
  for _, r in ipairs(rows) do
    local category, label, bone, right, axis, lo, hi = table.unpack(r)
    local fields = {}
    for i, x in ipairs({ category, M.translate_label(label), bone, right }) do fields[i] = "TEXT(" .. json.encode_string(x, false) .. ")" end
    header[#header + 1] = ("    {%s, %d, %.6ff, %.6ff},\n"):format(table.concat(fields, ", "), axis, lo, hi)
  end
  return table.concat(header) .. "};\n", #rows
end

local function main()
  local p = ok.args.parser()
  p:arg("source")
  p:opt("--output", { default = "Source/OpenKoikatsu/OKBoneControls.h" })
  local args = p:parse(arg)
  local header, count = M.generate(args.source)
  path.write_text(args.output, header)
  print(count .. " ABMX controls generated")
end

if ok.is_main() then ok.main(main) end
return M
