using UnrealBuildTool;
public class PoseBoneAudit : ModuleRules {
 public PoseBoneAudit(ReadOnlyTargetRules Target) : base(Target) {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PrivateDependencyModuleNames.AddRange(new[]{"Core","CoreUObject","Engine","UnrealEd","KKImportedAssets","Json","MeshDescription","SkeletalMeshDescription"});
 }
}
