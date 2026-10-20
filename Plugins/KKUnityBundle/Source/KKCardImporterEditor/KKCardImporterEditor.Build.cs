using UnrealBuildTool;
public class KKCardImporterEditor : ModuleRules {
 public KKCardImporterEditor(ReadOnlyTargetRules Target) : base(Target) {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PrivateDependencyModuleNames.AddRange(new string[]{"Core","CoreUObject","Engine","KKUnityBundle","KKImportedAssets","UnrealEd","Slate","SlateCore","ToolMenus","DesktopPlatform","ContentBrowser","AssetRegistry"});
 }
}
