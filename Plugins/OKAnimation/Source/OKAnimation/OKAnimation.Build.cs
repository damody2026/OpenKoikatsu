using UnrealBuildTool;

public class OKAnimation : ModuleRules
{
	public OKAnimation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Files keep their own anonymous-namespace helpers (Basis, Numbers ...): no unity merging.
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AnimationCore", "AnimGraphRuntime", "DeveloperSettings", "Json", "KKUnityBundle" });
		PrivateDependencyModuleNames.AddRange(new string[] { "JsonUtilities", "Projects", "Slate", "SlateCore", "InputCore", "KKCard" });
	}
}
