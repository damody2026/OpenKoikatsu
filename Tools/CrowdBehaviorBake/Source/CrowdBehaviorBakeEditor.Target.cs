using UnrealBuildTool;
public class CrowdBehaviorBakeEditorTarget:TargetRules {public CrowdBehaviorBakeEditorTarget(TargetInfo T):base(T){Type=TargetType.Editor;DefaultBuildSettings=BuildSettingsVersion.V7;IncludeOrderVersion=EngineIncludeOrderVersion.Unreal5_8;ExtraModuleNames.Add("CrowdBehaviorBake");}}
