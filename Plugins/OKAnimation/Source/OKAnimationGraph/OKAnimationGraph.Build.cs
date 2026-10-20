using UnrealBuildTool;

public class OKAnimationGraph : ModuleRules
{
	public OKAnimationGraph(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Written and tested as separate files (anonymous-namespace helpers): no unity merging.
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AnimGraph", "AnimGraphRuntime", "BlueprintGraph", "OKAnimation" });
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[] { "UnrealEd", "Slate", "SlateCore" });
		}
	}
}
