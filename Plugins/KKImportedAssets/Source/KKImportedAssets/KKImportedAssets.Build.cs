using UnrealBuildTool;
public class KKImportedAssets : ModuleRules
{
    public KKImportedAssets(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        if(Target.bBuildEditor)DynamicallyLoadedModuleNames.Add("AnimationDataController");
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
    }
}
