-- Exports the original game's non-H Studio animations as street runtime clips (OKCLIP01) and writes their manifest.
--
--   Tools\lua\lua.exe Tools\export_street_animation_library.lua [--game D:/KoikatuSunshine] [--only-manifest]
--
-- Source list: Content/IndependentResources/base_v1/studio_anims.json (every Studio animation, with its group,
-- category, Japanese name and controller-state key). Each kept state that plays one clip is exported with
-- ok-extract --export-runtime-clips into Content/IndependentResources/RuntimeAnimations/library/, and
-- library.manifest.json lists them with what the street needs to choose one: posture, sex, loop, duration, the
-- Japanese name and the category. H groups, the live/dance stage shows and walking/running (the street
-- has its own locomotion) are left out. What each clip is used for (her gestures, idles by place, photo poses) is
-- Content/IndependentResources/StreetSocialData/rules/animations.toml.

local ok = require("ok")
local json = ok.json
local lfs = require("lfs")

local ROOT = "C:/portable/OpenKoikatsu/"
local game = "D:/KoikatuSunshine"
local only_manifest = false
local i = 1
while i <= #arg do
  if arg[i] == "--game" then game = arg[i + 1]; i = i + 1
  elseif arg[i] == "--only-manifest" then only_manifest = true
  else error("unknown argument " .. arg[i]) end
  i = i + 1
end

local OUT = ROOT .. "Content/IndependentResources/RuntimeAnimations/library/"
lfs.mkdir(OUT)

local function read(path) local f = assert(io.open(path, "rb")); local s = f:read("a"); f:close(); return s end
local studio = json.decode(read(ROOT .. "Content/IndependentResources/base_v1/studio_anims.json"))

-- Groups kept: キャラ, スタジオ, バトル, アウトドア, サマー, 汎用, アフタースクール, EC, サンシャイン (the rest are H).
local KEEP = { [0] = true, [7] = true, [8] = true, [10] = true, [11] = true, [12] = true, [14] = true, [15] = true, [17] = true }
local groups, categories = {}, {}
for _, g in ipairs(studio.groups) do groups[g.id] = g.name end
for _, c in ipairs(studio.categories) do categories[(c.group or -1) .. ":" .. c.id] = c.name end
-- Categories left out inside kept groups: the stage live show, walking and running, the T-pose.
local SKIP_CATEGORY = { ["0:3"] = true, ["0:13"] = true }

-- Posture from the category: what she has to be doing for the clip to fit (standing, on a chair, on the floor, lying).
local function posture(cat, name)
  if cat:find("椅子") then return "chair" end
  if cat:find("床") then return "floor" end
  if cat:find("寝") or name:find("^寝") then return "lie" end
  if name:find("座り") or name:find("椅子") then return name:find("椅子") and "chair" or "floor" end
  return "stand"
end

local rows, list = {}, {}
for _, e in ipairs(studio.anims) do
  local cat = categories[e.group .. ":" .. e.category] or ""
  if e.kind == "anime" and KEEP[e.group] and not SKIP_CATEGORY[e.group .. ":" .. e.category] and e.name ~= "初期ポーズ" then
    local id = string.format("lib_%d_%d_%d.okanim", e.group, e.category, e.no)
    local male = cat == "男" or e.name:find("^男") ~= nil
    rows[#rows + 1] = {
      id = id, file = "library/" .. id, source = e.key, name = e.name, group = groups[e.group] or "",
      category = cat, posture = posture(cat, e.name), sex = male and "m" or "f",
      loop = e.looping == true, duration = e.duration or 0,
      -- A one-frame pose (Studio's photo poses): held, not played through.
      still = (e.duration or 0) < 0.5,
    }
    list[#list + 1] = OUT .. id .. "\t" .. e.key
  end
end
print(string.format("%d clips selected", #rows))

local exported = {}
if not only_manifest then
  local tsv = OUT .. "export-list.tsv"
  local f = assert(io.open(tsv, "wb")); f:write(table.concat(list, "\n"), "\n"); f:close()
  local exe = ROOT .. "Tools/ok-extract/target/release/ok-extract.exe"
  local p = assert(io.popen('""' .. exe .. '" ' .. game .. ' --export-runtime-clips "' .. tsv .. '" 2>nul"'))
  for line in p:lines() do
    local status, file, why = line:match("^(%a+)\t([^\t]+)\t?(.*)$")
    if status == "fail" then print("skipped " .. file:match("[^/]+$") .. ": " .. why) end
    if line:find("^exported") then print(line) end
  end
  p:close()
  os.remove(tsv)
end

-- The manifest: only the clips that exist (a blend-tree state was not exported).
local kept = {}
for _, r in ipairs(rows) do
  local path = OUT .. r.id
  local f = io.open(path, "rb")
  if f then
    local bytes = f:read("a"); f:close()
    r.bytes = #bytes
    kept[#kept + 1] = r
  end
end
table.sort(kept, function(a, b) return a.id < b.id end)
local manifest = { schema = 1, purpose = "Original non-H Studio animations as street runtime clips (Tools/export_street_animation_library.lua).", actions = kept }
local f = assert(io.open(ROOT .. "Content/IndependentResources/RuntimeAnimations/library.manifest.json", "wb"))
f:write(json.encode(manifest)); f:close()
print(string.format("manifest: %d clips", #kept))
