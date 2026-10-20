using UnrealBuildTool;

public class KKCard : ModuleRules
{
	public KKCard(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});
		// The OK card preview scales the original card image into its first frame.
		PrivateDependencyModuleNames.AddRange(new string[] { "ImageWrapper" });
	}
}
