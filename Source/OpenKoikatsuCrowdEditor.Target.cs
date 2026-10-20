using UnrealBuildTool;

// Separate target state keeps crowd validation builds independent of stale
// engine plugin hot reload mappings from the interactive editor target.
public class OpenKoikatsuCrowdEditorTarget : TargetRules
{
    public OpenKoikatsuCrowdEditorTarget(TargetInfo Target) : base(Target)
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
