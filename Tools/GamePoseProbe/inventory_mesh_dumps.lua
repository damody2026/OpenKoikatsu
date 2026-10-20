-- Inventory every current mesh-dump card, rejecting unresolved card names.
local ok = require "ok"
local G = require "gpp"
local json, path = ok.json, ok.path
local O, A = G.O, G.A

local M = {}
M.GAME = ok.game_root(true)

-- sorted(Path list) on Windows: case-insensitive comparison of path parts.
local function path_key(p)
  local parts = {}
  for _, s in ipairs(path.parts(p:lower())) do parts[#parts + 1] = s end
  return parts
end

local function main()
  local game = M.GAME
  local root = path.join(game, "mesh_dump")
  if not path.is_dir(root) then
    error("FileNotFoundError: [WinError 3] The system cannot find the path specified: '" .. G.winpath(root) .. "'")
  end
  local records = {}
  for _, dump in ipairs(ok.util.sorted(path.iterdir(root), path_key)) do
    if path.is_dir(dump) then
      local name = path.name(dump)
      local card = path.join(game, "UserData/chara/female", name)
      if not path.is_file(card) then error("FileNotFoundError: " .. G.winpath(card)) end
      local meshes = path.rglob(dump, "*.materials.json")
      local photos = 0
      for _, p in ipairs(path.rglob(dump, "*")) do
        local s = path.suffix(p):lower()
        if s == ".jpg" or s == ".jpeg" then photos = photos + 1 end
      end
      records[#records + 1] = O("card", name, "dump", G.winpath(dump), "mesh_count", #meshes, "photo_count", photos)
    end
  end
  local output = "Saved/Logs/Hair322ExpandedInventory.json"
  G.write_json(output, A(records))
  print(("%d cards; %d material dumps; %s"):format(#records, G.sum(records, function(r) return r.mesh_count end), G.winpath(output)))
end
M.main = main

if ok.is_main() then ok.main(main) end
return M
