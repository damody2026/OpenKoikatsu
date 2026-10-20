using UnrealBuildTool;

public class OKAnimationEditor : ModuleRules
{
	public OKAnimationEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Written and tested as separate files (anonymous-namespace helpers): no unity merging.
		bUseUnity = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "OKAnimation" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd", "Slate", "SlateCore", "InputCore", "EditorFramework", "ToolMenus", "AssetDefinition", "AssetTools", "AssetRegistry",
			"ContentBrowser", "PropertyEditor", "AdvancedPreviewScene", "Json", "JsonUtilities", "KKUnityBundle", "AnimationCore", "AnimGraphRuntime", "CurveEditor", "MeshDescription", "StaticMeshDescription", "SkeletalMeshDescription", "Kismet", "AnimGraph", "BlueprintGraph", "OKAnimationGraph", "LevelSequence", "MovieScene", "MovieSceneTracks", "KKCard", "KKSideloader", "Projects",
		});
	}
}

