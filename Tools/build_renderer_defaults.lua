-- Import common prefab renderer defaults, without any character/card input.
local ok = require "ok"
local json, path, util = ok.json, ok.path, ok.util

-- Python str.lower() for the scripts that appear in object names (Latin-1, Greek, Cyrillic, fullwidth).
local function pylower(s)
  if not s:find("[\128-\255]") then return s:lower() end
  local out = {}
  for _, cp in utf8.codes(s) do
    if cp >= 65 and cp <= 90 then cp = cp + 32
    elseif (cp >= 0xC0 and cp <= 0xDE and cp ~= 0xD7) then cp = cp + 32
    elseif cp >= 0x391 and cp <= 0x3A9 and cp ~= 0x3A2 then cp = cp + 32
    elseif cp >= 0x410 and cp <= 0x42F then cp = cp + 32
    elseif cp >= 0x400 and cp <= 0x40F then cp = cp + 80
    elseif cp >= 0xFF21 and cp <= 0xFF3A then cp = cp + 32 end
    out[#out + 1] = utf8.char(cp)
  end
  return table.concat(out)
end

-- str(Path(p)) on Windows: backslashes, no duplicate or "." components, no trailing slash.
local function pystr_path(p)
  p = p:gsub("\\", "/")
  local drive, rest = p:match("^(%a:)(.*)$")
  if not drive then drive, rest = "", p end
  local absolute = rest:sub(1, 1) == "/"
  local parts = {}
  for part in rest:gmatch("[^/]+") do if part ~= "." then parts[#parts + 1] = part end end
  local s = drive .. (absolute and "\\" or "") .. table.concat(parts, "\\")
  if s == "" then s = "." end
  return s
end

local function objects(file, kind) return ok.kit.unity_objects(file, { type = kind }) end

local function main()
  local p = ok.args.parser()
  p:opt("--bundle", { multiple = true, required = true, help = "Unity bundle path or zipmod#member" })
  p:opt("--output", { required = true })
  local a = p:parse(arg)
  if #a.bundle == 0 then p:fail("the following arguments are required: --bundle") end
  local entries = json.object()
  if path.exists(a.output) then
    local previous = ok.read_json(a.output)
    assert(previous.schema == 1)
    entries = previous.renderers
  end
  for _, source in ipairs(a.bundle) do
    local file, tmp = source, nil
    local archive, member = source:match("^([^#]*)#(.*)$")
    if archive then
      tmp = path.temp(".unity3d")
      ok.kit.call({ "zip-member", archive, member, tmp })
      file = tmp
    end
    local names = {}
    for _, o in ipairs(objects(file, "GameObject")) do names[o.path_id] = pylower(o.tree.m_Name or "") end
    local filters = {}
    for _, o in ipairs(objects(file, "MeshFilter")) do
      local t = o.tree
      filters[t.m_GameObject.m_PathID] = (t.m_Mesh or {}).m_PathID or 0
    end
    local renderers = {}
    for _, kind in ipairs({ "SkinnedMeshRenderer", "MeshRenderer" }) do
      for _, o in ipairs(objects(file, kind)) do renderers[#renderers + 1] = o end
    end
    for _, o in ipairs(renderers) do
      local t = o.tree
      local go = t.m_GameObject.m_PathID
      local mesh = (t.m_Mesh or {}).m_PathID
      if mesh == nil then mesh = filters[go] or 0 end
      if util.truthy(mesh) then
        local key = ("%d:%d"):format(mesh, o.path_id)
        local enabled = t.m_Enabled
        if enabled == nil then enabled = true end
        local value = json.object({ { "enabled", util.truthy(enabled) }, { "name", names[go] or "" } })
        if entries[key] ~= nil and not util.equal(entries[key], value) then
          -- Ambiguous source identities must never suppress an unrelated mesh.
          entries[key] = json.object({ { "ambiguous", true } })
        else
          entries[key] = value
        end
      end
    end
    if tmp then path.remove(tmp, true) end
  end
  path.mkdir(path.parent(a.output))
  path.write_text(a.output, json.encode(json.object({ { "schema", 1 }, { "renderers", entries } }), { ensure_ascii = true, sort_keys = true }))
  local count = 0
  for _ in pairs(entries) do count = count + 1 end
  print(json.encode(json.object({ { "common_renderers", count }, { "card_inputs", false }, { "output", pystr_path(a.output) } })))
end

ok.main(main)
