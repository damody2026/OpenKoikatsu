-- Builds Tools/GamePoseProbe/OpenKoikatsu.GamePoseProbe.dll (BepInEx plugin) with the .NET Framework csc.exe
-- against the installed game's CharaStudio_Data/Managed assemblies and BepInEx core.
--   Tools\lua\lua.exe Tools\GamePoseProbe\build.lua
local ok = require "ok"
local path = ok.path

local function win(p) return (p:gsub("/", "\\")) end

local game = ok.game_root()
local here = ok.script_dir
local game_managed = game .. "/CharaStudio_Data/Managed"
local args = { "C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe", "/nologo", "/target:library",
  "/out:" .. win(path.join(here, "OpenKoikatsu.GamePoseProbe.dll")) }
for _, name in ipairs({ "UnityEngine.dll", "UnityEngine.CoreModule.dll", "UnityEngine.AnimationModule.dll",
  "UnityEngine.JSONSerializeModule.dll", "UnityEngine.ImageConversionModule.dll", "UnityEngine.AssetBundleModule.dll",
  "Assembly-CSharp.dll", "Assembly-CSharp-firstpass.dll" }) do
  args[#args + 1] = "/reference:" .. win(path.join(game_managed, name))
end
args[#args + 1] = "/reference:" .. win(game .. "/BepInEx/core/BepInEx.dll")
args[#args + 1] = "/reference:" .. win(game .. "/BepInEx/core/0Harmony.dll")
for _, source in ipairs({ "Probe.cs", "AccessoryPhysicsControl.cs", "HPairControl.cs", "GenericIkScaleControl.cs",
    "GenericDynamicScaleControl.cs", "GenericColliderControl.cs", "StudioVisualAudit.cs" }) do
  args[#args + 1] = win(path.join(here, source))
end
local res = ok.proc.run(args)
if res.error then error("cannot run csc.exe: " .. tostring(res.error)) end
if res.code ~= 0 then os.exit(res.code) end
