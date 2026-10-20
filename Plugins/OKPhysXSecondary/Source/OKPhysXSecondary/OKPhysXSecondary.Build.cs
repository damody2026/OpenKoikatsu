using System.IO;
using UnrealBuildTool;

public class OKPhysXSecondary : ModuleRules
{
	public OKPhysXSecondary(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs; // the shared Engine PCH forward-declares physx::PxQuat (Chaos) and clashes with the real SDK
		// Each file on its own: a unity file would put the PhysX SDK next to Chaos headers (physx::PxTransform defined twice)
		// and merge the backends' file-local helpers (F3 ...). OK_Demo never saw this: untracked files build non-unity.
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "Projects", "Engine", "CoreUObject" });
		// Comparison backends: KawaiiPhysics (plugin, MIT) and the engine's Chaos PBD solver.
		PrivateDependencyModuleNames.AddRange(new string[] { "KawaiiPhysics", "AnimGraphRuntime", "Chaos", "ChaosCore" });
		if (Target.Platform != UnrealTargetPlatform.Win64)
		{
			throw new BuildException("OKPhysXSecondary links the PhysX 5.11 SDK staged by the Flex plugin and is Win64 only.");
		}

		// Headers are a private copy of PhysX 5.11 (BSD-3). Libraries are the ones the Flex
		// plugin already builds from the same SDK, so both stay on one PhysX version.
		string Plugin = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
		PrivateIncludePaths.Add(Path.Combine(Plugin, "Source", "ThirdParty", "PhysX", "include"));
		string FlexLib = Path.Combine(EngineDirectory, "Plugins", "GameWorks", "Flex", "Source", "ThirdParty", "PhysX58", "lib", "Win64");
		foreach (string Name in new[] { "PhysX", "PhysXCommon", "PhysXFoundation", "PhysXCooking", "PhysXExtensions_static", "PhysXPvdSDK_static" })
		{
			string Library = Path.Combine(FlexLib, Name + ".lib");
			if (!File.Exists(Library))
			{
				throw new BuildException("Missing PhysX library {0}. Run Engine/Plugins/GameWorks/Flex/Build/PhysX58/Build.ps1 -Stage.", Library);
			}
			PublicAdditionalLibraries.Add(Library);
			ExternalDependencies.Add(Library);
		}
		// The DLLs are copied at build time into Binaries/ThirdParty/Win64 (not in git): PhysX from the engine's Flex
		// staging (same SDK build as the libraries above), MuJoCo from Source/ThirdParty/MuJoCo/bin.
		string BinaryDirectory = Path.Combine(Plugin, "Binaries", "ThirdParty", "Win64");
		string FlexBin = Path.Combine(EngineDirectory, "Plugins", "GameWorks", "Flex", "Binaries", "ThirdParty", "PhysX58", "Win64");
		foreach (string Name in new[] { "PhysX", "PhysXCommon", "PhysXFoundation", "PhysXCooking" })
		{
			string Dll = Path.Combine(FlexBin, Name + ".dll");
			if (!File.Exists(Dll))
			{
				throw new BuildException("Missing PhysX DLL {0}. Run Engine/Plugins/GameWorks/Flex/Build/PhysX58/Build.ps1 -Stage.", Dll);
			}
			PublicDelayLoadDLLs.Add(Name + ".dll");
			RuntimeDependencies.Add(Path.Combine(BinaryDirectory, Name + ".dll"), Dll);
		}
		RuntimeDependencies.Add(Path.Combine(BinaryDirectory, "PhysX-LICENSE.md"), Path.Combine(FlexBin, "PhysX-LICENSE.md"), StagedFileType.NonUFS);

		// More comparison backends, built by Tools/build_physics_backends.lua: Jolt 5.6.0 (MIT) as a static library,
		// MuJoCo 3.15.0 (Apache-2.0) as a DLL. (Bullet is not used by this project.)
		string ThirdParty = Path.Combine(Plugin, "Source", "ThirdParty");
		// Shipping carries PhysX, KawaiiPhysics and Chaos only; Jolt and MuJoCo are for comparing solvers in development.
		bool bComparison = Target.Configuration != UnrealTargetConfiguration.Shipping;
		PrivateDefinitions.Add("OK_SECONDARY_COMPARISON_BACKENDS=" + (bComparison ? "1" : "0"));
		if (!bComparison) return;
		string JoltLib = Path.Combine(ThirdParty, "Jolt", "lib", "Jolt.lib");
		string MuJoCoLib = Path.Combine(ThirdParty, "MuJoCo", "lib", "mujoco.lib");
		if (!File.Exists(JoltLib) || !File.Exists(MuJoCoLib))
		{
			throw new BuildException("Missing Jolt/MuJoCo libraries. Run Tools/build_physics_backends.lua.");
		}
		// Must match the library (Distribution config, SSE4.2 + LZCNT/TZCNT). UE never defines NDEBUG, which would turn on
		// JPH_DEBUG in the headers and change BodyManager's layout (body locks then read garbage): JPH_NO_DEBUG.
		PrivateIncludePaths.Add(Path.Combine(ThirdParty, "Jolt", "include"));
		PrivateDefinitions.AddRange(new string[] { "JPH_USE_SSE4_1", "JPH_USE_SSE4_2", "JPH_USE_LZCNT", "JPH_USE_TZCNT", "JPH_NO_DEBUG" });
		PublicAdditionalLibraries.Add(JoltLib);
		PrivateIncludePaths.Add(Path.Combine(ThirdParty, "MuJoCo", "include"));
		PublicAdditionalLibraries.Add(MuJoCoLib);
		PublicDelayLoadDLLs.Add("mujoco.dll");
		string MuJoCoBin = Path.Combine(ThirdParty, "MuJoCo", "bin");
		RuntimeDependencies.Add(Path.Combine(BinaryDirectory, "mujoco.dll"), Path.Combine(MuJoCoBin, "mujoco.dll"));
		RuntimeDependencies.Add(Path.Combine(BinaryDirectory, "MuJoCo-LICENSE.txt"), Path.Combine(MuJoCoBin, "MuJoCo-LICENSE.txt"), StagedFileType.NonUFS);
	}
}
