using UnrealBuildTool;
public class OKHGShading : ModuleRules {
 public OKHGShading(ReadOnlyTargetRules Target) : base(Target) {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PublicDependencyModuleNames.AddRange(new[]{"Core","KKRuntimePose"});
  PrivateDependencyModuleNames.AddRange(new[]{"Projects","RenderCore","CoreUObject","Engine","Json","ImageWrapper"});
 }
}
