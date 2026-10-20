using UnrealBuildTool;

// Independent target state for compiling crowd engine experiments. The
// resulting Engine DLL is linked into the private crowd runtime overlay.
public class OpenKoikatsuCrowdEngineEditorTarget : TargetRules
{
    public OpenKoikatsuCrowdEngineEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.V7;
        IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
        ExtraModuleNames.Add("OpenKoikatsu");
    }
}
