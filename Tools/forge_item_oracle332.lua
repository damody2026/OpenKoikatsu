-- Independent original-DXBC versus translated-HLSL MainItem controls.
local ok = require "ok"
local common = require "forge_oracle_common"
local fl = common.fl

common.main({
  name = "MainItem",
  gen = "Saved/Tests/ForgeItem332/Generated",
  out = "Saved/Tests/ForgeItem332/Oracle",
  asm_dir = "Saved/ShaderDxbcEC278/KK_Plugins.Resources.EC_Shaders/Shader_Forge_main_item",
  programs = { { 7, 12 }, { 5, 14 } },
  shadow_index = 5,
  sizes = { [0] = 23, [1] = 5, [2] = 1 },
  variants = common.objects({
    {},
    { { "_ShadowColor", fl(0.0, 0.0, 0.0, 1.0) } },
    { { "_ShadowColor", fl(1.0, 1.0, 1.0, 1.0) } },
    { { "_ShadowColor", fl(0.2, 0.6, 0.9, 0.2) } },
    { { "_SpecularPower", 0.0 } },
    { { "_SpecularPower", 0.75 } },
    { { "_ShadowExtend", 0.3 }, { "_ShadowExtendAnother", 0.6 } },
    { { "_AnotherRampFull", 1.0 } },
    { { "_DetailRLineR", 1.0 }, { "_DetailBLineG", 1.0 } },
    { { "_notusetexspecular", 1.0 } },
    { { "_Color", fl(0.2, 0.6, 0.9, 1.0) }, { "_Color2", fl(0.8, 0.3, 0.2, 1.0) }, { "_Color3", fl(0.4, 0.7, 0.1, 1.0) } },
    { { "_MainTex_ST", fl(2.0, 3.0, -0.3, 0.2) } },
    { { "_NormalMap_ST", fl(5.0, 2.0, 0.13, -0.21) } },
    { { "_RampG_ST", fl(0.7, 1.2, 0.1, -0.3) } },
    { { "_AnotherRamp_ST", fl(2.0, 0.5, -0.1, 0.3) } },
    { { "_ColorMask_ST", fl(3.0, 2.0, -0.2, 0.1) } },
    { { "_LineMask_ST", fl(0.8, 1.4, 0.3, -0.2) } },
    { { "_Clock", fl(0.2, 0.3, 1.1, 0.7) } },
    { { "_ambientshadowG", fl(0.05, 0.15, 0.3, 0.8) } },
    { { "_LineColorG", fl(0.1, 0.4, 0.8, 0.6) } },
    { { "_rimpower", 0.5 }, { "_rimV", 0.7 } },
    { { "_Color", fl(1.0, 1.0, 1.0, 1.0) }, { "_Color2", fl(1.0, 1.0, 1.0, 1.0) }, { "_Color3", fl(1.0, 1.0, 1.0, 1.0) } },
  }),
  labels = { "Forward7", "Forward5" },
  negative = "cb0[11]",
  exe = "Saved/Tests/ForgeItem332/oracle.exe",
  cpp = "Tools/forge_item_oracle332.cpp",
  log = "Saved/Logs/ForgeItem332GpuParity.json",
})
