-- Scan every .zipmod for animation-related inner paths (central directory only).
--
-- Emits three TSVs under Saved/anim_export/:
--   mod_anim_bundles.tsv   zipmod <TAB> inner .unity3d that holds AnimationClips
--   mod_studio_info.tsv    zipmod <TAB> inner studio/info/{*.csv,*.unity3d} catalog
--   mod_head_prefabs.tsv   zipmod <TAB> inner chara/bo_head_*.unity3d (FBSCtrl targets)
--
-- Only zip central directories are read, which is cheap even for thousands of mods.
local ok = require "ok"
local lfs = require "lfs"
local path = ok.path

local MODS = arg[1] or ok.game_root(true, "\\") .. "\\mods"
local OUTDIR = arg[2] or "C:\\portable\\OpenKoikatsu\\Saved\\anim_export"

local ANIM_DIRS = {
  "h/anim/",
  "studio/anime/",
  "studio_k/anime/",
  "esthetic/animator/",
  "adv/motion/controller/",
  "action/animator/",
  "custom/custompose",
  "twoshot/",
}

local function norm(name)
  local p = name:gsub("\\", "/"):lower()
  if p:sub(1, 1) == "/" then return p end
  return "/" .. p
end

local function endswith(s, suffix) return s:sub(-#suffix) == suffix end
local function contains(s, sub) return s:find(sub, 1, true) ~= nil end

local function classify(inner)
  local p = norm(inner)
  if endswith(p, ".unity3d") then
    for _, d in ipairs(ANIM_DIRS) do if contains(p, d) then return "anim" end end
  end
  if (endswith(p, ".csv") or endswith(p, ".unity3d")) and contains(p, "studio/info/") then return "studio_info" end
  if endswith(p, ".unity3d") and contains(p, "/chara/bo_head") then return "head" end
  return nil
end

-- os.path.join for Windows strings.
local function join(a, b)
  if a == "" then return b end
  local last = a:sub(-1)
  if last == "\\" or last == "/" or (#a == 2 and last == ":") then return a .. b end
  return a .. "\\" .. b
end

-- os.walk(top): directory listing order as the file system returns it; symlinks
-- and junctions are listed but not descended into.
local function walk(top, fn)
  local okv, iter, state = pcall(lfs.dir, top)
  if not okv then return end
  local dirs, files = {}, {}
  for name in iter, state do
    if name ~= "." and name ~= ".." then
      local full = join(top, name)
      local attr = lfs.attributes(full)
      if attr and attr.mode == "directory" then dirs[#dirs + 1] = name else files[#files + 1] = name end
    end
  end
  fn(top, dirs, files)
  for _, d in ipairs(dirs) do
    local full = join(top, d)
    local link = lfs.symlinkattributes(full)
    if not (link and link.mode == "link") then walk(full, fn) end
  end
end

local function main()
  local buckets = { anim = {}, studio_info = {}, head = {} }
  local zips = 0
  local t0 = ok.time.time()
  local zip_paths = {}
  walk(MODS, function(root, _dirs, files)
    for _, fn in ipairs(files) do
      if endswith(fn:lower(), ".zipmod") then
        zips = zips + 1
        zip_paths[#zip_paths + 1] = join(root, fn)
      end
    end
  end)
  -- Central directories in one batch (ok-luakit zip-names), in walk order.
  local listing = #zip_paths > 0 and ok.kit.call({ "zip-names", "-" }, { input = table.concat(zip_paths, "\n") }) or {}
  for _, entry in ipairs(listing) do
    if entry.names then
      for _, name in ipairs(entry.names) do
        local kind = classify(name)
        if kind then table.insert(buckets[kind], { entry.path, name }) end
      end
    end
  end
  local names = { anim = "mod_anim_bundles.tsv", studio_info = "mod_studio_info.tsv", head = "mod_head_prefabs.tsv" }
  path.mkdir(OUTDIR)
  for _, kind in ipairs({ "anim", "studio_info", "head" }) do
    local rows = buckets[kind]
    local out = join(OUTDIR, names[kind])
    local lines = {}
    for i, r in ipairs(rows) do lines[i] = r[1] .. "\t" .. r[2] .. "\n" end
    path.write_bytes(out, table.concat(lines))
    print(("ok-extract: %s: %d -> %s"):format(kind, #rows, out))
  end
  print(("ok-extract: scanned %d zipmods in %.0fs"):format(zips, ok.time.time() - t0))
  return 0
end

ok.main(main)
