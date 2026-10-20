-- Installed shader bindings, native material scalar and UE render regression.
local ok = require "ok"
local G = require "gpp"
local json, util = ok.json, ok.util
local check = G.check

local M = {}
local ROOT = G.ROOT

function M.verify_sources()
  local logs = ROOT .. "/Saved/Logs"
  local koikano = G.read(logs .. "/SkinOutlineBindings281.log")
  local forge = G.read(logs .. "/ForgeSkinOutlineBindings281Installed.log")
  check(G.has(koikano, "pass=0 stage=progVertex") and G.has(forge, "pass=0 stage=progVertex"))
  check(G.has(koikano, "cb0[28] byte+8: _linewidthG"))
  check(G.has(koikano, "cb0[29] byte+4: _LineWidthS"))
  check(G.has(koikano, "t0: _DetailMask") and G.has(forge, "t0: _DetailMask"))
  check(not G.has(forge, "_LineWidthS"))
  local koikano_asm = G.read(ROOT .. "/Saved/ShaderDxbcKKS/KK_Plugins.Resources.KKS_Shaders/Koikano_main_skin/0000.asm.txt")
  local forge_asm = G.read(ROOT .. "/Saved/ShaderDxbcEC278/KK_Plugins.Resources.EC_Shaders/Shader_Forge_main_skin/0000.asm.txt")
  check(G.has(koikano_asm, "l(0.600000)") and G.has(koikano_asm, "l(0.003000)"))
  check(G.has(koikano_asm, "mul r0.x, r0.x, cb0[29].y"))
  check(G.has(forge_asm, "l(0.300000)") and G.has(forge_asm, "l(0.005000)"))
  check(G.has(koikano_asm, "sample_l r1.xyzw") and G.has(forge_asm, "sample_l r1.xyzw"))
  local folder = ROOT .. "/Saved/Tests/SkinOutline281"
  check(json.decode(G.read(folder .. "/GameMarchOutline281.json"))["error"] == "")
  for _, suffix in ipairs({ "initial", "final" }) do
    local widths = {}
    for _, line in ipairs(util.splitlines(G.read(folder .. "/GameMarchOutline281.json.head-materials-" .. suffix .. ".tsv"))) do
      local row = util.split(line, "\t")
      if row[1] == "float" and row[2] == "cf_O_face" and row[4] == "_LineWidthS" then widths[#widths + 1] = G.F(row[5]) end
    end
    check(#widths == 1 and widths[1] == 1.0, widths)
  end
  print("Koikano/Forge compiled vertex branches, LOD0/ST bindings and native face scalar passed")
end

local function main()
  M.verify_sources()
  local result = require("verify_eye_clamp").verify({ prefix = "SkinOutline", stage = 281, pipeline = 128, build_log = "SkinOutlineUnrealBuild281Final.log" })
  local shots = G.read(ROOT .. "/Saved/Logs/SkinOutlineShots281.log")
  check(G.has(shots, "OK outline source family=KoikanoSkin width_global=0.473790 width_material=1.000000 detail_lod0=1"))
  check(G.has(shots, "OK outline source family=ForgeSkin width_global=0.473790 width_material=1.000000 detail_lod0=1"))
  check(G.has(shots, "OK outline source family=Hair"))
  result.outline_source = G.O("KoikanoSkin", "(.1*d_m+.6)*.003*global_width*material_width*(1-detail.b)",
    "ForgeSkin", "(.1*d_m+.3)*.005*global_width*(1-detail.b)",
    "native_March_material_width", 1.0, "detail_sampling", "vertex LOD0, linear, own ST")
  G.write_json(ROOT .. "/Saved/Logs/SkinOutline281Audit.json", result)
end

if ok.is_main() then ok.main(main) end
return M
