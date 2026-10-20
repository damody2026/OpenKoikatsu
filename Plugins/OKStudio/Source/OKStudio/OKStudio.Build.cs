using UnrealBuildTool;
using System.IO;

public class OKStudio : ModuleRules
{
	public OKStudio(ReadOnlyTargetRules Target) : base(Target)
	{
        string ChildAnimationFile = Path.Combine(Path.GetDirectoryName(Target.ProjectFile.FullName), "Content", "IndependentResources", "base_v1", "studio_child_animations.json");
        if (File.Exists(ChildAnimationFile)) RuntimeDependencies.Add(ChildAnimationFile, StagedFileType.NonUFS);
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "Slate", "SlateCore", "InputCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "KKUnityBundle", "KKCard", "OKAnimation", "OKHGShading", "KKRuntimePose", "RHI", "UMG", "RenderCore", "ImageCore", "Niagara" });
	}
}
