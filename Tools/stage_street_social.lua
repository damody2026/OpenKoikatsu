-- --base-dir: the staged engine + paks this stage links to (a different engine build needs its own base).
--
--   Tools\lua\lua.exe Tools\stage_street_social.lua --executable <OpenKoikatsu.exe> [--destination Saved/StreetSocial/Build/X.stage]
--     [--base-dir Saved/StreetSocial/Base]
-- Prints the staged executable path as the last stdout line.
local ok = require "ok"
local path, proc = ok.path, ok.proc
local lfs = require "lfs"

local function win(p) return (path.norm(p):gsub("/", "\\")) end

local M = {}

function M.stage(executable, destination, base_dir)
  local root = ok.root
  local base = path.abs(path.join(root, base_dir)):gsub("/+$", "")
  local target = path.abs(path.join(root, destination)):gsub("/+$", "")
  local prefix = (root .. "/Saved/StreetSocial/"):lower()
  if target:lower():sub(1, #prefix) ~= prefix then error("Stage must stay in Saved/StreetSocial", 0) end
  if not path.exists(executable) then error("Built Development executable required", 0) end
  -- The verified baseline uses junctions; recursive enumeration does not traverse
  -- them. Retain its immutable engine/content roots explicitly.
  for _, relative_root in ipairs({ "Windows/Engine", "Windows/OpenKoikatsu/Content" }) do
    local link = path.join(target, relative_root)
    if not path.exists(link) then
      path.mkdir(path.parent(link))
      proc.run({ "mklink", "/J", win(link), win(path.join(base, relative_root)) }, { quiet = true, check = true })
    end
  end
  -- Every base file not reachable yet (through the junctions above) is hard linked, except the executable and .pdb files.
  local function walk(dir)
    for _, n in ipairs(path.list(dir)) do
      local full = path.join(dir, n)
      local attr = lfs.symlinkattributes(full)
      if attr and attr.mode == "directory" then walk(full)
      elseif attr and attr.mode == "file" then
        local relative = full:sub(#base + 2)
        if relative:lower() ~= "windows/openkoikatsu/binaries/win64/openkoikatsu.exe" and path.suffix(n):lower() ~= ".pdb" then
          local new = path.join(target, relative)
          if not path.exists(new) then
            path.mkdir(path.parent(new))
            local linked, message = path.link(full, new)
            if not linked then error("hard link " .. new .. " -> " .. full .. ": " .. tostring(message), 0) end
          end
        end
      end
    end
  end
  walk(base)
  local exe = path.join(target, "Windows/OpenKoikatsu/Binaries/Win64/OpenKoikatsu.exe")
  path.mkdir(path.parent(exe))
  path.copy(executable, exe)
  -- Heroine mind (Tools/ok-mind): loaded at runtime from beside the executable; optional.
  local mind = path.join(root, "Tools/ok-mind/target/release/ok_mind.dll")
  if path.exists(mind) then path.copy(mind, path.join(path.parent(exe), "ok_mind.dll")) end
  -- Game rules (Tools/ok-rules): required; loaded from beside the executable. The tuning built into it is used when the
  -- StreetSocialData/rules folder is not passed (OK_RULES_TUNING).
  local rules = path.join(root, "Tools/ok-rules/target/release/ok_rules.dll")
  if path.exists(rules) then path.copy(rules, path.join(path.parent(exe), "ok_rules.dll"))
  else
    io.stderr:write("WARNING: Game rules not built (cargo build --release in Tools/ok-rules): StreetSocial from this stage quits unless OK_RULES_DLL is set\n")
  end
  return win(exe)
end

local function main()
  local p = ok.args.parser("Stage a StreetSocial build linked to a staged base")
  p:opt("--executable", { required = true })
  p:opt("--destination", { default = "Saved/StreetSocial/Build/Stage" })
  p:opt("--base-dir", { default = "Saved/StreetSocial/Base" })
  local a = p:parse(arg)
  io.write(M.stage(a.executable, a.destination, a.base_dir), "\n")
end

if ok.is_main() then ok.main(main) end
return M
