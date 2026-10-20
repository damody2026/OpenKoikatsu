using UnrealBuildTool;

public class KKSideloader : ModuleRules
{
	public KKSideloader(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"XmlParser",
			"KKCard"
		});

		AddEngineThirdPartyPrivateStaticDependencies(Target, "zlib");
	}
}
