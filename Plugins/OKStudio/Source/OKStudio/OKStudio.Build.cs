using UnrealBuildTool;

public class OKStudio : ModuleRules
{
	public OKStudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "Slate", "SlateCore", "InputCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "KKUnityBundle", "KKCard", "OKAnimation", "OKHGShading", "KKRuntimePose", "RHI", "UMG", "RenderCore", "ImageCore", "Niagara" });
	}
}
