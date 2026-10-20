using UnrealBuildTool;
public class KKCrowdGpuPacking : ModuleRules {
 public KKCrowdGpuPacking(ReadOnlyTargetRules Target) : base(Target) {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PublicDependencyModuleNames.AddRange(new[]{"Core","RenderCore","RHI"});
  PrivateDependencyModuleNames.AddRange(new[]{"Projects","CoreUObject","Engine","Json","Renderer"});
  PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory,"Source/Runtime/Renderer/Private"));
  PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory,"Source/Runtime/Renderer/Internal"));
  PrivateIncludePaths.Add(System.IO.Path.Combine(EngineDirectory,"Source/Runtime/Engine/Internal"));
 }
}
