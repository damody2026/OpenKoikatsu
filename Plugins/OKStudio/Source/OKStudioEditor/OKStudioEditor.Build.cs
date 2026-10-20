using UnrealBuildTool;

public class OKStudioEditor : ModuleRules
{
	public OKStudioEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "OKStudio" });
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd", "Slate", "SlateCore", "InputCore", "EditorFramework", "ToolMenus", "PropertyEditor", "AdvancedPreviewScene",
			"Json", "JsonUtilities", "DesktopPlatform", "KKUnityBundle", "KKCard", "OKAnimation", "Niagara", "NiagaraEditor", "LevelSequence", "MovieScene", "MovieSceneTracks", "CinematicCamera",
			"MovieRenderPipelineCore", "MovieRenderPipelineEditor", "MovieRenderPipelineRenderPasses", "ImageCore",
		});
	}
}
