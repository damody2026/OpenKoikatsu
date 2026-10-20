-- Independent original-DXBC versus translated-HLSL MainAlpha controls.
local ok = require "ok"
local common = require "forge_oracle_common"
local fl = common.fl

common.main({
  name = "MainAlpha",
  gen = "Saved/Tests/ForgeAlpha333/Generated",
  out = "Saved/Tests/ForgeAlpha333/Oracle",
  asm_dir = "Saved/ShaderDxbcEC278/KK_Plugins.Resources.EC_Shaders/Shader_Forge_main_alpha",
  programs = { { 17, 12 }, { 6, 14 } },
  shadow_index = 6,
  sizes = { [0] = 24, [1] = 5, [2] = 1 },
  variants = common.objects({
    {},
    { { "_ShadowColor", fl(0.0, 0.0, 0.0, 1.0) } },
    { { "_ShadowColor", fl(1.0, 1.0, 1.0, 1.0) } },
    { { "_ShadowColor", fl(0.2, 0.6, 0.9, 0.2) } },
    { { "_SpecularPower", 0.0 } },
    { { "_SpecularPower", 0.75 } },
    { { "_ShadowExtend", 0.3 }, { "_ShadowExtendAnother", 0.6 } },
    { { "_alpha_a", 0.0 }, { "_alpha_b", 0.0 } },
    { { "_DetailBLineGA", 1.0 } },
    { { "_outline", 0.0 } },
    { { "_liquidftop", 1.0 }, { "_liquidfbot", 0.7 }, { "_liquidbtop", 0.3 }, { "_liquidbbot", 0.5 }, { "_liquidface", 0.8 } },
    { { "_MainTex_ST", fl(2.0, 3.0, -0.3, 0.2) } },
    { { "_NormalMap_ST", fl(5.0, 2.0, 0.13, -0.21) } },
    { { "_RampG_ST", fl(0.7, 1.2, 0.1, -0.3) } },
    { { "_AnotherRamp_ST", fl(2.0, 0.5, -0.1, 0.3) } },
    { { "_Texture2_ST", fl(3.0, 2.0, -0.2, 0.1) } },
    { { "_Texture3_ST", fl(0.8, 1.4, 0.3, -0.2) } },
    { { "_liquidmask_ST", fl(0.2, 0.3, 1.1, 0.7) } },
    { { "_ambientshadowG", fl(0.05, 0.15, 0.3, 0.8) } },
    { { "_LineColorG", fl(0.1, 0.4, 0.8, 0.6) } },
    { { "_rimpower", 0.5 }, { "_rimV", 0.7 } },
    { { "_Color", fl(1.0, 1.0, 1.0, 1.0) }, { "_Color2", fl(1.0, 1.0, 1.0, 1.0) }, { "_Color3", fl(1.0, 1.0, 1.0, 1.0) } },
  }),
  labels = { "Forward17", "Forward6" },
  negative = "cb0[7]",
  exe = "Saved/Tests/ForgeAlpha333/oracle.exe",
  cpp = "Tools/forge_alpha_oracle333.cpp",
  log = "Saved/Logs/ForgeAlpha333GpuParity.json",
})
