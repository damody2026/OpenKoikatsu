using UnrealBuildTool;
public class GpuPoseAuditEditorTarget:TargetRules {
 public GpuPoseAuditEditorTarget(TargetInfo Target):base(Target){Type=TargetType.Editor;DefaultBuildSettings=BuildSettingsVersion.V7;IncludeOrderVersion=EngineIncludeOrderVersion.Unreal5_8;ExtraModuleNames.Add("GpuPoseAudit");}
}
