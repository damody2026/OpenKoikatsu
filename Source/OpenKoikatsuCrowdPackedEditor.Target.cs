using UnrealBuildTool;
public class OpenKoikatsuCrowdPackedEditorTarget : TargetRules
{
    public OpenKoikatsuCrowdPackedEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        LinkType = TargetLinkType.Modular;
        bMergeModules = false;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("OpenKoikatsu");
        PreBuildSteps.Add("call \"$(ProjectDir)\\Tools\\ok-extract\\build-release.cmd\"");
    }
}
