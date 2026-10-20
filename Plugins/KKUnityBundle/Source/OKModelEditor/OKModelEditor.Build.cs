using UnrealBuildTool;

public class OKModelEditor : ModuleRules
{
	public OKModelEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "KKUnityBundle" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd", "Slate", "SlateCore", "InputCore", "EditorFramework", "ToolMenus", "AssetDefinition", "AssetTools", "AssetRegistry",
			"PropertyEditor", "AdvancedPreviewScene", "Json",
		});
	}
}
