using UnrealBuildTool;
using System.IO;

public class OKStudio : ModuleRules
{
	public OKStudio(ReadOnlyTargetRules Target) : base(Target)
	{
        string ChildAnimationFile = Path.Combine(Path.GetDirectoryName(Target.ProjectFile.FullName), "Content", "IndependentResources", "base_v1", "studio_child_animations.json");
        if (File.Exists(ChildAnimationFile)) RuntimeDependencies.Add(ChildAnimationFile, StagedFileType.NonUFS);
        string MapEnvironmentFile = Path.Combine(Path.GetDirectoryName(Target.ProjectFile.FullName), "Content", "IndependentResources", "base_v1", "studio", "map_environments.json");
        if (File.Exists(MapEnvironmentFile)) RuntimeDependencies.Add(MapEnvironmentFile, StagedFileType.NonUFS);
        string ReflectionRoot = Path.GetDirectoryName(MapEnvironmentFile);
        string ReflectionIndex = Path.Combine(ReflectionRoot, "map_reflections.json");
        if (File.Exists(ReflectionIndex)) RuntimeDependencies.Add(ReflectionIndex, StagedFileType.NonUFS);
        string ReflectionCubes = Path.Combine(ReflectionRoot, "reflections");
        if (Directory.Exists(ReflectionCubes)) foreach (string FileName in Directory.GetFiles(ReflectionCubes, "*.okcube")) RuntimeDependencies.Add(FileName, StagedFileType.NonUFS);
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json", "JsonUtilities", "Slate", "SlateCore", "InputCore" });
		PrivateDependencyModuleNames.AddRange(new string[] { "KKUnityBundle", "KKCard", "OKAnimation", "OKHGShading", "KKRuntimePose", "RHI", "UMG", "RenderCore", "ImageCore", "Niagara", "XmlParser", "HTTP" });
	}
}
